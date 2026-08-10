#include "clk/board.hpp"

#include <cstring>

namespace clk::board {
namespace {

constexpr const char* kDevNames[] = {
    "opto", "vbat", "knob", "pixels", "wake",    "expander", "amp",
    "als",  "env",  "imu",  "sd",     "xtal32k", "motor",
};
static_assert(sizeof(kDevNames) / sizeof(kDevNames[0]) == static_cast<std::size_t>(Dev::count));

constexpr uint16_t bit(Dev d) { return static_cast<uint16_t>(1u << static_cast<int>(d)); }

constexpr uint16_t kAll = bit(Dev::Opto) | bit(Dev::Vbat) | bit(Dev::Knob) | bit(Dev::Pixels) |
                          bit(Dev::WakeLed) | bit(Dev::Expander) | bit(Dev::Amp) | bit(Dev::Als) |
                          bit(Dev::Env) | bit(Dev::Imu) | bit(Dev::Sd) | bit(Dev::Xtal32k) |
                          bit(Dev::Motor);

#if defined(CONFIG_CLOCK_BOARD_REV0_3)
constexpr const char* kBoard = "rev0_3";
constexpr uint16_t kDefault = kAll;  // everything is soldered down
#elif defined(CONFIG_CLOCK_BOARD_DEVKIT)
constexpr const char* kBoard = "devkit";
// Nothing is fitted until you wire it, and the DevKitC has no 32.768 kHz crystal at all
// (FIRMWARE.md §12.0).  Starting empty is the honest default: `sensor list` then reports
// what you have actually connected, not what the schematic wishes you had.
constexpr uint16_t kDefault = 0;
#else
constexpr const char* kBoard = "host";
constexpr uint16_t kDefault = kAll;  // all faked, all answering
#endif

uint16_t g_present = kDefault;

}  // namespace

const char* name(Dev d) noexcept {
    const auto i = static_cast<int>(d);
    return (i >= 0 && i < kDevCount) ? kDevNames[i] : "?";
}

bool parse_dev(const char* s, Dev& out) noexcept {
    if (!s) return false;
    for (int i = 0; i < kDevCount; ++i) {
        if (std::strcmp(s, kDevNames[i]) == 0) {
            out = static_cast<Dev>(i);
            return true;
        }
    }
    return false;
}

bool present(Dev d) noexcept { return (g_present & bit(d)) != 0; }

void set_present(Dev d, bool on) noexcept {
    if (on)
        g_present |= bit(d);
    else
        g_present = static_cast<uint16_t>(g_present & ~bit(d));
}

void reset_presence() noexcept { g_present = kDefault; }

const char* board_name() noexcept { return kBoard; }

}  // namespace clk::board
