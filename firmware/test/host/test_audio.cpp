// The audio path: the sine generator, the TAS5760M driver against the 0x6C register model,
// and the ceiling that keeps a bring-up tone off the cell protector.   [FIRMWARE.md §6.2, §11.2]
//
// Three things are worth testing here and none of them needs a speaker:
//
//   1. THE REGISTER SET.  PBTL is one bit and getting it wrong puts both halves of the bridge
//      across the DMA58-4; the reserved LSB of 0x06 must stay 1; the digital boost has to be
//      cleared or every watt in §6.2's ceiling table is 6 dB out.  All three are silent on a
//      bench and assertable here.
//   2. THE START-UP ORDER.  §9.2.1.2.1 wants the chip configured MUTED and SPK_SD raised
//      before the unmute.  The fake walks the same sequence audio_esp.cpp does.
//   3. THE VOLUME MAP.  Percent -> dB -> a 0.5 dB register ladder with a mute floor at 0x07,
//      and a ceiling that answers Denied rather than clamping.
#include "check.hpp"
#include "testutil.hpp"

#include <cmath>

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/models.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/hal/tas5760m.hpp"
#include "clk/hal/tone.hpp"

using namespace clk;
namespace sim = hal::host;
namespace model = hal::host::model;

namespace {

void fresh() {
    sim::reset();
    cli::unsafe_set(true);
}

bool near(float got, float want, float tol) { return std::fabs(got - want) <= tol; }

// ---- the generator, which is pure ---------------------------------------------------------

void test_tone_is_a_sine_at_the_frequency_asked_for() {
    hal::tone::Sine g;
    // One second of 1 kHz at 48 kHz: 1000 periods, so exactly 2000 zero crossings.  Counting
    // sign changes is the cheapest honest check that the phase accumulator is scaled right --
    // a Q32 step off by a factor of two shows up here and nowhere else until it is audible.
    g.start(1000, 48000, 1.0f, 1000);
    int16_t buf[256 * 2];
    int crossings = 0;
    int16_t prev = 0;
    std::size_t made = 0;
    for (int b = 0; b < 200 && !g.done(); ++b) {
        made += g.fill(buf, 256);
        for (std::size_t i = 0; i < 256; ++i) {
            const int16_t s = buf[2 * i];
            if ((prev < 0 && s >= 0) || (prev >= 0 && s < 0)) ++crossings;
            prev = s;
        }
    }
    CHECK(made == 48000);
    CHECK(crossings >= 1995 && crossings <= 2005);
}

void test_both_slots_carry_the_same_sample() {
    // The amp is PBTL and reads ONE slot (reg 0x06 bit 1).  A generator that filled only the
    // left would be silent on this board and fine on a stereo one.
    hal::tone::Sine g;
    g.start(440, 48000, 1.0f, 100);
    int16_t buf[64 * 2];
    g.fill(buf, 64);
    for (std::size_t i = 0; i < 64; ++i) CHECK(buf[2 * i] == buf[2 * i + 1]);
}

void test_the_envelope_starts_and_ends_at_silence() {
    hal::tone::Sine g;
    g.start(1000, 48000, 1.0f, 100);  // 4800 frames, 240 of fade at each end
    int16_t buf[4800 * 2];
    const std::size_t made = g.fill(buf, 4800);
    CHECK(made == 4800);
    CHECK(g.done());
    // First and last sample are inside the ramp, so both are small -- that is the whole point
    // of the ramp, and a click is exactly what a full-amplitude first sample sounds like.
    CHECK(buf[0] == 0);
    CHECK(std::abs(buf[2 * 4799]) < 2000);
    // ...and the middle is not.
    int16_t peak = 0;
    for (std::size_t i = 2000; i < 2400; ++i) {
        if (std::abs(buf[2 * i]) > peak) peak = static_cast<int16_t>(std::abs(buf[2 * i]));
    }
    CHECK(peak > 30000);
    // Past the end it is silence for ever, not a repeat.
    CHECK(g.fill(buf, 64) == 0);
    CHECK(buf[0] == 0 && buf[126] == 0);
}

void test_a_tone_shorter_than_two_fades_is_all_envelope() {
    hal::tone::Sine g;
    g.start(1000, 48000, 1.0f, 2);  // 96 frames; kFadeMs would want 240 at each end
    int16_t buf[96 * 2];
    CHECK(g.fill(buf, 96) == 96);
    CHECK(g.done());
    CHECK(buf[0] == 0);
}

void test_release_ends_an_endless_tone() {
    hal::tone::Sine g;
    g.start(1000, 48000, 1.0f, 0);
    int16_t buf[1024 * 2];
    CHECK(g.fill(buf, 1024) == 1024);
    CHECK(!g.done());
    g.release();
    // One fade's worth left (240 frames at 48 kHz) and then nothing.
    CHECK(g.fill(buf, 1024) == 240);
    CHECK(g.done());
}

// ---- the volume ladder ----------------------------------------------------------------------

void test_percent_is_amplitude_and_the_ladder_is_half_a_dB() {
    using hal::tas5760m::db_for_pct;
    using hal::tas5760m::db_for_vol_reg;
    using hal::tas5760m::vol_reg_for_db;

    CHECK(near(db_for_pct(100), 0.0f, 0.01f));
    CHECK(near(db_for_pct(50), -6.02f, 0.02f));
    CHECK(near(db_for_pct(10), -20.0f, 0.01f));

    // 0xCF is 0 dB and one step is 0.5 dB, so -20 dB is 40 steps down.
    CHECK(vol_reg_for_db(0.0f) == 0xCF);
    CHECK(vol_reg_for_db(-20.0f) == 0xCF - 40);
    CHECK(vol_reg_for_db(24.0f) == 0xFF);
    CHECK(near(db_for_vol_reg(0xCF), 0.0f, 0.001f));
    CHECK(near(db_for_vol_reg(0xA7), -20.0f, 0.001f));

    // The floor.  Anything under 0x07 mutes on the real part, so the map does not hand back a
    // register in 0x01..0x06 that would look like -101 dB and behave like silence.
    CHECK(vol_reg_for_db(-100.0f) == 0x00);
    CHECK(vol_reg_for_db(-200.0f) == 0x00);
    CHECK(hal::tas5760m::db_for_pct(0) <= hal::tas5760m::kVolMinDb);
}

// ---- the driver, against the register model ------------------------------------------------

void test_configure_writes_pbtl_the_right_gain_and_no_digital_boost() {
    fresh();
    CHECK(hal::audio::enable(true) == Status::Ok);

    // 0x06: PBTL on (bit 7), PWM rate at the POR 101, A_GAIN 00 = 19.2 dBV, PBTL Ch Sel 0,
    // and the reserved LSB left at its mandated 1.
    CHECK(model::tas5760m_reg(0x06) == 0xD1);

    // 0x02: HPF in, digital boost +0 dB, single speed, I2S.  The boost is the one that
    // matters -- its POR is +6 dB, and §6.2's whole watt table assumes 0 dBFS = 9.12 V rms,
    // which is only true at +0.
    CHECK(model::tas5760m_reg(0x02) == 0x04);
    CHECK(((model::tas5760m_reg(0x02) >> 4) & 0x03) == 0);  // digital boost field
    CHECK((model::tas5760m_reg(0x02) & 0x80) == 0);         // HPF not bypassed

    // 0x01: not asleep, not shut down at the register level -- the PIN is the gate.
    CHECK((model::tas5760m_reg(0x01) & 0x03) == 0x01);

    // Both channel volumes, at the default 10 % = -20 dB.
    CHECK(model::tas5760m_reg(0x04) == 0xA7);
    CHECK(model::tas5760m_reg(0x05) == 0xA7);

    // Unmuted, with fade enabled.
    CHECK(model::tas5760m_reg(0x03) == 0x80);
}

void test_the_startup_order_is_the_datasheets() {
    fresh();
    // Before anything: SPK_SD low, and that is what the board does at POR with no firmware
    // help (expander hi-Z, OLAT 0).
    CHECK(hal::expander::get(hal::expander::Sig::SpkSd).v == false);
    CHECK(hal::tas5760m::shadow().configured == false);

    CHECK(hal::audio::enable(true) == Status::Ok);
    CHECK(hal::expander::get(hal::expander::Sig::SpkSd).v == true);
    CHECK(hal::tas5760m::shadow().configured == true);
    CHECK(hal::tas5760m::shadow().muted == false);
    CHECK(hal::audio::active());

    // Down again: muted first (§9.2.1.2.2), then the pin.
    CHECK(hal::audio::enable(false) == Status::Ok);
    CHECK(model::tas5760m_reg(0x03) == 0x83);  // Mute R + Mute L
    CHECK(hal::expander::get(hal::expander::Sig::SpkSd).v == false);
    CHECK(!hal::audio::active());
}

void test_the_volume_ceiling_refuses_rather_than_clamping() {
    fresh();
    CHECK(hal::audio::set_volume_pct(hal::audio::kMaxVolPct) == Status::Ok);
    CHECK(hal::audio::volume_pct() == hal::audio::kMaxVolPct);

    // Over the ceiling is Denied and the setting does NOT move.  A clamp would be a volume
    // that lies, and R-AUDIO-1's failure mode -- a protector trip -- presents as a reboot,
    // so the one thing that must not happen is the CLI reporting a level it did not set.
    CHECK(hal::audio::set_volume_pct(static_cast<uint8_t>(hal::audio::kMaxVolPct + 1)) ==
          Status::Denied);
    CHECK(hal::audio::set_volume_pct(100) == Status::Denied);
    CHECK(hal::audio::volume_pct() == hal::audio::kMaxVolPct);
    CHECK(hal::audio::set_volume_pct(101) == Status::BadArg);

    // ...and a legal one reaches the chip, live, without a reconfigure.
    CHECK(hal::audio::enable(true) == Status::Ok);
    CHECK(hal::audio::set_volume_pct(10) == Status::Ok);
    CHECK(model::tas5760m_reg(0x04) == 0xA7);
    CHECK(model::tas5760m_reg(0x05) == 0xA7);
}

void test_a_tone_brings_the_amp_up_and_stop_parks_it() {
    fresh();
    CHECK(!hal::audio::playing());
    CHECK(hal::audio::tone(440, 500) == Status::Ok);
    CHECK(hal::audio::playing());
    CHECK(hal::audio::active());

    CHECK(hal::audio::stop() == Status::Ok);
    CHECK(!hal::audio::playing());
    CHECK(!hal::audio::active());
    CHECK(hal::expander::get(hal::expander::Sig::SpkSd).v == false);

    // A tone with a duration ends on its own, on sim time.
    CHECK(hal::audio::tone(440, 100) == Status::Ok);
    CHECK(hal::audio::playing());
    sim::advance(200000);
    CHECK(!hal::audio::playing());
    CHECK(hal::expander::get(hal::expander::Sig::SpkSd).v == false);

    CHECK(hal::audio::tone(5, 100) == Status::BadArg);
    CHECK(hal::audio::tone(48000, 100) == Status::BadArg);
}

void test_faults_are_read_from_the_chip() {
    fresh();
    CHECK(hal::audio::enable(true) == Status::Ok);
    auto f = hal::tas5760m::faults();
    CHECK(f.ok() && !f.v.clk && !f.v.oc && !f.v.dc && !f.v.ot);

    model::tas5760m_set_faults(0x0F);
    f = hal::tas5760m::faults();
    CHECK(f.ok() && f.v.clk && f.v.oc && f.v.dc && f.v.ot);

    // Reg 0x08's low nibble is read-only on the real part.  A driver that read-modify-wrote
    // the OCE threshold must not be able to invent an over-current.
    CHECK(hal::tas5760m::write_reg(0x08, 0x00) == Status::Ok);
    CHECK((model::tas5760m_reg(0x08) & 0x0F) == 0x0F);

    RecordingSink r;
    CHECK(run("audio status", r) == Status::Ok);
    CHECK(r.contains("reg 0x08:"));
    CHECK(r.contains("OC"));
}

void test_an_absent_amp_is_notpresent_everywhere() {
    fresh();
    board::set_present(board::Dev::Amp, false);
    hal::tas5760m::forget();

    CHECK(hal::audio::enable(true) == Status::NotPresent);
    CHECK(hal::audio::tone(440, 100) == Status::NotPresent);
    CHECK(hal::audio::state().st == Status::NotPresent);
    CHECK(hal::tas5760m::faults().st == Status::NotPresent);
    // Volume is the exception, and deliberately: it is remembered so that fitting the amp
    // does not silently revert a setting the operator already made (D16 -- absence is not an
    // error, and a setting that reverts is worse than one that waits).
    CHECK(hal::audio::set_volume_pct(20) == Status::NotPresent);
    CHECK(hal::audio::volume_pct() == 20);

    RecordingSink r;
    CHECK(run("audio tone 440 100", r) == Status::NotPresent);
    CHECK(r.contains("no amp fitted"));
}

void test_cli_reports_the_chain_and_the_refusals() {
    fresh();
    RecordingSink r;
    CHECK(run("audio tone 440 200", r) == Status::Ok);

    RecordingSink s;
    CHECK(run("audio status", s) == Status::Ok);
    CHECK(s.contains("clocks=on"));
    CHECK(s.contains("sd_pin=high"));
    CHECK(s.contains("configured=yes"));
    CHECK(s.contains("PBTL mono"));
    CHECK(s.contains("256 x fs"));

    RecordingSink v;
    CHECK(run("audio vol 100", v) == Status::Denied);
    CHECK(v.contains("bring-up volume ceiling"));
    CHECK(!v.contains("= 0.0 dB at the amp"));  // a refusal must never print as a success

    RecordingSink b;
    CHECK(run("audio tone 440 999999", b) == Status::BadArg);
}

}  // namespace

void run_audio_tests() {
    test_tone_is_a_sine_at_the_frequency_asked_for();
    test_both_slots_carry_the_same_sample();
    test_the_envelope_starts_and_ends_at_silence();
    test_a_tone_shorter_than_two_fades_is_all_envelope();
    test_release_ends_an_endless_tone();
    test_percent_is_amplitude_and_the_ladder_is_half_a_dB();
    test_configure_writes_pbtl_the_right_gain_and_no_digital_boost();
    test_the_startup_order_is_the_datasheets();
    test_the_volume_ceiling_refuses_rather_than_clamping();
    test_a_tone_brings_the_amp_up_and_stop_parks_it();
    test_faults_are_read_from_the_chip();
    test_an_absent_amp_is_notpresent_everywhere();
    test_cli_reports_the_chain_and_the_refusals();
    sim::reset();
    cli::unsafe_set(false);
}
