// ESP32-S3 HAL.                                            [FIRMWARE.md §2, §12.0]
//
// STATUS, 2026-09-13: `clock_`, `adc`, `knob`, `i2c`, `pixels`, `motor`, `expander` and the
// three sensor-board drivers are real.  `hal::power` moved to shared/power.cpp -- it is
// arithmetic over the ADC and the expander and had nothing platform-specific left in it.
//
// `hal::audio` moved to esp/src/audio_esp.cpp on 2026-09-13 -- it is the only peripheral that
// owns a task, and the amp's own register set is shared/tas5760m.cpp.
//
// ONE namespace here is still an honest NotPresent stub, gated on the 12 V boost rather than
// on anything in this file (FIRMWARE.md §12.2 Phase 4):
//   wake  -> ledc, ~1 kHz, gamma applied above this layer
//
// A stub is not a placeholder apology.  On BOARD=devkit it is the *correct* answer until you
// wire something up (board_cfg starts the devkit with an empty presence mask), and D16 says
// absence is a first-class result, never a faked success and never an error log.  Each
// peripheral is independently testable the moment its part is on the breadboard, which is
// exactly why the presence mask is per-device rather than per-board.
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/pulse_cnt.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/rtc.h"

#include "clk/board.hpp"
#include "clk/hal/bme688.hpp"
#include "clk/hal/bno085.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/mcp23017.hpp"
#include "clk/hal/tsl2591.hpp"
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

// ============================ shared GPIO plumbing ========================================
namespace {

// Three pins on this board want an edge interrupt -- `SENSOR_INT` (IO42), `ENC_SW` (IO17) and
// eventually `EXPANDER_INT` (IO44) -- and IDF's per-pin ISR dispatch is installed once for the
// whole port.  Whoever gets there first installs it; ESP_ERR_INVALID_STATE means somebody
// already did, which is success and not a failure.
bool ensure_gpio_isr_service() noexcept {
    static bool ok = false;
    if (ok) return true;
    const esp_err_t e = ::gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    ok = (e == ESP_OK || e == ESP_ERR_INVALID_STATE);
    if (!ok) CLK_LOGE(sys, "gpio isr service: %s", ::esp_err_to_name(e));
    return ok;
}

}  // namespace

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

