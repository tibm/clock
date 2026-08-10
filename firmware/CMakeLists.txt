# Host build: unit tests + clocksim.  The target build entry point is apps/clock/.
# [FIRMWARE.md §1.2, §11]
cmake_minimum_required(VERSION 3.22)
project(clock_fw_host CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)          # gnu++23, matching the IDF build

add_compile_options(-Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
add_compile_definitions(CLK_BOARD="host" CLK_SDK="host")

add_subdirectory(components/core)
add_subdirectory(components/command)
add_subdirectory(components/board_cfg)
add_subdirectory(components/clk_hal)
add_subdirectory(components/domain)
add_subdirectory(components/services)
add_subdirectory(components/cli)
add_subdirectory(apps/clocksim)

enable_testing()
add_subdirectory(test/host)
