# Host unit tests.  [FIRMWARE.md §11.1]
#
# GoogleTest arrives as a pinned submodule (toolchain.lock -> vendored.googletest).  Until
# `git submodule update --init vendor/googletest` has been run, these build against a tiny
# assert harness so the tree stays green with no network.  The test bodies do not change.
set(CLK_GTEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/googletest)

add_executable(test_host test_main.cpp test_log.cpp test_cli.cpp)
target_link_libraries(test_host PRIVATE clk_cli clk_command clk_core)
target_include_directories(test_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(EXISTS ${CLK_GTEST_DIR}/CMakeLists.txt)
    message(STATUS "host tests: GoogleTest from vendor/")
    add_subdirectory(${CLK_GTEST_DIR} ${CMAKE_CURRENT_BINARY_DIR}/gtest EXCLUDE_FROM_ALL)
    target_link_libraries(test_host PRIVATE GTest::gtest_main)
    target_compile_definitions(test_host PRIVATE CLK_HAVE_GTEST=1)
else()
    message(STATUS "host tests: minimal harness (run `git submodule update --init vendor/googletest` for GoogleTest)")
endif()

add_test(NAME host COMMAND test_host)
