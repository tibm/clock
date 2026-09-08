// ESP32-S3 HAL.                                            [FIRMWARE.md §2, §12.0]
//
// STATUS: `clock_` is real; every peripheral below is an honest stub returning NotPresent.
// That is not a placeholder apology -- on BOARD=devkit it is the *correct* answer until you
// wire something up (board_cfg starts the devkit with an empty presence mask), and D16 says
// absence is a first-class result, never a faked success and never an error log.
//
// Filling these in is the day-one devkit task, in the order §12.0 gives:
//   pixels  -> espressif/led_strip 3.0.3, SPI3 backend on kPins.neopix   (D4)
//   adc     -> adc_oneshot + adc_cali curve fitting, ADC1_CH0/CH1
//   wake    -> ledc, ~1 kHz, gamma applied above this layer
//   knob    -> pcnt unit0 + glitch filter, ENC_SW as a GPIO IRQ
//   i2c     -> i2c_master at 400 kHz, generous timeouts (the BNO085 clock-stretches)
//   motor   -> 2x MCPWM + GPTimer0: Q16.16 phase accumulator, quarter-sine LUT, 8
//              comparators, comparator target latches the stop                        (D5)
//   expander-> MCP23017 over i2c; today it is named signals, it becomes the driver     (§11.2)
//   imu     -> BNO085 SHTP/SH-2 over i2c, SENSOR_INT on IO42
// Each one is independently testable the moment its part is on the breadboard, which is
// exactly why the presence mask is per-device rather than per-board.
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/rtc.h"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/mcp23017.hpp"
#include "clk/log.hpp"
#include "clk/port.hpp"

