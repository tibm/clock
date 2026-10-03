#include "check.hpp"

#include "clk/hal/hal.hpp"

void run_log_tests();
void run_journal_tests();
void run_evtrace_tests();
void run_cli_tests();
void run_sim_tests();
void run_motor_tests();
void run_anim_tests();
void run_level_tests();
void run_alarm_tests();
void run_motion_service_tests();
void run_tsl2591_tests();
void run_bme688_tests();
void run_bno085_tests();
void run_audio_tests();
void run_net_tests();
void run_storage_tests();
void run_wifi_tests();
void run_history_tests();

int main() {
    // Same first move as app_main and clocksim.  It is also what hands core/ its clock --
    // without it port::now_us() is 0 forever and no active object ever ticks.
    clk::hal::init();

    run_log_tests();
    // The debug journal's ring (§9.4a) -- before any AO, so this thread is its only writer.
    run_journal_tests();
    // The event tracer's ring (§6.9) -- likewise before any AO dispatches into it.
    run_evtrace_tests();
    run_cli_tests();
    run_sim_tests();
    run_motor_tests();
    // The light engine and the room's dimmer (§6.6a, §6.6g) -- pure.
    run_anim_tests();
    run_level_tests();
    // The weekly alarm and the knob's one-off (§6.6f) -- pure, a week in a microsecond.
    run_alarm_tests();
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
    // Time zones and SNTP packets -- the pure halves of Wi-Fi time (§6.4, §6.7).
    run_wifi_tests();
    // The history log's record + files + budget (§6.3a).
    run_history_tests();
    // Last: these start the active objects, and an AO thread outlives the test that woke it.
    run_motion_service_tests();
    return check_summary("host");
}