// ============================ hal::knob ==================================================
// PCNT unit 0 in x4 quadrature, plus `ENC_SW` as a debounced GPIO interrupt.  Two mechanisms
// for two very different signals, and the split is the hardware's: A/B are a continuous
// position that hardware can count without us, while the push is a single event that must not
// be missed however briefly it happens (README §12 -- press is the entire mode UI).
namespace knob {
namespace {

// The EM14 is an OPTICAL encoder: 64 CPR, x4 on the quadrature decoder, 256 counts/rev, and
// no contact to bounce (§6.6d).  It also has no detent, so four counts is the notch a detented
// knob would have had -- that unit lives in `ui`, not here; the HAL counts edges.
constexpr int kHigh = 0x7FFF;  // the S3's counter is 16-bit; accum_count carries the rest
constexpr int kLow = -0x7FFF;

// 1 us rejects nothing a finger can produce -- 120 rpm at 64 CPR x4 is 512 edges a second,
// two milliseconds apart -- while killing the ringing a 5 V edge induces on its neighbour.
//
// ⚠ Bench risk worth knowing before you blame the firmware: A and B arrive through 100k/200k
// dividers (README §12), so the source impedance at the pin is ~67 k and stray capacitance
// turns each edge into a microsecond-scale ramp.  A slow ramp through a CMOS threshold is
// where a quadrature decoder invents counts.  The symptom is specific: counts moving while
// the knob is STILL, or a single detent reporting more than four.  If that happens the fix is
// the divider (10k/20k draws 0.24 mA at 5 V and is 10x stiffer), not this number -- raising
// the filter far enough to hide it would start swallowing real edges at speed.
constexpr uint32_t kGlitchNs = 1000;

// ENC_SW is a dry contact to GND with a 10 k pull-up and 100 nF on the board, so the pin is
// already RC-filtered to about a millisecond; 5 ms on top is §6.6's figure and covers the
// tail of it.  Applied as a LOCKOUT after the first falling edge rather than as a settling
// wait, because the first edge is the real one and everything within 5 ms of it is the same
// press arriving twice.
constexpr int64_t kDebounceUs = 5000;

pcnt_unit_handle_t g_unit = nullptr;
bool g_failed = false;
int32_t g_last_read = 0;
// Has ENC_SW ever been observed OPEN?  Until it has, a low reading is not a press.
//
// A person cannot be holding the knob before the firmware starts polling it, so a switch that
// reads closed from the very first read is a wiring fault, not input -- and believing it costs
// the whole UI: `ui` starts its hold timer on the first `sw`, and ten seconds later the clock
// is in BLE pairing mode with nobody having touched it.  That is what the bench saw on
// 2026-09-10.  Refusing to report a press until the pin has been seen idle turns a dead
// harness into one warning and a knob that still rotates, instead of a product stuck in a mode
// it cannot be talked out of.
bool g_sw_seen_open = false;
bool g_sw_warned = false;

// Written by the ISR, read by a task.  `g_latched` is the whole reason the press is an
// interrupt and not a poll: a closure shorter than the caller's poll period would otherwise
// not exist, and `ui` ticks at 20 ms while a quick click is easily under that.
volatile bool g_latched = false;
volatile int64_t g_last_edge_us = 0;

void IRAM_ATTR sw_isr(void*) noexcept {
    const int64_t now = ::esp_timer_get_time();
    if (now - g_last_edge_us < kDebounceUs) return;  // still inside the same press
    g_last_edge_us = now;
    if (::gpio_get_level(static_cast<gpio_num_t>(board::kPins.enc_sw)) == 0) g_latched = true;
}

bool init_sw() noexcept {
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << board::kPins.enc_sw;
    cfg.mode = GPIO_MODE_INPUT;
    // R113 is the 10 k pull-up on the board.  The internal one is in parallel with it and
    // harmless, and it is what defines the pin while J10 is unplugged -- which is most of
    // bring-up, and a floating input on an edge interrupt is an interrupt storm.
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.intr_type = GPIO_INTR_ANYEDGE;
    if (const esp_err_t e = ::gpio_config(&cfg); e != ESP_OK) {
        CLK_LOGE(drv_knob, "ENC_SW config: %s", ::esp_err_to_name(e));
        return false;
    }
    if (!ensure_gpio_isr_service()) return false;
    return ::gpio_isr_handler_add(static_cast<gpio_num_t>(board::kPins.enc_sw), &sw_isr, nullptr) ==
           ESP_OK;
}

bool ready() noexcept {
    if (g_unit || g_failed) return g_unit != nullptr;
    g_failed = true;  // cleared only by reaching the end

    pcnt_unit_config_t ucfg{};
    ucfg.low_limit = kLow;
    ucfg.high_limit = kHigh;
    // Without this the count wraps at +-32767 and a knob turned 128 revolutions one way
    // reports the far end of its range.  With it, IDF accumulates across the watch points
    // below and `get_count` answers a 32-bit total -- which is what `ui` diffs.
    ucfg.flags.accum_count = true;
    if (const esp_err_t e = ::pcnt_new_unit(&ucfg, &g_unit); e != ESP_OK) {
        g_unit = nullptr;
        CLK_LOGE(drv_knob, "pcnt unit: %s", ::esp_err_to_name(e));
        return false;
    }

    pcnt_glitch_filter_config_t filt{};
    filt.max_glitch_ns = kGlitchNs;
    (void)::pcnt_unit_set_glitch_filter(g_unit, &filt);

    // x4 decoding: two channels, each watching one line's edges while reading the other's
    // level.  Every one of the four transitions in a quadrature cycle therefore counts, which
    // is where 64 CPR becomes 256 counts a revolution.
    pcnt_channel_handle_t ca = nullptr, cb = nullptr;
    pcnt_chan_config_t cfg_a{};
    cfg_a.edge_gpio_num = board::kPins.enc_a;
    cfg_a.level_gpio_num = board::kPins.enc_b;
    pcnt_chan_config_t cfg_b{};
    cfg_b.edge_gpio_num = board::kPins.enc_b;
    cfg_b.level_gpio_num = board::kPins.enc_a;
    if (::pcnt_new_channel(g_unit, &cfg_a, &ca) != ESP_OK ||
        ::pcnt_new_channel(g_unit, &cfg_b, &cb) != ESP_OK) {
        CLK_LOGE(drv_knob, "pcnt channels");
        return false;
    }

    // ⚠ THE SIGN LIVES HERE, and clockwise must come out POSITIVE: §6.6c gives the knob its
    // meaning by direction -- clockwise arms the alarm, anticlockwise disarms it -- so a
    // backwards encoder is not a cosmetic bug, it is a clock that disarms when you meant to
    // arm.  The EM14 datasheet says channel A leads B clockwise; if the bench disagrees
    // (`sensor knob stream 20`, turn it clockwise, watch the sign) swap the two edge actions
    // on channel A and nothing else.
    (void)::pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                         PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    (void)::pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                          PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    (void)::pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                         PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    (void)::pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                          PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    // accum_count only accumulates AT watch points, so the limits have to be watched or the
    // flag above does nothing at all.
    (void)::pcnt_unit_add_watch_point(g_unit, kLow);
    (void)::pcnt_unit_add_watch_point(g_unit, kHigh);

    if (::pcnt_unit_enable(g_unit) != ESP_OK || ::pcnt_unit_clear_count(g_unit) != ESP_OK ||
        ::pcnt_unit_start(g_unit) != ESP_OK) {
        CLK_LOGE(drv_knob, "pcnt start");
        return false;
    }

    // A knob whose switch would not arm still counts, and counting is most of what the knob
    // is for -- so this degrades rather than failing the unit.
    if (!init_sw()) CLK_LOGW(drv_knob, "ENC_SW interrupt unavailable; rotation still works");

    g_failed = false;
    CLK_LOGI(drv_knob, "PCNT unit up on IO%d/IO%d, ENC_SW on IO%d", board::kPins.enc_a,
             board::kPins.enc_b, board::kPins.enc_sw);
    return true;
}

}  // namespace

Result<State> read() noexcept {
    if (!board::present(board::Dev::Knob)) return Result<State>::bad(Status::NotPresent);
    if (!ready()) return Result<State>::bad(Status::NotPresent);

    int raw = 0;
    if (::pcnt_unit_get_count(g_unit, &raw) != ESP_OK) return Result<State>::bad(Status::Failed);

    State s{};
    s.count = raw;
    s.delta = s.count - g_last_read;
    g_last_read = s.count;

    // The pin is the truth about what is happening NOW; the latch is the truth about what
    // happened while nobody was looking.  A press still held reads down and consumes the
    // latch; one already released is reported down exactly once and then lets go.  This is
    // deliberately the same behaviour clocksim's fake implements, so a quick click means the
    // same thing on a laptop and on the bench.
    // PCNT reads these pads through the GPIO matrix, which leaves the input buffer enabled,
    // so the levels are still readable here -- and on a bench they are the whole diagnosis.
    s.a_raw = ::gpio_get_level(static_cast<gpio_num_t>(board::kPins.enc_a)) != 0;
    s.b_raw = ::gpio_get_level(static_cast<gpio_num_t>(board::kPins.enc_b)) != 0;
    const bool low = ::gpio_get_level(static_cast<gpio_num_t>(board::kPins.enc_sw)) == 0;
    s.sw_raw = low;
    if (!low) g_sw_seen_open = true;
    const bool latched = g_latched;
    g_latched = false;
    if (!g_sw_seen_open) {
        // Stuck closed since boot.  Say so once -- at Warn, so it survives the drivers'
        // default quiet -- and report the knob as un-pressed so rotation stays usable.
        if (!g_sw_warned) {
            g_sw_warned = true;
            CLK_LOGW(drv_knob,
                     "ENC_SW reads closed at boot (IO%d low) -- ignoring the press "
                     "until it opens; check J10.5 against the EM14's switch pins",
                     board::kPins.enc_sw);
        }
        s.sw = false;
        return Result<State>::good(s);
    }
    s.sw = low || latched;
    return Result<State>::good(s);
}

}  // namespace knob

