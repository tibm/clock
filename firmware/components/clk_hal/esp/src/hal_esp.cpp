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
// Each one is independently testable the moment its part is on the breadboard, which is
// exactly why the presence mask is per-device rather than per-board.
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

namespace clk::hal {

namespace clock_ {

uint64_t micros() noexcept { return static_cast<uint64_t>(esp_timer_get_time()); }
uint32_t millis() noexcept { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void     sleep_ms(uint32_t ms) noexcept { vTaskDelay(pdMS_TO_TICKS(ms)); }

}  // namespace clock_

namespace adc {
Result<uint16_t> read_mv(Ch) noexcept          { return Result<uint16_t>::bad(Status::NotPresent); }
Result<float>    read_opto_norm() noexcept     { return Result<float>::bad(Status::NotPresent); }
}  // namespace adc

namespace knob {
Result<State> read() noexcept                  { return Result<State>::bad(Status::NotPresent); }
}  // namespace knob

namespace pixels {
Status set(std::size_t, Rgbw) noexcept         { return Status::NotPresent; }
Status set_all(Rgbw) noexcept                  { return Status::NotPresent; }
Status refresh() noexcept                      { return Status::NotPresent; }
Rgbw   get(std::size_t) noexcept               { return Rgbw{}; }
}  // namespace pixels

namespace wake {
Status  set(uint8_t, uint8_t) noexcept         { return Status::NotPresent; }
uint8_t warm() noexcept                        { return 0; }
uint8_t cool() noexcept                        { return 0; }
}  // namespace wake

namespace i2c {
Result<std::size_t> scan(uint8_t*, std::size_t) noexcept {
    return Result<std::size_t>::bad(Status::NotPresent);
}
Result<uint8_t> read_reg(uint8_t, uint8_t) noexcept { return Result<uint8_t>::bad(Status::NotPresent); }
Status          write_reg(uint8_t, uint8_t, uint8_t) noexcept { return Status::NotPresent; }
}  // namespace i2c

namespace power {
Result<State> read() noexcept                  { return Result<State>::bad(Status::NotPresent); }
}  // namespace power

Status init() noexcept {
    CLK_LOGI(sys, "hal: board=%s, peripherals not implemented yet (see hal/esp)",
             board::board_name());
    return Status::Ok;
}

}  // namespace clk::hal
