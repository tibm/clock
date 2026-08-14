#include "check.hpp"

#include "clk/hal/hal.hpp"

void run_log_tests();
void run_cli_tests();
void run_sim_tests();
void run_motor_tests();
void run_anim_tests();
void run_motion_service_tests();

int main() {
    // Same first move as app_main and clocksim.  It is also what hands core/ its clock --
    // without it port::now_us() is 0 forever and no active object ever ticks.
    clk::hal::init();

    run_log_tests();
    run_cli_tests();
    run_sim_tests();
    run_motor_tests();
    // Last: these start the active objects, and an AO thread outlives the test that woke it.
    run_motion_service_tests();
    return check_summary("host");
}