// hal::motor is real and lives in esp/src/motor_esp.cpp -- it is the one peripheral on this
// side with an interrupt in the signal path, and it earns a file of its own.

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

// Four is every device this board can hold at once (expander, amp, and two of the three on
// the daughterboard in any one conversation).  A miss evicts the oldest, which on a bus this
// small never happens twice in a row.
struct Slot {
    uint8_t addr;
    i2c_master_dev_handle_t h;
};
Slot g_dev[4]{};
std::size_t g_next = 0;

// A pure accessor.  It used to install the bus lazily on first use, and on a real boot that
// was a RACE: `motion` homes and `ui` polls the knob within microseconds of each other, both
// found g_bus null, and both called i2c_new_master_bus() on port 0.  The loser got
//
//     E i2c.common: I2C bus id(0) has already been acquired
//     E i2c.master: i2c_new_master_bus(1058): I2C bus acquire failed
//
// and -- worse than the noise -- set the old `g_bus_failed` latch, which would have killed
// every I2C device on the board for the rest of the boot.  What actually happened on the
// bench was subtler and more confusing: the expander write inside motor::enable() failed, so
// `motion` concluded "no movement fitted" and skipped homing on a board whose movement was
// soldered on.  (2026-09-10.)
//
// Installing it in hal::init() instead removes the race rather than locking around it:
// app_main calls hal::init() before it constructs a single active object, so there is exactly
// one caller and it is single-threaded.
i2c_master_bus_handle_t bus() noexcept { return g_bus; }

