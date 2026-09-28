#include "check.hpp"

#include "clk/hal/hal.hpp"

void run_log_tests();
void run_cli_tests();
void run_sim_tests();
void run_motor_tests();
void run_anim_tests();
void run_level_tests();
void run_motion_service_tests();
void run_tsl2591_tests();
void run_bme688_tests();
void run_bno085_tests();
void run_audio_tests();
void run_net_tests();
void run_storage_tests();

int main() {
    // Same first move as app_main and clocksim.  It is also what hands core/ its clock --
    // without it port::now_us() is 0 forever and no active object ever ticks.
    clk::hal::init();

    run_log_tests();
    run_cli_tests();
    run_sim_tests();
    run_motor_tests();
    run_level_tests();
    // The three sensor-board drivers.  Two of them run against register models behind the
    // fake bus (§11.2); the BNO085's transport is target-only, so what is checked here is
    // its pure half -- header parsing, Q-point maths, the dial-axis map.
    run_tsl2591_tests();
    run_bme688_tests();
    run_bno085_tests();
    // The amp: the sine generator, the TAS5760M driver against the 0x6C register model, and
    // the ceiling that keeps a bring-up tone off the cell protector (§6.2).
    run_audio_tests();
    // The alarm's sound: the WAV check, the PSRAM ring, the mixer, the fake card (§6.3).
    run_storage_tests();
    // The app link's wire formats, byte for byte (§8).  The AO half runs with the others below.
    run_net_tests();
    // Last: these start the active objects, and an AO thread outlives the test that woke it.
    run_motion_service_tests();
    return check_summary("host");
}