namespace clk::hal {

namespace clock_ {

uint64_t micros() noexcept { return static_cast<uint64_t>(esp_timer_get_time()); }
uint32_t millis() noexcept { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void sleep_ms(uint32_t ms) noexcept { vTaskDelay(pdMS_TO_TICKS(ms)); }

// The one peripheral answer on this side that is real today, and it is real because it costs
// a register read: `esp_clk_init()` has already chosen the source by the time app_main runs,
// and it announces a fallback exactly once, in a boot log nobody is reading a week later.
SlowSrc slow_src() noexcept {
    switch (::rtc_clk_slow_src_get()) {
        case SOC_RTC_SLOW_CLK_SRC_XTAL32K:
            return SlowSrc::Xtal32k;
        case SOC_RTC_SLOW_CLK_SRC_RC_SLOW:
            return SlowSrc::RcSlow;
        case SOC_RTC_SLOW_CLK_SRC_RC_FAST_D256:
            return SlowSrc::RcFastD256;
        default:
            return SlowSrc::Unknown;
    }
}

}  // namespace clock_

// ============================ hal::adc ===================================================
// ADC1 one-shot with curve-fitting calibration.  ADC1 and not ADC2 because ADC2 is shared
// with the Wi-Fi radio and reads fail while the radio is up -- which would present as a
// homing sensor that works perfectly until the clock joins a network.
namespace adc {
namespace {

// IO1 = ADC1_CH0 (VBAT_SENSE), IO2 = ADC1_CH1 (HOME_OPTO).  esp32.md, and kPins agrees.
constexpr adc_channel_t kChan[] = {ADC_CHANNEL_0, ADC_CHANNEL_1};
// Four conversions a read.  A single sample off a phototransistor jitters by tens of mV,
// which turns a threshold into a coin-flip; four is enough to steady the bench number and
// still costs ~100 us, well inside homing's 1 kHz poll (Section 14).
constexpr int kAvg = 4;
// The divider node is 100k||100k with C110's 100 nF across it: tau = 5 ms, so five time
// constants is 25 ms.  It only has to be paid when something asks for the cell voltage.
constexpr uint32_t kVbatSettleMs = 30;

adc_oneshot_unit_handle_t g_unit = nullptr;
adc_cali_handle_t g_cali = nullptr;
bool g_failed = false;

bool ready() noexcept {
    if (g_unit || g_failed) return g_unit != nullptr;
    adc_oneshot_unit_init_cfg_t init{};
    init.unit_id = ADC_UNIT_1;
    if (const esp_err_t e = ::adc_oneshot_new_unit(&init, &g_unit); e != ESP_OK) {
        g_unit = nullptr;
        g_failed = true;
        CLK_LOGE(drv_opto, "adc unit: %s", ::esp_err_to_name(e));
        return false;
    }
    adc_oneshot_chan_cfg_t ch{};
    // 12 dB gets the full 0-3.1 V span.  Both nodes swing most of the rail -- the opto's
    // 10k pull-up against a saturating phototransistor, and Vbat halved to ~2.1 V -- so a
    // narrower attenuation would clip the useful end of each.
    ch.atten = ADC_ATTEN_DB_12;
    ch.bitwidth = ADC_BITWIDTH_DEFAULT;
    for (const auto c : kChan) (void)::adc_oneshot_config_channel(g_unit, c, &ch);

    adc_cali_curve_fitting_config_t cal{};
    cal.unit_id = ADC_UNIT_1;
    cal.atten = ADC_ATTEN_DB_12;
    cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (::adc_cali_create_scheme_curve_fitting(&cal, &g_cali) != ESP_OK) {
        g_cali = nullptr;  // uncalibrated: raw counts scaled, and the log says so once
        CLK_LOGW(drv_opto, "no eFuse ADC calibration; millivolts are approximate");
    }
    return true;
}

Result<uint16_t> sample(adc_channel_t c) noexcept {
    int32_t acc = 0;
    for (int i = 0; i < kAvg; ++i) {
        int raw = 0;
        if (::adc_oneshot_read(g_unit, c, &raw) != ESP_OK) {
            return Result<uint16_t>::bad(Status::Failed);
        }
        int mv = raw;
        if (g_cali) {
            if (::adc_cali_raw_to_voltage(g_cali, raw, &mv) != ESP_OK) mv = raw;
        } else {
            mv = raw * 3100 / 4095;  // nominal full scale at 12 dB; honest enough to log
        }
        acc += mv;
    }
    return Result<uint16_t>::good(static_cast<uint16_t>(acc / kAvg));
}

}  // namespace

Result<uint16_t> read_mv(Ch ch) noexcept {
    const bool vbat = ch == Ch::Vbat;
    if (!board::present(vbat ? board::Dev::Vbat : board::Dev::Opto)) {
        return Result<uint16_t>::bad(Status::NotPresent);
    }
    if (!ready()) return Result<uint16_t>::bad(Status::NotPresent);
    if (!vbat) return sample(kChan[1]);

    // Vbat sits behind Q3.  With VBAT_DIV_EN low the divider's bottom leg is OPEN, and R22
    // then pulls the ADC node up to the cell (D14 clamping it to ~3.5 V) -- a reading that
    // looks entirely plausible and means nothing.  So switch the leg in, let C110 settle,
    // read, and put it back the way it was: the default is disconnected because the divider
    // is a permanent 20 uA drain on a backup cell otherwise.
    const auto was = expander::get(expander::Sig::VbatDivEn);
    if (!was.ok()) return Result<uint16_t>::bad(was.st);
    if (const Status st = expander::set(expander::Sig::VbatDivEn, true); st != Status::Ok) {
        return Result<uint16_t>::bad(st);
    }
    clock_::sleep_ms(kVbatSettleMs);
    const auto mv = sample(kChan[0]);
    (void)expander::set(expander::Sig::VbatDivEn, was.v);
    if (!mv.ok()) return mv;
    // R22/R23 are 100k/100k, so the pin sees half the cell.  Undoing it here keeps every
    // caller in volts-at-the-cell and stops the divider ratio leaking upward.
    return Result<uint16_t>::good(static_cast<uint16_t>(mv.v * 2));
}

Result<float> read_opto_norm() noexcept {
    const auto mv = read_mv(Ch::Opto);
    if (!mv.ok()) return Result<float>::bad(mv.st);
    return Result<float>::good(opto_norm_from_mv(mv.v));
}

}  // namespace adc

namespace knob {
Result<State> read() noexcept { return Result<State>::bad(Status::NotPresent); }
}  // namespace knob

namespace motor {
Status enable(bool) noexcept { return Status::NotPresent; }
bool enabled() noexcept { return false; }
Status run(Hand, int32_t, int32_t) noexcept { return Status::NotPresent; }
Status hold(Hand) noexcept { return Status::NotPresent; }
Status adopt(Hand, int32_t) noexcept { return Status::NotPresent; }
Axis state(Hand) noexcept { return Axis{}; }
}  // namespace motor

// ============================ hal::pixels ================================================
// SK6812 RGBW x7 on IO7 through the SN74AHCT1G125 3V3->5V buffer, driven by led_strip's
// SPI3 backend with DMA -- not RMT (D4): RMT's channels are wanted elsewhere and the SPI
// backend does not fight the GPTimer ISR that will be commutating the stepper.
namespace pixels {
namespace {

led_strip_handle_t g_strip = nullptr;
bool g_failed = false;
// led_strip has no readback, and `get()` is documented as "last value written", so the
// mirror IS the answer rather than a cache of one.  It also survives a driver that failed
// to install, which keeps `ui led` printing something truthful on a board with no strip.
Rgbw g_px[kCount]{};

led_strip_handle_t strip() noexcept {
    if (g_strip || g_failed) return g_strip;
    led_strip_config_t cfg{};
    cfg.strip_gpio_num = board::kPins.neopix;
    cfg.max_leds = kCount;
    cfg.led_model = LED_MODEL_SK6812;
    // SK6812 RGBW: four bytes a pixel, white on its own die.  Getting this wrong shows up as
    // colours that are right but shifted one channel along the chain, which reads like a
    // wiring fault and is not one.
    cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW;
    cfg.flags.invert_out = false;

    led_strip_spi_config_t spi{};
    spi.clk_src = SPI_CLK_SRC_DEFAULT;
    spi.spi_bus = SPI3_HOST;
    spi.flags.with_dma = true;

    if (const esp_err_t err = ::led_strip_new_spi_device(&cfg, &spi, &g_strip); err != ESP_OK) {
        g_strip = nullptr;
        g_failed = true;
        CLK_LOGE(drv_led, "led_strip install failed: %s", ::esp_err_to_name(err));
    }
    return g_strip;
}

}  // namespace

Status set(std::size_t i, Rgbw c) noexcept {
    if (i >= kCount) return Status::BadArg;
    if (!board::present(board::Dev::Pixels)) return Status::NotPresent;
    auto* s = strip();
    if (!s) return Status::NotPresent;
    // Mirror first: what the caller asked for is what `get()` must report, whether or not
    // the chain is physically there to show it.
    g_px[i] = c;
    return ::led_strip_set_pixel_rgbw(s, i, c.r, c.g, c.b, c.w) == ESP_OK ? Status::Ok
                                                                          : Status::Failed;
}

Status set_all(Rgbw c) noexcept {
    for (std::size_t i = 0; i < kCount; ++i) {
        if (const Status st = set(i, c); st != Status::Ok) return st;
    }
    return Status::Ok;
}

Status refresh() noexcept {
    if (!board::present(board::Dev::Pixels)) return Status::NotPresent;
    auto* s = strip();
    if (!s) return Status::NotPresent;
    return ::led_strip_refresh(s) == ESP_OK ? Status::Ok : Status::Failed;
}

Rgbw get(std::size_t i) noexcept { return i < kCount ? g_px[i] : Rgbw{}; }

}  // namespace pixels

namespace wake {
Status set(uint8_t, uint8_t) noexcept { return Status::NotPresent; }
uint8_t warm() noexcept { return 0; }
uint8_t cool() noexcept { return 0; }
}  // namespace wake

// ============================ hal::i2c ===================================================
// Real, as of 2026-09-07 -- the first peripheral on this side that is.  One bus, shared by
// the expander, the amp and the sensor daughterboard (esp32.md), so the handle is a file
// static and the devices hang off it.
namespace i2c {
namespace {

constexpr uint32_t kFreqHz = 400'000;
// Generous on purpose: the BNO085 clock-stretches, and a timeout tuned to the fastest device
// on a shared bus is a timeout that fails intermittently on the slowest one.
constexpr int kTimeoutMs = 50;
constexpr uint8_t kAddrFirst = 0x08, kAddrLast = 0x77;  // the 7-bit range that is not reserved

i2c_master_bus_handle_t g_bus = nullptr;
bool g_bus_failed = false;

// Four is every device this board can hold at once (expander, amp, and two of the three on
// the daughterboard in any one conversation).  A miss evicts the oldest, which on a bus this
// small never happens twice in a row.
struct Slot {
    uint8_t addr;
    i2c_master_dev_handle_t h;
};
Slot g_dev[4]{};
std::size_t g_next = 0;

i2c_master_bus_handle_t bus() noexcept {
    if (g_bus || g_bus_failed) return g_bus;
    i2c_master_bus_config_t cfg{};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = static_cast<gpio_num_t>(board::kPins.i2c_sda);
    cfg.scl_io_num = static_cast<gpio_num_t>(board::kPins.i2c_scl);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    // R95/R96 are 4.7 k on the board and the sensor daughterboard parallels 10 k more
    // (esp32.md). The internal pull-ups are tens of kilohms and could not hold 400 kHz
    // anyway; enabling them would mask a missing external resistor, which is the one fault
    // this bus can have that is worth finding on the bench rather than in the field.
    cfg.flags.enable_internal_pullup = false;
    if (const esp_err_t err = ::i2c_new_master_bus(&cfg, &g_bus); err != ESP_OK) {
        g_bus = nullptr;
        g_bus_failed = true;  // a bus that would not install will not install on retry either
        CLK_LOGE(drv_exp, "i2c bus install failed: %s", ::esp_err_to_name(err));
    }
    return g_bus;
}

i2c_master_dev_handle_t dev(uint8_t addr) noexcept {
    auto* b = bus();
    if (!b) return nullptr;
    for (auto const& s : g_dev) {
        if (s.h && s.addr == addr) return s.h;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = kFreqHz;
    i2c_master_dev_handle_t h = nullptr;
    if (::i2c_master_bus_add_device(b, &cfg, &h) != ESP_OK) return nullptr;
    Slot& slot = g_dev[g_next];
    g_next = (g_next + 1) % (sizeof(g_dev) / sizeof(g_dev[0]));
    if (slot.h) (void)::i2c_master_bus_rm_device(slot.h);
    slot = Slot{addr, h};
    return h;
}

// A device that does not ACK is ABSENT, not broken -- that is the whole of D16 and it is why
// an unplugged daughterboard reads as NotPresent rather than as a bus error.  A timeout is
// the other thing entirely: somebody is holding the line down, and that IS a failure.
Status err_to_status(esp_err_t e) noexcept {
    if (e == ESP_OK) return Status::Ok;
    return e == ESP_ERR_TIMEOUT ? Status::Failed : Status::NotPresent;
}

}  // namespace

Result<std::size_t> scan(uint8_t* out, std::size_t cap) noexcept {
    auto* b = bus();
    if (!b) return Result<std::size_t>::bad(Status::NotPresent);
    std::size_t n = 0;
    for (uint8_t a = kAddrFirst; a <= kAddrLast; ++a) {
        const esp_err_t first = ::i2c_master_probe(b, a, kTimeoutMs);
        // A timeout is not a NACK: somebody is holding the bus down, and WHICH address the
        // sweep was on when that happened is the whole diagnosis.  Random addresses mean a
        // device is stretching SCL for everyone (the BNO085 does exactly this while it has an
        // SHTP packet nobody has read); the same address every time means that one device.
        // IDF logs the timeout at E from its own tag and does not say where, so say it here.
        if (first == ESP_ERR_TIMEOUT) CLK_LOGW(drv_exp, "i2c: bus held at 0x%02X", a);
        if (first != ESP_OK) continue;
        // Confirm before believing it.  Measured on rev0.3, 2026-09-08: a single probe
        // false-ACKs at a RANDOM address roughly once every 500 probes -- three hits across
        // fifteen sweeps, at 0x27, 0x33 and 0x4E, never the same address twice.  The two real
        // devices answered 15 sweeps out of 15, and twenty register reads of a known value
        // came back exact, so the bus is fine and it is the probe that lies.  A second
        // independent ACK costs a millisecond and 112 probes, and it is the difference
        // between a scan you can act on and a scan that sends you hunting a chip that was
        // never on the schematic.
        ::vTaskDelay(pdMS_TO_TICKS(2));
        const esp_err_t second = ::i2c_master_probe(b, a, kTimeoutMs);
        if (second == ESP_ERR_TIMEOUT) CLK_LOGW(drv_exp, "i2c: bus held at 0x%02X", a);
        if (second != ESP_OK) continue;
        if (out && n < cap) out[n] = a;
        ++n;  // counted even past `cap`, so a caller with a small buffer still learns the truth
    }
    return Result<std::size_t>::good(n);
}

Result<uint8_t> read_reg(uint8_t addr, uint8_t reg) noexcept {
    auto* h = dev(addr);
    if (!h) return Result<uint8_t>::bad(Status::NotPresent);
    uint8_t v = 0;
    const esp_err_t err = ::i2c_master_transmit_receive(h, &reg, 1, &v, 1, kTimeoutMs);
    if (err != ESP_OK) return Result<uint8_t>::bad(err_to_status(err));
    return Result<uint8_t>::good(v);
}

Status write_reg(uint8_t addr, uint8_t reg, uint8_t val) noexcept {
    auto* h = dev(addr);
    if (!h) return Status::NotPresent;
    const uint8_t buf[2] = {reg, val};
    return err_to_status(::i2c_master_transmit(h, buf, sizeof buf, kTimeoutMs));
}

}  // namespace i2c

namespace imu {
Result<State> read() noexcept { return Result<State>::bad(Status::NotPresent); }
}  // namespace imu

// The named-signal surface is the MCP23017 driver now (shared/mcp23017.cpp), which reaches
// the chip through hal::i2c above.  Nothing here knows a register.
namespace expander {
Result<bool> get(Sig s) noexcept { return mcp23017::get(s); }
Status set(Sig s, bool level) noexcept { return mcp23017::set(s, level); }
}  // namespace expander

namespace audio {
Status enable(bool) noexcept { return Status::NotPresent; }
bool active() noexcept { return false; }
Status set_volume_pct(uint8_t) noexcept { return Status::NotPresent; }
uint8_t volume_pct() noexcept { return 0; }
}  // namespace audio

namespace power {
Result<State> read() noexcept { return Result<State>::bad(Status::NotPresent); }
}  // namespace power

// NVS, and it is real on this side already: the per-unit hand calibration (§6.1) has to
// survive a power cut before anything else does, and `nvs_flash_init()` has been in
// app_main since milestone 0.  One namespace, `clock`, which is where §7.5's Config lands
// when `storage` (§6.3) exists -- this is that drawer, opened early for two keys.
namespace store {

namespace {
constexpr const char* kNs = "clock";
}  // namespace

Result<int32_t> get_i32(const char* key) noexcept {
    if (!key || !*key) return Result<int32_t>::bad(Status::BadArg);
    nvs_handle_t h{};
    if (::nvs_open(kNs, NVS_READONLY, &h) != ESP_OK)
        return Result<int32_t>::bad(Status::NotPresent);
    int32_t v = 0;
    const esp_err_t err = ::nvs_get_i32(h, key, &v);
    ::nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Result<int32_t>::bad(Status::NotPresent);
    return err == ESP_OK ? Result<int32_t>::good(v) : Result<int32_t>::bad(Status::Failed);
}

Status set_i32(const char* key, int32_t value) noexcept {
    if (!key || !*key) return Status::BadArg;
    nvs_handle_t h{};
    if (::nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return Status::NotPresent;
    esp_err_t err = ::nvs_set_i32(h, key, value);
    if (err == ESP_OK) err = ::nvs_commit(h);
    ::nvs_close(h);
    return err == ESP_OK ? Status::Ok : Status::Failed;
}

}  // namespace store

Status init() noexcept {
    port::set_clock(&clock_::micros);  // core/ owns no clock of its own (§2)
    CLK_LOGI(sys, "hal: board=%s, peripherals not implemented yet (see hal/esp)",
             board::board_name());
    return Status::Ok;
}

Status reboot() noexcept {
    CLK_LOGW(sys, "reboot: esp_restart()");
    // Flush the console so the last line makes it out of the UART before the reset.  The
    // coils are left as they are on purpose: STEP_STBY falls with the rail, and holding a
    // stepper energised through a reset is how you cook a driver.
    ::vTaskDelay(pdMS_TO_TICKS(80));
    ::esp_restart();
    return Status::Failed;  // esp_restart() does not return
}

}  // namespace clk::hal
