// The light engine.                                          [FIRMWARE.md §11.1, §6.6a]
//
// Pure functions of (animation, config, now), so these tests need no clock, no HAL and no
// AO -- they hand `render()` a timestamp and check the colour.  Which is the whole reason
// the curves live in domain/ and not in ui.cpp.
#include "check.hpp"

#include "clk/domain/anim.hpp"

using namespace clk;
using domain::Anim;
using domain::AnimCfg;
using domain::Pattern;
using domain::Rgbw;

namespace {

constexpr AnimCfg kCfg{};  // the shipped defaults -- these tests pin them too
constexpr uint64_t kMs = 1000;

// Envelope at `ms` after the animation was armed at t=0.
uint8_t at(Anim a, uint32_t ms, AnimCfg const& c = kCfg) {
    a.t0_us = 0;
    return domain::envelope(a, c, ms * kMs);
}

}  // namespace

// ---- ramps ---------------------------------------------------------------------------------

void test_ramp_up() {
    const auto a = domain::ramp_up(domain::kWhite, 200, 1000);

    CHECK(at(a, 0) == 0);       // starts dark, always
    CHECK(at(a, 1000) == 200);  // arrives at exactly `level`, not near it
    CHECK(at(a, 5000) == 200);  // and HOLDS -- a ramp is not a one-shot that snaps back
    CHECK(domain::done(a, kCfg, 1000 * kMs));
    CHECK(!domain::done(a, kCfg, 999 * kMs));

    // Monotonic all the way up.  A curve that dips is a curve that flickers.
    uint8_t prev = 0;
    bool rising = true;
    for (uint32_t t = 0; t <= 1000; t += 20) {
        const uint8_t v = at(a, t);
        rising = rising && v >= prev;
        prev = v;
    }
    CHECK(rising);

    // Flat at both ends (that is what smooth_u8 buys): the first and last 50 ms move much
    // less than the middle 50 ms, so a ramp eases in and out rather than starting with a jolt.
    const int mid = at(a, 525) - at(a, 475);
    CHECK(at(a, 50) - at(a, 0) < mid);
    CHECK(at(a, 1000) - at(a, 950) < mid);
}

void test_ramp_down() {
    const auto a = domain::ramp_down(domain::kWhite, 255, 400);
    CHECK(at(a, 0) == 255);
    CHECK(at(a, 400) == 0);
    CHECK(at(a, 4000) == 0);  // stays off; "zero emission when idle" is not a moment, it is a state
    CHECK(at(a, 200) > 100 && at(a, 200) < 160);
    CHECK(domain::done(a, kCfg, 400 * kMs));
}

// The default ramp is the UI's own fade, not the sunrise's.  Both are RampUp -- the sunrise
// passes its own duration, which is the entire reason `ms` exists.
void test_ramp_duration_override() {
    const auto ui_ = domain::ramp_up(domain::kWhite, 255);  // cfg.ramp_ms = 250
    const auto sunrise = domain::ramp_up(domain::kAmber, 255, 30 * 60 * 1000);
    CHECK(at(ui_, 250) == 255);
    CHECK(at(sunrise, 250) < 4);  // 250 ms into half an hour is essentially nothing
    CHECK(at(sunrise, 15 * 60 * 1000) == 127);
    CHECK(at(sunrise, 30 * 60 * 1000) == 255);
}

// ---- swell ---------------------------------------------------------------------------------

