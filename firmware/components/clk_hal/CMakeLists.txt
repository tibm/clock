# api/ declares, esp/ and host/ define.  CMake picks -- never an #ifdef in a driver.
#
# Directory is clk_hal/, not hal/: ESP-IDF ships its own `hal` component and IDF component
# names are a single flat namespace, so `hal/` here silently shadows hal/gpio_types.h and
# every IDF header that includes it.  The C++ include path stays clk/hal/hal.hpp.
# [FIRMWARE.md §2, D14]
if(ESP_PLATFORM)
    idf_component_register(SRCS esp/src/hal_esp.cpp
                           INCLUDE_DIRS api
                           REQUIRES core board_cfg esp_timer nvs_flash)
else()
    find_package(Threads REQUIRED)
    add_library(clk_hal STATIC host/src/hal_host.cpp)
    # host/include is public so cmd_sim.cpp can reach the fake-control surface.  Nothing
    # in services/ or drivers/ may include it -- if they could, they could detect the
    # fakes, and D14 would stop being true.
    target_include_directories(clk_hal PUBLIC api host/include)
    target_link_libraries(clk_hal PUBLIC clk_core clk_board_cfg Threads::Threads)
endif()
