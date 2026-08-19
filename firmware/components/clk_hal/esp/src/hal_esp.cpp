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
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"
#include "clk/port.hpp"

namespace clk::hal {

namespace clock_ {

uint64_t micros() noexcept { return static_cast<uint64_t>(esp_timer_get_time()); }
uint32_t millis() noexcept { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void sleep_ms(uint32_t ms) noexcept { vTaskDelay(pdMS_TO_TICKS(ms)); }

}  // namespace clock_

namespace adc {
Result<uint16_t> read_mv(Ch) noexcept { return Result<uint16_t>::bad(Status::NotPresent); }
Result<float> read_opto_norm() noexcept { return Result<float>::bad(Status::NotPresent); }
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

namespace pixels {
Status set(std::size_t, Rgbw) noexcept { return Status::NotPresent; }
Status set_all(Rgbw) noexcept { return Status::NotPresent; }
Status refresh() noexcept { return Status::NotPresent; }
Rgbw get(std::size_t) noexcept { return Rgbw{}; }
}  // namespace pixels

namespace wake {
Status set(uint8_t, uint8_t) noexcept { return Status::NotPresent; }
uint8_t warm() noexcept { return 0; }
uint8_t cool() noexcept { return 0; }
}  // namespace wake

namespace i2c {
Result<std::size_t> scan(uint8_t*, std::size_t) noexcept {
    return Result<std::size_t>::bad(Status::NotPresent);
}
Result<uint8_t> read_reg(uint8_t, uint8_t) noexcept {
    return Result<uint8_t>::bad(Status::NotPresent);
}
Status write_reg(uint8_t, uint8_t, uint8_t) noexcept { return Status::NotPresent; }
}  // namespace i2c

namespace imu {
Result<State> read() noexcept { return Result<State>::bad(Status::NotPresent); }
}  // namespace imu

namespace expander {
Result<bool> get(Sig) noexcept { return Result<bool>::bad(Status::NotPresent); }
Status set(Sig, bool) noexcept { return Status::NotPresent; }
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
