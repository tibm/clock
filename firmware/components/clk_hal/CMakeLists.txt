# api/ declares, esp/ and host/ define.  CMake picks -- never an #ifdef in a driver.
#
# Directory is clk_hal/, not hal/: ESP-IDF ships its own `hal` component and IDF component
# names are a single flat namespace, so `hal/` here silently shadows hal/gpio_types.h and
# every IDF header that includes it.  The C++ include path stays clk/hal/hal.hpp.
# [FIRMWARE.md §2, D14]
#
# shared/ is the interesting list: platform-independent device drivers, compiled into BOTH
# backends.  On target they drive silicon; on the host they drive the register models in
# host/src/model_*.cpp behind the fake hal::i2c.  That is §11.2 -- the driver under test is
# the driver that ships -- and it is why a new I2C part adds one file to shared/ and one to
# host/src/, not two implementations of the same chip.
set(CLK_HAL_SHARED
    shared/mcp23017.cpp
    shared/tas5760m.cpp
    shared/tsl2591.cpp
    shared/bme688.cpp
    shared/bno085.cpp
    shared/power.cpp
    shared/tone.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS esp/src/hal_esp.cpp esp/src/motor_esp.cpp esp/src/audio_esp.cpp
                                esp/src/ble_esp.cpp
                                ${CLK_HAL_SHARED}
                           INCLUDE_DIRS api
                           REQUIRES core board_cfg esp_timer nvs_flash esp_driver_i2c
                                    esp_driver_gpio esp_driver_pcnt esp_driver_mcpwm
                                    esp_driver_gptimer esp_driver_i2s esp_adc
                                    espressif__led_strip bt)
else()
    find_package(Threads REQUIRED)
    add_library(clk_hal STATIC host/src/hal_host.cpp host/src/ble_host.cpp host/src/model_tsl2591.cpp
                               host/src/model_bme688.cpp host/src/model_tas5760m.cpp
                               ${CLK_HAL_SHARED})
    # host/include is public so cmd_sim.cpp can reach the fake-control surface.  Nothing
    # in services/ or drivers/ may include it -- if they could, they could detect the
    # fakes, and D14 would stop being true.
    target_include_directories(clk_hal PUBLIC api host/include)
    target_link_libraries(clk_hal PUBLIC clk_core clk_board_cfg Threads::Threads)
endif()
