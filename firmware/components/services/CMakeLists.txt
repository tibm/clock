# The active objects (§3.2).  services -> { command, domain, drivers, core, board_cfg } and
# hal only for peripherals it OWNS -- the REQUIRES graph is the enforcement, not a convention.
set(CLK_SVC_SRCS src/motion.cpp src/chrono.cpp src/ui.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CLK_SVC_SRCS}
                           INCLUDE_DIRS include
                           REQUIRES core domain board_cfg clk_hal)
else()
    add_library(clk_services STATIC ${CLK_SVC_SRCS})
    target_include_directories(clk_services PUBLIC include)
    target_link_libraries(clk_services PUBLIC clk_core clk_domain clk_board_cfg clk_hal)
endif()
