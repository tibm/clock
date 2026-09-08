# rule 10: esp_console_cmd_register is called in console_esp.cpp and nowhere else.
# NOTE on §2's "cli -> command, never services, never hal": that is the destination, and it
# arrives with command::dispatch (§5).  Until then the CLI reaches services and hal directly,
# as cmd_sensor/cmd_ui already do -- but every row is a VIEW: it posts an event to the owning
# AO and reads a snapshot.  No row drives a peripheral the AO owns (rule 1).
set(CLK_CLI_SRCS
    src/registry.cpp
    src/cmd_sys.cpp
    src/cmd_sensor.cpp
    src/cmd_ui.cpp
    src/cmd_motion.cpp
    src/cmd_chrono.cpp
    src/cmd_board.cpp
    src/stream.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CLK_CLI_SRCS} src/console_esp.cpp
                           INCLUDE_DIRS include
                           REQUIRES core command board_cfg clk_hal domain services
                                    console esp_driver_usb_serial_jtag esp_timer pthread)
else()
    add_library(clk_cli STATIC ${CLK_CLI_SRCS} src/console_host.cpp src/cmd_sim.cpp)
    target_include_directories(clk_cli PUBLIC include)
    target_link_libraries(clk_cli PUBLIC clk_command clk_core clk_board_cfg clk_hal
                                         clk_domain clk_services)
    target_compile_definitions(clk_cli PUBLIC CLK_HAVE_SIM=1)
endif()
