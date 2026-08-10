# D14: the real services + fake HAL + the same CLI, as a native binary.
#
# uibridge.cpp lives here rather than under components/ because apps/clock puts components/
# on EXTRA_COMPONENT_DIRS -- anything there is a component the target build can see, and the
# bridge must never be one.
find_package(Threads REQUIRED)

add_executable(clocksim main.cpp uibridge.cpp)
target_link_libraries(clocksim PRIVATE clk_cli Threads::Threads)  # clk_cli links the rest
target_compile_definitions(clocksim PRIVATE CLK_PROFILE="${CLK_PROFILE}")
