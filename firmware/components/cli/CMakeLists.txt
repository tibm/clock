# rule 10: esp_console_cmd_register is called in console_esp.cpp and nowhere else.
set(CLK_CLI_SRCS src/registry.cpp src/cmd_sys.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CLK_CLI_SRCS} src/console_esp.cpp
                           INCLUDE_DIRS include
                           REQUIRES core command console esp_driver_usb_serial_jtag esp_timer)
else()
    add_library(clk_cli STATIC ${CLK_CLI_SRCS} src/console_host.cpp)
    target_include_directories(clk_cli PUBLIC include)
    target_link_libraries(clk_cli PUBLIC clk_command clk_core)
endif()