// The tap's dial wash (§6.6b): 1 s up, 5 s lit, 4 s down, dark.  One animation rather than a
// sequencer in `ui`, so the whole gesture is a pure function of (cue, cfg, now) like the rest.
void test_swell() {
    const auto a = domain::swell(domain::kRed, 200);

    CHECK(at(a, 0) == 0);       // begins dark
    CHECK(at(a, 1000) == 200);  // up in a second, exactly at `level`
    CHECK(at(a, 3500) == 200);  // and holds through the middle
    CHECK(at(a, 6000) == 200);  // right to the end of the hold
    CHECK(at(a, 8000) == 100);  // half way down the four-second fall
    CHECK(at(a, 10'000) == 0);  // dark again, and stays there
    CHECK(at(a, 60'000) == 0);

    CHECK(!domain::done(a, kCfg, 9999 * kMs));
    CHECK(domain::done(a, kCfg, 10'000 * kMs));

    // Up then flat then down, and never back up: a wash that wobbles reads as a fault.
    uint8_t prev = 0;
    bool ok = true;
    for (uint32_t t = 0; t <= 6000; t += 20) {
        const uint8_t v = at(a, t);
        ok = ok && v >= prev;
        prev = v;
    }
    for (uint32_t t = 6000; t <= 10'000; t += 20) {
        const uint8_t v = at(a, t);
        ok = ok && v <= prev;
        prev = v;
    }
    CHECK(ok);

    // Eased at all four corners -- the same 3t^2-2t^3 as every other curve here.
    CHECK(at(a, 50) - at(a, 0) < at(a, 525) - at(a, 475));
    CHECK(at(a, 6200) - at(a, 6000) < 8);

    // Slower out than in is the whole point: at the same distance from each edge the fall has
    // barely started while the rise is nearly done.
    CHECK(at(a, 500) > at(a, 6000) - at(a, 6500));

    // All three durations come from the config, and `ms` is not one of them.
    AnimCfg c{};
    c.swell_in_ms = 100;
    c.swell_hold_ms = 100;
    c.swell_out_ms = 100;
    CHECK(at(a, 100, c) == 200);
    CHECK(at(a, 300, c) == 0);
    CHECK(domain::done(a, c, 300 * kMs));
    CHECK(domain::render(a, kCfg, 3000 * kMs).r > 0);  // and it is red, not white
    CHECK(domain::render(a, kCfg, 3000 * kMs).w == 0);
}

// ---- breathe -------------------------------------------------------------------------------

void test_breathe() {
    const auto a = domain::breathe(domain::kWhite, 100, 2000);

    CHECK(at(a, 0) == 0);              // a breath begins dark
    CHECK(at(a, 1000) == 100);         // peaks at `level` half way
    CHECK(at(a, 2000) == 0);           // and is back
    CHECK(at(a, 2000 + 1000) == 100);  // forever
    CHECK(!domain::done(a, kCfg, 10'000 * kMs));

    // Symmetric: the way up and the way down are the same curve.
    for (uint32_t t = 0; t <= 1000; t += 100) CHECK(at(a, t) == at(a, 2000 - t));

    // Two pixels armed at the same instant stay in phase for as long as anyone watches --
    // this is what "all five breathe in sync" means, and it is a property of arming them
    // together rather than of anything the engine does.
    auto p1 = domain::breathe(domain::kBlue, 180);
    auto p2 = domain::breathe(domain::kBlue, 180);
    p1.t0_us = p2.t0_us = 12'345;
    bool same_forever = true;
    for (uint64_t t = 12'345; t < 12'345 + 60'000'000ull; t += 37'000) {
        same_forever = same_forever && domain::render(p1, kCfg, t) == domain::render(p2, kCfg, t);
    }
    CHECK(same_forever);
    CHECK(domain::same(p1, p2));
}

// A COUNTED breath: the bell alongside the tap wash (§6.6b).  It has to end, and it has to end
// DARK -- a count of breaths does that where a count of milliseconds would cut it off part way.
void test_breathe_counted() {
    const auto a = domain::breathe(domain::kWhite, 200, 2500, 2);

    CHECK(at(a, 0) == 0);
    CHECK(at(a, 1250) == 200);  // first breath peaks
    CHECK(at(a, 2500) == 0);    // ... and closes
    CHECK(at(a, 3750) == 200);  // second
    CHECK(at(a, 5000) == 0);    // done, on the boundary
    CHECK(at(a, 7000) == 0);    // and there is no third

    CHECK(!domain::done(a, kCfg, 4999 * kMs));
    CHECK(domain::done(a, kCfg, 5000 * kMs));

    // The floor does not outlive the count -- once the breaths are spent the pixel is off,
    // not held at the floor forever.
    AnimCfg c{};
    c.breathe_floor = 40;
    CHECK(at(a, 0, c) == 31);  // 40 * 200/255, the floor scaled by `level`
    CHECK(at(a, 5000, c) == 0);

    // Two breaths of half the window fill it exactly -- which is how `ui` picks the period.
    CHECK(!domain::same(a, domain::breathe(domain::kWhite, 200, 2500, 0)));
}

void test_breathe_floor() {
    AnimCfg c{};
    c.breathe_floor = 40;
    const auto a = domain::breathe(domain::kWhite, 255, 2000);
    CHECK(at(a, 0, c) == 40);      // never fully dark
    CHECK(at(a, 1000, c) == 255);  // still reaches the top
}

// ---- blink and flash -------------------------------------------------------------------------

void test_blink() {
    const auto a = domain::blink(domain::kRed, 255, 200);  // 45 % duty -> 90 ms lit
    CHECK(at(a, 0) == 255);
    CHECK(at(a, 89) == 255);
    CHECK(at(a, 90) == 0);
    CHECK(at(a, 199) == 0);
    CHECK(at(a, 200) == 255);  // and again
    CHECK(!domain::done(a, kCfg, 10'000 * kMs));
    // Hard edges: nothing between lit and dark.  A blink that fades is a breath.
    bool binary = true;
    for (uint32_t t = 0; t < 1000; t += 7) {
        const uint8_t v = at(a, t);
        binary = binary && (v == 0 || v == 255);
    }
    CHECK(binary);
}

// Three quick red flashes: the refusal (FIRMWARE.md §6.6c).  It has to END, or the pixel
// never goes back to its mode.
void test_flash_burst() {
    const auto a = domain::flash(domain::kRed, 255, 3);
    const uint32_t one = kCfg.flash_ms + kCfg.flash_gap_ms;  // 200 ms

    CHECK(at(a, 0) == 255);
    CHECK(at(a, kCfg.flash_ms) == 0);
    CHECK(at(a, one) == 255);      // second
    CHECK(at(a, 2 * one) == 255);  // third
    CHECK(at(a, 3 * one) == 0);    // done -- and dark, not held lit
    CHECK(!domain::done(a, kCfg, (3 * one - 1) * kMs));
    CHECK(domain::done(a, kCfg, 3 * one * kMs));

    // Zero repeats is the endless form (a fault code), and it never reports done.
    auto forever = domain::flash(domain::kRed, 255, 0);
    CHECK(!domain::done(forever, kCfg, 60'000 * kMs));
    CHECK(at(forever, 10 * one) == 255);
}

// ---- colour, gamma, level --------------------------------------------------------------------

void test_render_gamma_and_colour() {
    // Solid at full level is the colour itself -- gamma must not tint anything at the top.
    const auto full = domain::solid(domain::kRed, 255);
    CHECK(domain::render(full, kCfg, 0) == Rgbw{255, 0, 0, 0});

    // Half `level` is a quarter of the duty (gamma 2.0), which is what makes it LOOK half.
    const auto half = domain::solid(domain::kWhite, 128);
    const auto r = domain::render(half, kCfg, 0);
    CHECK(r.w == 64);
    CHECK(r.r == 0 && r.g == 0 && r.b == 0);

    // Off renders black whatever colour it was given.
    CHECK(domain::render(domain::off(), kCfg, 999) == Rgbw{});
    CHECK(domain::render(Anim{Pattern::Off, domain::kRed, 255, 0, 0, 0}, kCfg, 999) == Rgbw{});

    // Channels keep their ratio as an animation dims -- a warm white must not go pink.
    Anim a = domain::ramp_down(Rgbw{200, 100, 50, 0}, 255, 1000);
    const auto mid = domain::render(a, kCfg, 500 * kMs);
    CHECK(mid.r > mid.g && mid.g > mid.b);
    CHECK(mid.r / 2 == mid.g);
    CHECK(mid.g / 2 == mid.b);
}

// `same()` is what stops a 50 Hz repaint from resetting every animation to t=0 -- the bug
// that would make a breath a stutter and five "synchronised" pixels a mess.
void test_same_ignores_arming_time() {
    Anim a = domain::breathe(domain::kBlue, 200);
    Anim b = domain::breathe(domain::kBlue, 200);
    a.t0_us = 1;
    b.t0_us = 999'999;
    CHECK(domain::same(a, b));

    CHECK(!domain::same(a, domain::breathe(domain::kBlue, 201)));
    CHECK(!domain::same(a, domain::breathe(domain::kRed, 200)));
    CHECK(!domain::same(a, domain::blink(domain::kBlue, 200)));
    CHECK(!domain::same(domain::flash(domain::kRed, 255, 3), domain::flash(domain::kRed, 255, 2)));
}

void run_anim_tests() {
    test_ramp_up();
    test_ramp_down();
    test_ramp_duration_override();
    test_swell();
    test_breathe();
    test_breathe_counted();
    test_breathe_floor();
    test_blink();
    test_flash_burst();
    test_render_gamma_and_colour();
    test_same_ignores_arming_time();
}
