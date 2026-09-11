// X40.879 commutation: 2x TB6612FNG, 2x MCPWM, one GPTimer ISR.   [FIRMWARE.md D5, §6.1]
//
// Split out of hal_esp.cpp because it is the one peripheral here with an interrupt in the
// signal path and a state machine underneath it; everything else in that file is a function
// call to IDF.  The trajectory planner is deliberately NOT here -- §6.1 keeps the trapezoid in
// `motion`, above the HAL and identical on both platforms, because that is the part that
// carries the bugs.  What lives below this line is only: turn a velocity into coil currents,
// and stop exactly where you were told to.
//
// ⚠ NOTHING HERE HAS TURNED A ROTOR YET.  `M1` was soldered on 2026-09-09 and this is written
// against esp32.md and the X27 base spec, not against a movement that has moved.  The two
// numbers most likely to be wrong are the microstep count per revolution (§13 open question 1)
// and the coil phase order, and both are one constant each -- see `kUstepsPerRev` in hal.hpp
// and `kSwapB` below.
#include "driver/gptimer.h"
#include "driver/mcpwm_prelude.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

namespace clk::hal::motor {
namespace {

// ---- the shape of the drive --------------------------------------------------------------
//
// PWM-on-IN, which is the scheme esp32.md wires for: PWMA and PWMB are strapped HIGH at the
// TB6612 and the four AIN/BIN inputs carry the modulation.  So each coil is a pair of pins and
// the SIGN of the current is which one of the pair is modulating:
//
//     current > 0   IN1 = PWM(|i|)   IN2 = low
//     current < 0   IN1 = low        IN2 = PWM(|i|)
//
// The coil does the rest.  An X27-family winding is ~260 Ω and tens of millihenries, so at
// 25 kHz the electrical time constant is several carrier periods long and the coil integrates
// the PWM into a clean average current -- the carrier is a voltage DAC, not a chopper, and
// there is no current-sense loop to close.  At 5 V that is ~19 mA at full duty.
constexpr uint32_t kCarrierHz = 25'000;    // D5.  Above hearing, which is the point (§6.1)
constexpr uint32_t kMcpwmHz = 10'000'000;  // 100 ns resolution
constexpr uint32_t kPeriodTicks = kMcpwmHz / kCarrierHz;  // 400
static_assert(kPeriodTicks * kCarrierHz == kMcpwmHz, "carrier must divide the MCPWM clock");

// D5: the commutation rate is decoupled from the carrier so the two can be tuned apart.  At
// 20 kHz and x16 microstepping the ceiling is one microstep per tick = 20000 usteps/s, which
// is 1.16 rev/s -- ample for time-set, and the hands are stationary >99 % of the time anyway.
constexpr uint32_t kTickHz = 20'000;
constexpr uint32_t kGptimerHz = 1'000'000;
constexpr uint64_t kAlarmTicks = kGptimerHz / kTickHz;  // 50

// One electrical cycle of a bipolar stepper is four full steps; at x16 that is 64 microsteps.
// The Q16.16 accumulator therefore wraps its electrical angle every 64 << 16 counts, and the
// top eight bits of the remainder index a 256-point sine.
constexpr int kUstepsPerElecCycle = 64;
constexpr uint32_t kElecMask = (kUstepsPerElecCycle << 16) - 1;  // 0x3FFFFF
constexpr int kElecShift = 14;  // 22 bits of cycle -> 8 bits of index

// ⚠ If the hands run backwards on the bench, flip this and nothing else.  Reversing one coil's
// phase reverses the rotor, and it is a wiring fact rather than a firmware one: esp32.md says
// each coil's polarity and phase order are "firmware-trimmable -- just wire consistently".
constexpr bool kSwapB = false;

// ---- the LUT -----------------------------------------------------------------------------
// Taylor rather than std::sin, because std::sin is not a constant expression in standard C++
// and leaning on the GCC builtin would make this file compile on exactly one toolchain.  Nine
// terms over [0, pi/2] is good to about 1e-10, and we keep fifteen bits of it.
constexpr double kPi = 3.14159265358979323846;

constexpr double csin(double x) noexcept {
    double term = x, sum = x;
    for (int n = 1; n <= 9; ++n) {
        term *= -x * x / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

// 65 entries, not 64: the quarter has to include its endpoint or sin(pi/2) becomes a special
// case in an ISR, and 130 bytes is not worth an extra branch on the hot path.
struct Quarter {
    uint16_t v[65];
};
constexpr Quarter make_quarter() noexcept {
    Quarter q{};
    for (int i = 0; i <= 64; ++i) {
        q.v[i] = static_cast<uint16_t>(csin(kPi * static_cast<double>(i) / 128.0) * 32767.0 + 0.5);
    }
    return q;
}
constexpr Quarter kQ = make_quarter();
static_assert(kQ.v[0] == 0 && kQ.v[64] == 32767, "quarter LUT endpoints");

// sin(2*pi*i/256) in Q15, by symmetry off the quarter.
constexpr int32_t sin256(uint8_t i) noexcept {
    const uint8_t k = i & 63;
    switch (i >> 6) {
        case 0:
            return kQ.v[k];
        case 1:
            return kQ.v[64 - k];
        case 2:
            return -static_cast<int32_t>(kQ.v[k]);
        default:
            return -static_cast<int32_t>(kQ.v[64 - k]);
    }
}
constexpr int32_t cos256(uint8_t i) noexcept { return sin256(static_cast<uint8_t>(i + 64)); }

// ---- per-axis state ----------------------------------------------------------------------
struct Axis_ {
    mcpwm_cmpr_handle_t cmp[4]{};  // A+, A-, B+, B-, matching board::kPins order
    // Two accumulators that normally move together and are allowed to diverge exactly once:
    // adopt() renames the coordinate without moving the hand, so `pos` jumps and `elec` must
    // not -- the rotor is where it is, and a phase step here would be a physical lurch.
    int64_t pos = 0;   // Q16.16 microsteps, unwrapped
    int64_t elec = 0;  // Q16.16 microsteps of electrical angle, free-running
    int64_t stop = 0;  // Q16.16, absolute
    int32_t vel = 0;   // microsteps/s, signed, + = clockwise
    int32_t inc = 0;   // Q16.16 microsteps per tick, derived from vel
    bool moving = false;
};

Axis_ g_ax[2];
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

mcpwm_timer_handle_t g_pwm_timer[2]{};
gptimer_handle_t g_tick = nullptr;
bool g_built = false;
bool g_build_failed = false;
bool g_enabled = false;

int idx_of(Hand h) noexcept { return h == Hand::Hour ? 0 : 1; }

// ---- the ISR -----------------------------------------------------------------------------
// Runs at 20 kHz whenever the coils are live.  Everything it touches is either owned by it or
// under the spinlock; it allocates nothing, logs nothing and takes no other lock.
//
// Not marked IRAM_ATTR, and that is a deliberate bring-up choice rather than an oversight:
// mcpwm_comparator_set_compare_value() lives in flash unless CONFIG_MCPWM_CTRL_FUNC_IN_IRAM is
// set, so an IRAM ISR could not call it and would have to poke registers through the private
// HAL. The cost of not being in IRAM is that a flash write (OTA, NVS commit) stalls
// commutation for as long as the cache is disabled.  The hands would twitch; nothing breaks.
// Revisit when §6.4's OTA lands, which is the first thing that writes flash while moving.
bool step_axis(Axis_& a) noexcept {
    if (a.vel == 0) return false;
    a.pos += a.inc;
    a.elec += a.inc;
    // The increment is at most one microstep per tick, so this lands on the target rather
    // than sailing past it -- the clamp is exact, not a correction.
    if ((a.inc > 0 && a.pos >= a.stop) || (a.inc < 0 && a.pos <= a.stop)) {
        a.elec += a.stop - a.pos;  // keep the phase consistent with where we actually stopped
        a.pos = a.stop;
        a.vel = 0;
        a.inc = 0;
        a.moving = false;
    }
    return true;
}

void write_coils(Axis_& a) noexcept {
    const auto i = static_cast<uint8_t>((static_cast<uint32_t>(a.elec) & kElecMask) >> kElecShift);
    const int32_t sa = sin256(i);
    const int32_t sb = kSwapB ? -cos256(i) : cos256(i);
    const int32_t da = (((sa < 0 ? -sa : sa) * static_cast<int32_t>(kPeriodTicks)) >> 15);
    const int32_t db = (((sb < 0 ? -sb : sb) * static_cast<int32_t>(kPeriodTicks)) >> 15);
    // The idle leg gets zero, which on this generator config is at worst a single 100 ns tick
    // of carrier -- 17 microamps into a 30 mH coil against the 19 mA the driven leg carries.
    (void)::mcpwm_comparator_set_compare_value(a.cmp[0], sa >= 0 ? da : 0);
    (void)::mcpwm_comparator_set_compare_value(a.cmp[1], sa >= 0 ? 0 : da);
    (void)::mcpwm_comparator_set_compare_value(a.cmp[2], sb >= 0 ? db : 0);
    (void)::mcpwm_comparator_set_compare_value(a.cmp[3], sb >= 0 ? 0 : db);
}

bool on_tick(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) noexcept {
    portENTER_CRITICAL_ISR(&g_mux);
    for (auto& a : g_ax) {
        // A parked axis keeps its coils exactly where they are: the comparators already hold
        // this phase, so re-writing them every tick would be 160 000 register writes a second
        // to change nothing.  It also means SOMETHING ELSE has to prime them -- see enable().
        if (step_axis(a)) write_coils(a);
    }
    portEXIT_CRITICAL_ISR(&g_mux);
    return false;  // no task woken
}

// ---- construction --------------------------------------------------------------------------

// One MCPWM group per shaft: two operators, four comparators, four generators.  Each generator
// is HIGH at zero and LOW at its own compare, so the comparator value IS the duty in ticks.
bool build_group(int group, const int (&pins)[4], Axis_& a) noexcept {
    mcpwm_timer_config_t tcfg{};
    tcfg.group_id = group;
    tcfg.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    tcfg.resolution_hz = kMcpwmHz;
    tcfg.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
    tcfg.period_ticks = kPeriodTicks;
    if (::mcpwm_new_timer(&tcfg, &g_pwm_timer[group]) != ESP_OK) {
        CLK_LOGE(drv_step, "mcpwm timer %d", group);
        return false;
    }

    for (int op_i = 0; op_i < 2; ++op_i) {
        mcpwm_oper_handle_t op = nullptr;
        mcpwm_operator_config_t ocfg{};
        ocfg.group_id = group;
        if (::mcpwm_new_operator(&ocfg, &op) != ESP_OK ||
            ::mcpwm_operator_connect_timer(op, g_pwm_timer[group]) != ESP_OK) {
            CLK_LOGE(drv_step, "mcpwm operator %d.%d", group, op_i);
            return false;
        }
        for (int g_i = 0; g_i < 2; ++g_i) {
            const int slot = op_i * 2 + g_i;
            mcpwm_comparator_config_t ccfg{};
            // Update at zero, so a comparator written mid-period takes effect on a carrier
            // boundary rather than truncating the pulse that is already out.
            ccfg.flags.update_cmp_on_tez = true;
            if (::mcpwm_new_comparator(op, &ccfg, &a.cmp[slot]) != ESP_OK) {
                CLK_LOGE(drv_step, "mcpwm comparator %d.%d", group, slot);
                return false;
            }
            (void)::mcpwm_comparator_set_compare_value(a.cmp[slot], 0);

            mcpwm_gen_handle_t gen = nullptr;
            mcpwm_generator_config_t gcfg{};
            gcfg.gen_gpio_num = pins[slot];
            if (::mcpwm_new_generator(op, &gcfg, &gen) != ESP_OK) {
                CLK_LOGE(drv_step, "mcpwm generator IO%d", pins[slot]);
                return false;
            }
            // IDF's own ..._END() sentinels are partial initialisers, so -Wextra fires inside
            // mcpwm_gen.h rather than in anything we wrote.  Suppressed locally, at the two
            // call sites, rather than by weakening the flag for the whole component.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
            (void)::mcpwm_generator_set_actions_on_timer_event(
                gen,
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY,
                                             MCPWM_GEN_ACTION_HIGH),
                MCPWM_GEN_TIMER_EVENT_ACTION_END());
            (void)::mcpwm_generator_set_actions_on_compare_event(
                gen,
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, a.cmp[slot],
                                               MCPWM_GEN_ACTION_LOW),
                MCPWM_GEN_COMPARE_EVENT_ACTION_END());
#pragma GCC diagnostic pop
        }
    }

    return ::mcpwm_timer_enable(g_pwm_timer[group]) == ESP_OK &&
           ::mcpwm_timer_start_stop(g_pwm_timer[group], MCPWM_TIMER_START_NO_STOP) == ESP_OK;
}

bool build() noexcept {
    if (g_built || g_build_failed) return g_built;
    g_build_failed = true;

    // Group 0 is the minute shaft, group 1 the hour, matching esp32.md's driver #1 / #2.
    if (!build_group(0, board::kPins.step_minute, g_ax[idx_of(Hand::Minute)])) return false;
    if (!build_group(1, board::kPins.step_hour, g_ax[idx_of(Hand::Hour)])) return false;

    gptimer_config_t gcfg{};
    gcfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    gcfg.direction = GPTIMER_COUNT_UP;
    gcfg.resolution_hz = kGptimerHz;
    if (::gptimer_new_timer(&gcfg, &g_tick) != ESP_OK) {
        CLK_LOGE(drv_step, "gptimer");
        return false;
    }
    gptimer_alarm_config_t acfg{};
    acfg.alarm_count = kAlarmTicks;
    acfg.reload_count = 0;
    acfg.flags.auto_reload_on_alarm = true;
    gptimer_event_callbacks_t cbs{};
    cbs.on_alarm = &on_tick;
    if (::gptimer_set_alarm_action(g_tick, &acfg) != ESP_OK ||
        ::gptimer_register_event_callbacks(g_tick, &cbs, nullptr) != ESP_OK ||
        ::gptimer_enable(g_tick) != ESP_OK) {
        CLK_LOGE(drv_step, "gptimer alarm");
        return false;
    }

    g_build_failed = false;
    g_built = true;
    CLK_LOGI(drv_step, "2x TB6612 on MCPWM0/1 @ %lu Hz carrier, commutation @ %lu Hz",
             static_cast<unsigned long>(kCarrierHz), static_cast<unsigned long>(kTickHz));
    return true;
}

void all_coils_off() noexcept {
    for (auto& a : g_ax) {
        for (auto* c : a.cmp) {
            if (c) (void)::mcpwm_comparator_set_compare_value(c, 0);
        }
    }
}

}  // namespace

Status enable(bool on) noexcept {
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    if (on == g_enabled) return Status::Ok;
    if (!build()) return Status::NotPresent;

    if (on) {
        portENTER_CRITICAL(&g_mux);
        for (auto& a : g_ax) {
            a.vel = 0;
            a.inc = 0;
            a.moving = false;
            a.stop = a.pos;
        }
        portEXIT_CRITICAL(&g_mux);

        // PRIME THE COMPARATORS.  The ISR deliberately skips a parked axis, so without this
        // the bridges would be released holding zero duty: no current, no holding torque, and
        // a rotor free to be dragged by gear friction the moment the hands are asked to move.
        // Writing the current phase instead energises the motor into the position it is
        // already in, which produces no step and no noise.
        //
        // Outside the spinlock on purpose: these are flash-resident IDF calls that take the
        // MCPWM driver's own lock, and the tick is stopped here (this path only runs when it
        // is), so there is no ISR to race and no reason to hold interrupts off across them.
        for (auto& a : g_ax) write_coils(a);

        if (::gptimer_start(g_tick) != ESP_OK) {
            all_coils_off();
            return Status::Failed;
        }
        // Standby last, and it is ~200 us of I2C away -- by which point the comparators above
        // have been latched at a carrier boundary, so the bridges never see a stale duty.
        if (const Status st = expander::set(expander::Sig::StepStby, true); st != Status::Ok) {
            (void)::gptimer_stop(g_tick);
            all_coils_off();
            return st;
        }
        g_enabled = true;
        return Status::Ok;
    }

    // Standby FIRST on the way down.  It is the one action that is unconditionally safe --
    // both bridges go high-impedance the moment the pin falls, whatever the comparators hold
    // and whatever the ISR is in the middle of.
    const Status st = expander::set(expander::Sig::StepStby, false);
    portENTER_CRITICAL(&g_mux);
    for (auto& a : g_ax) {
        a.vel = 0;
        a.inc = 0;
        a.moving = false;
        a.stop = a.pos;
    }
    portEXIT_CRITICAL(&g_mux);
    // §6.1's "coils de-energized between moves, GPTimer stopped" is exactly this call: the
    // service drops motor power between moves, so stopping the tick here is what makes the
    // 20 kHz ISR cost nothing for the >99 % of its life the hands are not moving.
    (void)::gptimer_stop(g_tick);
    all_coils_off();
    g_enabled = false;
    return st;
}

bool enabled() noexcept { return g_enabled; }

Status run(Hand h, int32_t usteps_per_s, int32_t stop_at) noexcept {
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    if (!g_enabled) return Status::NotReady;  // STEP_STBY is low: the coils are dead

    Axis_& a = g_ax[idx_of(h)];
    portENTER_CRITICAL(&g_mux);
    const int64_t target = static_cast<int64_t>(stop_at) << 16;
    const int64_t delta = target - a.pos;
    // A velocity pointing away from the target is a caller bug, not a slow move: rejecting it
    // beats running the hand into the next revolution.  Same rule, same wording, as the host
    // fake -- `motion` must not be able to tell which one it is driving.
    if (usteps_per_s == 0 || (delta > 0) != (usteps_per_s > 0)) {
        const bool arrived = delta == 0;
        portEXIT_CRITICAL(&g_mux);
        return (arrived || usteps_per_s == 0) ? Status::Ok : Status::BadArg;
    }
    a.stop = target;
    a.vel = usteps_per_s;
    // Q16.16 microsteps per tick.  The rounding is toward zero, so a velocity that does not
    // divide the tick rate lands a hair slow rather than overshooting -- and `motion` re-issues
    // run() every control tick anyway, which is what makes the landing exact (hal.hpp).
    a.inc = static_cast<int32_t>((static_cast<int64_t>(usteps_per_s) << 16) / kTickHz);
    if (a.inc == 0) a.inc = usteps_per_s > 0 ? 1 : -1;  // never silently stall
    a.moving = true;
    portEXIT_CRITICAL(&g_mux);
    return Status::Ok;
}

Status hold(Hand h) noexcept {
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    Axis_& a = g_ax[idx_of(h)];
    portENTER_CRITICAL(&g_mux);
    a.vel = 0;
    a.inc = 0;
    a.moving = false;
    a.stop = a.pos;
    portEXIT_CRITICAL(&g_mux);
    return Status::Ok;  // coils keep whatever phase they are holding
}

Status adopt(Hand h, int32_t pos) noexcept {
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    Axis_& a = g_ax[idx_of(h)];
    portENTER_CRITICAL(&g_mux);
    // Implies hold(), and `elec` is deliberately untouched: this renames the coordinate the
    // hand is at, it does not move the hand.  Homing calls it the instant it knows where zero
    // is, and a phase jump here would undo the measurement that earned the number.
    a.vel = 0;
    a.inc = 0;
    a.moving = false;
    a.pos = static_cast<int64_t>(pos) << 16;
    a.stop = a.pos;
    portEXIT_CRITICAL(&g_mux);
    return Status::Ok;
}

Axis state(Hand h) noexcept {
    Axis out{};
    Axis_& a = g_ax[idx_of(h)];
    portENTER_CRITICAL(&g_mux);
    // Round rather than truncate: a hand parked at 4095.9999 microsteps is at 4096, and
    // reporting 4095 would make `motion` chase a microstep that is not missing.
    out.pos = static_cast<int32_t>((a.pos + (1 << 15)) >> 16);
    out.vel = a.moving ? a.vel : 0;
    out.moving = a.moving;
    portEXIT_CRITICAL(&g_mux);
    return out;
}

}  // namespace clk::hal::motor
