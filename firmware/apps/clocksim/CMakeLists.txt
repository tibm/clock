# D14: the real services + fake HAL + the same CLI, as a native binary.
add_executable(clocksim main.cpp)
target_link_libraries(clocksim PRIVATE clk_cli)   # clk_cli PUBLIC-links the rest
target_compile_definitions(clocksim PRIVATE CLK_PROFILE="${CLK_PROFILE}")