bool install_bus() noexcept {
    if (g_bus) return true;
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
        CLK_LOGE(drv_exp, "i2c bus install failed: %s", ::esp_err_to_name(err));
        return false;
    }
    return true;
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

// Called once by hal::init(), before any active object exists.  See bus() for why that is
// the only safe time to do it.
bool install() noexcept { return install_bus(); }

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

Status write_read(uint8_t addr, const uint8_t* w, std::size_t wn, uint8_t* r,
                  std::size_t rn) noexcept {
    if ((wn && !w) || (rn && !r)) return Status::BadArg;
    if (!wn && !rn) return Status::BadArg;  // a bare address probe belongs in scan()
    auto* h = dev(addr);
    if (!h) return Status::NotPresent;
    // Three IDF calls, not one: transmit_receive() requires both halves, and the BNO085
    // needs each half on its own -- a header read with nothing written, a packet write with
    // nothing read.  Splitting here keeps that a normal case rather than a special one.
    if (wn && rn) return err_to_status(::i2c_master_transmit_receive(h, w, wn, r, rn, kTimeoutMs));
    if (wn) return err_to_status(::i2c_master_transmit(h, w, wn, kTimeoutMs));
    return err_to_status(::i2c_master_receive(h, r, rn, kTimeoutMs));
}

Status write(uint8_t addr, const uint8_t* buf, std::size_t n) noexcept {
    return write_read(addr, buf, n, nullptr, 0);
}

Status read(uint8_t addr, uint8_t* buf, std::size_t n) noexcept {
    return write_read(addr, nullptr, 0, buf, n);
}

Result<uint8_t> read_reg(uint8_t addr, uint8_t reg) noexcept {
    uint8_t v = 0;
    const Status st = write_read(addr, &reg, 1, &v, 1);
    return st == Status::Ok ? Result<uint8_t>::good(v) : Result<uint8_t>::bad(st);
}

// One transaction, not n.  Auto-increment is the device's, and the reason to use it is not
// speed: a burst that the chip advances internally cannot be interleaved with anything, so
// a 16-bit ADC pair belongs to one integration and a calibration block to one power-up.
Status read_regs(uint8_t addr, uint8_t reg, uint8_t* out, std::size_t n) noexcept {
    return write_read(addr, &reg, 1, out, n);
}

Status write_reg(uint8_t addr, uint8_t reg, uint8_t val) noexcept {
    const uint8_t buf[2] = {reg, val};
    return write(addr, buf, sizeof buf);
}

}  // namespace i2c

// ============================ hal::imu / als / env ========================================
// Three drivers, three forwards.  The device knowledge is in shared/, compiled into both
// backends; nothing here knows a register, an SHTP channel or a compensation coefficient.
namespace imu {
Result<State> read() noexcept { return bno085::read(); }
Link link() noexcept { return bno085::link(); }
}  // namespace imu

