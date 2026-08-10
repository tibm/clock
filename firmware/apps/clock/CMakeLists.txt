# The product firmware.  Build through tools/build.sh, which composes the sdkconfig
# fragments from PROFILE x BOARD.  [FIRMWARE.md §1.2]
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(clock)

# IDF 5.5 defaults C++ to -std=gnu++2b.  Pin the canonical spelling so the target and host
# builds agree (D2, §1.3).  CXX_COMPILE_OPTIONS -- not COMPILE_OPTIONS, which would also
# hit the C files.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++23" APPEND)

# Build identity, consumed by `sys ver` and the boot banner (§9.7).
execute_process(COMMAND git describe --always --dirty --tags
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE CLK_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
string(TIMESTAMP CLK_BUILD_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
if(NOT DEFINED CLK_PROFILE)
    set(CLK_PROFILE "dev")
endif()
if(NOT DEFINED CLK_BOARD)
    set(CLK_BOARD "devkit")
endif()

idf_build_set_property(COMPILE_DEFINITIONS "CLK_GIT_SHA=\"${CLK_GIT_SHA}\""   APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "CLK_BUILD_UTC=\"${CLK_BUILD_UTC}\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "CLK_PROFILE=\"${CLK_PROFILE}\""   APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "CLK_BOARD=\"${CLK_BOARD}\""       APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "CLK_SDK=\"${IDF_VER}\""           APPEND)
