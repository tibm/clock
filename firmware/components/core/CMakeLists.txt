# Dual-build: idf.py sets ESP_PLATFORM, the host build does not.  One file, two targets --
# so core/ cannot drift between the firmware and clocksim.  [FIRMWARE.md §2]
set(CLK_CORE_SRCS src/log.cpp src/status.cpp src/ao.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CLK_CORE_SRCS} src/log_esp.cpp src/port_esp.cpp
                           INCLUDE_DIRS include
                           REQUIRES log freertos)
else()
    find_package(Threads REQUIRED)
    add_library(clk_core STATIC ${CLK_CORE_SRCS} src/log_host.cpp src/port_host.cpp)
    target_include_directories(clk_core PUBLIC include)
    target_link_libraries(clk_core PUBLIC Threads::Threads)
endif()