namespace als {
Result<State> read() noexcept { return tsl2591::read(); }
}  // namespace als

namespace env {
Result<State> read() noexcept { return bme688::read(); }
}  // namespace env

// The named-signal surface is the MCP23017 driver now (shared/mcp23017.cpp), which reaches
// the chip through hal::i2c above.  Nothing here knows a register.
namespace expander {
Result<bool> get(Sig s) noexcept { return mcp23017::get(s); }
Status set(Sig s, bool level) noexcept { return mcp23017::set(s, level); }
}  // namespace expander

// hal::audio is not here either: it is the one peripheral in the HAL that owns a task, so it
// lives next to the reason for that in esp/src/audio_esp.cpp.  The chip half is
// shared/tas5760m.cpp, compiled into both backends.

// hal::power is not here: it is shared/power.cpp, compiled into both backends.  Everything it
// needs is above -- the ADC (which owns the `VBAT_DIV_EN` leg) and the expander -- so there was
// nothing platform-specific left to put on this side.

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

namespace i2c {
bool install() noexcept;
}  // namespace i2c

namespace {

// SENSOR_INT (IO42) is the BNO085's `H_INTN`: active-low, push-pull, and it means "the hub
// has an SHTP packet waiting", not "a tap happened" (§6.5.1 item 2).  The handler counts the
// edge and stops there; draining is bno085::read()'s job, on a task, where a device that
// clock-stretches is allowed to cost milliseconds.
//
// Counting it at all is what splits one bench symptom into two questions.  "No taps ever
// arrive" with edges counting means the driver is not parsing what the hub sent; with the
// count stuck at zero it means the hub never spoke, which is R-BOARD-3's failure and a
// different afternoon entirely.
void IRAM_ATTR sensor_int_isr(void*) noexcept { bno085::isr_tick(); }

void install_sensor_int() noexcept {
    if (!board::present(board::Dev::Imu)) return;
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << board::kPins.sensor_int;
    cfg.mode = GPIO_MODE_INPUT;
    // The hub drives this push-pull, so the pull-up is not electrically required.  It is here
    // for the case the daughterboard is NOT plugged in, which is most of bring-up: an
    // undriven IO42 would otherwise float and hand us an interrupt storm to explain.
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.intr_type = GPIO_INTR_NEGEDGE;
    if (const esp_err_t e = ::gpio_config(&cfg); e != ESP_OK) {
        CLK_LOGW(drv_imu, "SENSOR_INT config: %s", ::esp_err_to_name(e));
        return;
    }
    if (!ensure_gpio_isr_service()) return;
    (void)::gpio_isr_handler_add(static_cast<gpio_num_t>(board::kPins.sensor_int), &sensor_int_isr,
                                 nullptr);
}

}  // namespace

Status init() noexcept {
    port::set_clock(&clock_::micros);  // core/ owns no clock of its own (§2)

    // Two IDF log lines that are noise on THIS board, silenced by tag rather than by turning
    // the whole component down.  Both were chased on the bench before being understood, so
    // both get a reason rather than a suppression:
    //
    //   i2c.master "check pull-up resistances" is printed unconditionally whenever internal
    //     pull-ups are disabled (i2c_master.c:1067) -- a blanket reminder, not a measurement.
    //     We disable them deliberately because R95/R96 are fitted (§12.0.4).
    //   led_strip_spi "Only support WS2812" fires for any other led_model on the SPI backend.
    //     The SK6812 timing was verified on rev0.3 on 2026-09-08: the dial pixels light and
    //     the colours are true, which is what that warning is guessing about (§12.0.5).
    ::esp_log_level_set("i2c.master", ESP_LOG_ERROR);
    ::esp_log_level_set("led_strip_spi", ESP_LOG_ERROR);

    // The bus BEFORE the active objects.  hal::init() is the last single-threaded moment in
    // the boot, and i2c::bus() explains why that matters.
    if (!i2c::install()) CLK_LOGE(sys, "i2c bus unavailable -- expander, amp and J7 are dark");

    install_sensor_int();
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
