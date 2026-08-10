# rule 10: esp_console_cmd_register is called in console_esp.cpp and nowhere else.
set(CLK_CLI_SRCS
    src/registry.cpp
    src/cmd_sys.cpp
    src/cmd_sensor.cpp
    src/cmd_ui.cpp
    src/stream.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CLK_CLI_SRCS} src/console_esp.cpp
                           INCLUDE_DIRS include
                           REQUIRES core command board_cfg clk_hal
                                    console esp_driver_usb_serial_jtag esp_timer pthread)
else()
    add_library(clk_cli STATIC ${CLK_CLI_SRCS} src/console_host.cpp src/cmd_sim.cpp)
    target_include_directories(clk_cli PUBLIC include)
    target_link_libraries(clk_cli PUBLIC clk_command clk_core clk_board_cfg clk_hal)
    target_compile_definitions(clk_cli PUBLIC CLK_HAVE_SIM=1)
endif()
