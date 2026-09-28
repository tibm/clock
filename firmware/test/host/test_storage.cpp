// The alarm's sound: the WAV check, the ring, the mixer, the card, `storage`, and the alarm
// ringing through `ui`.                                   [FIRMWARE.md §6.2, §6.3, §6.6, §11.2]
//
// The pure half runs first (run_storage_tests).  The AO half (run_storage_service_tests) runs
// inside test_motion's block, where the real AOs are up and wired as app_main wires them.
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "check.hpp"
#include "testutil.hpp"

#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/hal/pcm.hpp"
#include "clk/hal/wav.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/ui.hpp"

using namespace clk;
namespace sim = hal::host;
namespace wav = hal::wav;
namespace pcm = hal::pcm;

namespace {

// ---- building WAVs --------------------------------------------------------------------------

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A WAV the way an encoder writes one.  `list` puts an Audacity/ffmpeg-style LIST chunk
// between fmt and data -- the case a canonical-44-byte check gets wrong.
std::vector<uint8_t> make_wav(uint32_t rate, uint16_t ch, uint16_t bits, uint32_t frames,
                              uint16_t format = 1, bool list = false, int16_t value = 8000) {
    std::vector<uint8_t> b;
    const uint32_t data = frames * ch * (bits / 8u);
    tag(b, "RIFF");
    put32(b, 0);  // patched below
    tag(b, "WAVE");
    tag(b, "fmt ");
    put32(b, 16);
    put16(b, format);
    put16(b, ch);
    put32(b, rate);
    put32(b, rate * ch * (bits / 8u));
    put16(b, static_cast<uint16_t>(ch * (bits / 8u)));
    put16(b, bits);
    if (list) {
        tag(b, "LIST");
        put32(b, 27);  // odd: one pad byte follows
        for (int i = 0; i < 28; ++i) b.push_back('x');
    }
    tag(b, "data");
    put32(b, data);
    for (uint32_t i = 0; i < data / 2u; ++i) put16(b, static_cast<uint16_t>(value));
    const uint32_t riff = static_cast<uint32_t>(b.size() - 8);
    for (int i = 0; i < 4; ++i) b[4 + i] = static_cast<uint8_t>(riff >> (8 * i));
    return b;
}

wav::Info parse(std::vector<uint8_t> const& b) {
    const std::size_t n = b.size() < wav::kHeaderMax ? b.size() : wav::kHeaderMax;
    return wav::parse(b.data(), n, static_cast<uint32_t>(b.size()));
}

// ---- wav ------------------------------------------------------------------------------------

void test_wav_accepts_48k_mono_16() {
    const auto in = parse(make_wav(48000, 1, 16, 4800));
    CHECK(in.ok());
    CHECK(in.data_off == 44);
    CHECK(in.data_bytes == 9600);
    CHECK(in.ms() == 100);
}

void test_wav_skips_a_list_chunk() {
    // What Audacity and ffmpeg (without -bitexact) actually write.
    const auto in = parse(make_wav(48000, 1, 16, 480, 1, true));
    CHECK(in.ok());
    CHECK(in.data_off == 44 + 8 + 28);
}

void test_wav_refuses_everything_else_by_name() {
    CHECK(parse(make_wav(44100, 1, 16, 441)).err == wav::Err::Rate);
    CHECK(parse(make_wav(48000, 2, 16, 480)).err == wav::Err::Channels);
    CHECK(parse(make_wav(48000, 1, 24, 480)).err == wav::Err::Bits);
    CHECK(parse(make_wav(48000, 1, 32, 480, 3)).err == wav::Err::NotPcm);  // float
    // The refusal still carries what the file IS, so the CLI can say so.
    const auto cd = parse(make_wav(44100, 2, 16, 44100));
    CHECK(cd.rate == 44100 && cd.channels == 2);
    CHECK(cd.ms() == 1000);

    std::vector<uint8_t> junk(64, 0x55);
    CHECK(parse(junk).err == wav::Err::NotWav);
    const auto good = make_wav(48000, 1, 16, 480);
    CHECK(wav::parse(good.data(), 30, 30).err == wav::Err::Truncated);
}

void test_wav_clips_a_data_chunk_that_lies() {
    // A copy cut short: the header says 9600 bytes of samples, the file holds 1001.
    auto b = make_wav(48000, 1, 16, 4800);
    b.resize(44 + 1001);
    const auto in = parse(b);
    CHECK(in.ok());
    CHECK(in.data_bytes == 1000);  // clipped, and to whole samples
}

// ---- ring -----------------------------------------------------------------------------------

void test_ring_wraps() {
    int16_t store[8];
    pcm::Ring r;
    r.attach(store, 8);
    const int16_t a[5] = {1, 2, 3, 4, 5};
    CHECK(r.write(a, 5) == 5);
    int16_t o[8]{};
    CHECK(r.read(o, 3) == 3);
    CHECK(o[0] == 1 && o[2] == 3);
    const int16_t b[7] = {6, 7, 8, 9, 10, 11, 12};
    CHECK(r.write(b, 7) == 6);  // room for six: 8 - (5 - 3)
    CHECK(r.space() == 0);
    CHECK(r.read(o, 8) == 8);
    CHECK(o[0] == 4 && o[1] == 5 && o[2] == 6 && o[7] == 11);
    CHECK(r.level() == 0);
}

// ---- mixer ----------------------------------------------------------------------------------

struct Rig {
    std::vector<int16_t> store = std::vector<int16_t>(96000);
    pcm::Ring ring;
    pcm::Mixer mix;
    Rig() { ring.attach(store.data(), store.size()); }
    void feed(std::size_t n, int16_t v = 10000) {
        std::vector<int16_t> s(n, v);
        ring.write(s.data(), n);
    }
    // Frames out, mono (slot 0).
    std::vector<int16_t> pull(std::size_t frames, bool eof = false) {
        std::vector<int16_t> st(frames * 2);
        mix.fill(ring, eof, st.data(), frames);
        std::vector<int16_t> m(frames);
        for (std::size_t i = 0; i < frames; ++i) m[i] = st[2 * i];
        return m;
    }
};

int max_step(std::vector<int16_t> const& v) {
    int m = 0;
    for (std::size_t i = 1; i < v.size(); ++i) m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}

void test_mixer_waits_to_prime() {
    Rig r;
    r.mix.start(48000, 0);
    r.feed(1000);
    auto out = r.pull(256);
    CHECK(!r.mix.primed());
    CHECK(out[255] == 0);
    CHECK(r.ring.level() == 1000);  // nothing consumed while priming
    // 250 ms in the ring -- or the producer saying that is all there is -- starts it.
    r.feed(12000);
    out = r.pull(256);
    CHECK(r.mix.primed());
    CHECK(out[100] == 10000);
}

void test_mixer_short_file_plays_whole() {
    Rig r;
    r.mix.start(48000, 0);
    r.feed(500);  // under the prime threshold, but eof: that is the whole file
    const auto out = r.pull(1024, true);
    CHECK(r.mix.primed());
    CHECK(out[0] == 10000 && out[499] == 10000);
    CHECK(r.mix.done());
    CHECK(r.mix.frames_played() == 500);
    CHECK(r.mix.underruns() == 0);  // the end of a file is not an underrun
    CHECK(std::abs(out[500]) < 10000 && std::abs(out[1023]) < 200);  // decayed, not stepped
}

void test_mixer_ramps_from_the_floor() {
    Rig r;
    r.mix.start(48000, 1000);  // one second
    r.feed(96000);
    auto out = r.pull(256);
    CHECK(out[0] > 200 && out[0] < 400);              // -30 dB of 10000
    for (int i = 0; i < 188; ++i) out = r.pull(256);  // ~1 s in
    CHECK(out[255] == 10000);
    CHECK(r.mix.gain() == 1.0f);
}

void test_mixer_underrun_fades_and_counts_once() {
    Rig r;
    r.mix.start(48000, 0);
    r.feed(12000);
    auto a = r.pull(12000);         // drain it exactly
    auto gap = r.pull(2000);        // ... and then some: starving
    CHECK(r.mix.underruns() == 1);  // one gap, one count, however many blocks it lasts
    CHECK(!r.mix.done());
    // Never a click: the step from the last real sample into the gap is a decay, not 10000.
    std::vector<int16_t> seam{a.end() - 8, a.end()};
    seam.insert(seam.end(), gap.begin(), gap.end());
    CHECK(max_step(seam) < 400);
    CHECK(gap.back() == 0);
    // Data returns: it fades back in rather than stepping to full.
    r.feed(12000);
    const auto back = r.pull(1000);
    CHECK(max_step(back) < 400);
    CHECK(back.back() == 10000);
    CHECK(r.mix.underruns() == 1);
}

void test_mixer_release_is_a_tail() {
    Rig r;
    r.mix.start(48000, 0);
    r.feed(48000);
    (void)r.pull(512);
    r.mix.release();
    const auto out = r.pull(512);
    CHECK(r.mix.done());
    CHECK(max_step(out) < 400);
    CHECK(out.back() == 0);
}

// ---- the fake card --------------------------------------------------------------------------

std::string g_dir;

void write_file(const std::string& rel, std::vector<uint8_t> const& b) {
    const std::string p = g_dir + "/" + rel;
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return;
    std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
}

void make_card() {
    char tmpl[] = "/tmp/clk-sd-XXXXXX";
    const char* d = ::mkdtemp(tmpl);
    g_dir = d ? d : "";
    ::mkdir((g_dir + "/tones").c_str(), 0755);
    write_file("tones/short.wav", make_wav(48000, 1, 16, 4800));           // 100 ms
    write_file("tones/birds.wav", make_wav(48000, 1, 16, 9600, 1, true));  // 200 ms
    write_file("tones/cd44.wav", make_wav(44100, 1, 16, 4410));
    write_file("tones/stereo.wav", make_wav(48000, 2, 16, 480));
    write_file("tones/readme.txt", {'h', 'i'});
}

struct Seen {
    int n = 0;
    bool birds = false, dir = false;
};

bool count_entry(hal::sd::Entry const& e, void* ctx) {
    auto& s = *static_cast<Seen*>(ctx);
    ++s.n;
    if (std::strcmp(e.name, "birds.wav") == 0 && e.size == 44 + 36 + 19200) s.birds = true;
    if (e.dir) s.dir = true;
    return true;
}

void test_sd_absent_until_there_is_a_card() {
    sim::set_sd_dir("");
    CHECK(hal::sd::mount() == Status::NotPresent);
    CHECK(!hal::sd::mounted());
    CHECK(hal::sd::open("/sd/tones/birds.wav").st == Status::NotPresent);
    sim::set_sd_dir(g_dir.c_str());
    CHECK(hal::sd::mount() == Status::Ok);
    Seen s;
    CHECK(hal::sd::list(hal::sd::kTonesDir, &count_entry, &s) == Status::Ok);
    CHECK(s.n == 5 && s.birds && !s.dir);
    CHECK(hal::sd::list("/sd/nope", &count_entry, &s) == Status::Failed);
    CHECK(hal::sd::open("/etc/passwd").st == Status::BadArg);  // nothing outside /sd
    const auto fd = hal::sd::open("/sd/tones/short.wav");
    CHECK(fd.ok());
    uint8_t hdr[wav::kHeaderMax];
    const auto r = hal::sd::read(fd.v, hdr, sizeof hdr);
    CHECK(r.ok() && r.v == sizeof hdr);
    CHECK(wav::parse(hdr, r.v, hal::sd::size(fd.v).v).ok());
    hal::sd::close(fd.v);
    CHECK(hal::sd::unmount() == Status::Ok);
}

// ---- the AOs --------------------------------------------------------------------------------

svc::Storage& sto() { return svc::storage(); }

template <class Fn>
bool wait_until(Fn pred, int max_ms = 4000) {
    for (int i = 0; i < max_ms / 5; ++i) {
        if (pred()) return true;
        hal::clock_::sleep_ms(5);
    }
    return pred();
}

bool answered(uint32_t seq) {
    return seq &&
           wait_until([&] { return static_cast<int32_t>(sto().snapshot().done_seq - seq) >= 0; });
}

bool mode_is(svc::Ui::Mode m, int ms = 2000) {
    return wait_until([&] { return svc::ui().snapshot().mode == m; }, ms);
}

bool quiet() {
    return wait_until([] {
        return sto().snapshot().playing == svc::Storage::Playing::Nothing &&
               !hal::audio::stream().open;
    });
}

void test_storage_plays_a_file_to_the_end() {
    sim::set_warp(1.0);
    RecordingSink r;
    CHECK(run("audio play short.wav", r) == Status::Ok);
    CHECK(r.contains("play short.wav"));
    CHECK(quiet());
    // Every sample of the 100 ms file went through the ring and the mixer, and none late.
    const auto st = hal::audio::stream();
    CHECK(st.played == 4800);
    CHECK(st.underruns == 0);
}

void test_storage_loops_seamlessly() {
    sim::set_warp(1.0);
    CHECK(answered(sto().play("birds.wav", true)));
    CHECK(sto().snapshot().last_st == Status::Ok);
    CHECK(wait_until([] { return sto().snapshot().loops >= 3; }));
    CHECK(hal::audio::stream().open);
    CHECK(hal::audio::stream().underruns == 0);
    RecordingSink r;
    CHECK(run("audio stop", r) == Status::Ok);
    CHECK(quiet());
}

void test_storage_refuses_a_wrong_file_by_name() {
    RecordingSink r;
    CHECK(run("audio play cd44.wav", r) == Status::BadArg);
    CHECK(r.contains("sample rate is not 48000 Hz"));
    CHECK(r.contains("ffmpeg"));
    RecordingSink r2;
    CHECK(run("audio play nothere.wav", r2) == Status::Failed);
    CHECK(r2.contains("no such file"));
    RecordingSink r3;
    CHECK(run("storage ls", r3) == Status::Ok);
    CHECK(r3.contains("2 playable"));
    CHECK(r3.contains("not mono"));
}

void test_storage_a_tone_replaces_the_stream() {
    CHECK(answered(sto().play("birds.wav", true)));
    CHECK(hal::audio::stream().open);
    CHECK(hal::audio::tone(440, 50) == Status::Ok);
    // `storage` notices on its next pump and lets go of the file.
    CHECK(quiet());
}

void test_storage_alarm_tone_is_checked_and_persisted() {
    char nvs[] = "/tmp/clk-nvs-XXXXXX";
    const int fd = ::mkstemp(nvs);
    if (fd >= 0) ::close(fd);
    sim::set_store_path(nvs);

    RecordingSink r;
    CHECK(run("chrono alarm tone stereo.wav", r) == Status::BadArg);
    CHECK(r.contains("not mono"));
    CHECK(sto().snapshot().alarm_tone[0] == '\0');  // a refused file is not chosen
    RecordingSink r2;
    CHECK(run("chrono alarm tone tones/birds.wav", r2) == Status::BadArg);  // bare names only
    RecordingSink r3;
    CHECK(run("chrono alarm tone birds.wav", r3) == Status::Ok);
    CHECK(r3.contains("tone birds.wav"));
    char got[hal::store::kStrMax];
    CHECK(hal::store::get_str("sto.tone", got, sizeof got) == Status::Ok);
    CHECK_STREQ(got, "birds.wav");
    // ... and it is in the file, which is what survives a reboot.
    sim::set_store_path(nvs);  // reload from disk
    CHECK(hal::store::get_str("sto.tone", got, sizeof got) == Status::Ok);
    CHECK_STREQ(got, "birds.wav");
    sim::set_store_path("");
    ::unlink(nvs);
}

// Fire -> ring (the file, looped, ramping) -> tap = snooze -> the snooze runs out -> ring
// again -> long press = dismissed.  §6.6's diagram, walked end to end.
void test_alarm_rings_snoozes_and_dismisses() {
    sim::set_warp(1.0);
    RecordingSink r;
    CHECK(run("chrono alarm fire", r) == Status::Ok);
    CHECK(r.contains("birds.wav"));
    CHECK(mode_is(svc::Ui::Mode::Ringing));
    auto s = sto().snapshot();
    CHECK(s.playing == svc::Storage::Playing::File && s.alarm && s.loop);
    CHECK(hal::audio::stream().open);

    sim::tap();
    CHECK(mode_is(svc::Ui::Mode::Snoozed));
    CHECK(quiet());
    const auto u = svc::ui().snapshot();
    CHECK(u.snooze_left_ms > 8 * 60 * 1000);

    sim::advance(9ull * 60 * 1000 * 1000 + 100'000);  // the snooze, in sim time
    CHECK(mode_is(svc::Ui::Mode::Ringing));
    CHECK(wait_until([] { return sto().snapshot().playing == svc::Storage::Playing::File; }));

    sim::press(1000);  // a long press
    CHECK(mode_is(svc::Ui::Mode::Idle, 3000));
    CHECK(quiet());
}

void test_alarm_falls_back_to_the_beep() {
    RecordingSink r;
    CHECK(run("chrono alarm tone none", r) == Status::Ok);
    RecordingSink r2;
    CHECK(run("chrono alarm fire", r2) == Status::Ok);
    CHECK(r2.contains("fallback beep"));
    CHECK(sto().snapshot().playing == svc::Storage::Playing::Beep);
    CHECK(wait_until([] { return hal::audio::playing(); }));  // it makes a sound
    RecordingSink r3;
    CHECK(run("chrono alarm dismiss", r3) == Status::Ok);
    CHECK(quiet());

    // No card at all: the chosen file cannot play, so the beep it is -- and the answer says why.
    CHECK(answered(sto().select_tone("birds.wav")));
    CHECK(answered(sto().unmount()));
    sim::set_sd_dir("");
    svc::ui().fire_alarm();
    CHECK(mode_is(svc::Ui::Mode::Ringing));
    CHECK(wait_until([] { return sto().snapshot().playing == svc::Storage::Playing::Beep; }));
    CHECK(std::strcmp(sto().snapshot().last_why ? sto().snapshot().last_why : "", "no card") == 0);
    svc::ui().dismiss();
    CHECK(mode_is(svc::Ui::Mode::Idle));
    CHECK(quiet());
    sim::set_sd_dir(g_dir.c_str());
    CHECK(answered(sto().mount()));
}

void test_alarm_fires_at_its_minute_once() {
    sim::set_warp(1.0);
    RecordingSink r;
    CHECK(run("chrono alarm set 07:00", r) == Status::Ok);
    CHECK(run("chrono alarm arm on", r) == Status::Ok);
    CHECK(run("chrono time set 06:59:58", r) == Status::Ok);
    CHECK(mode_is(svc::Ui::Mode::Idle));
    CHECK(mode_is(svc::Ui::Mode::Ringing, 4000));
    RecordingSink r2;
    CHECK(run("chrono alarm dismiss", r2) == Status::Ok);
    // Still 07:00: dismissed means dismissed, not "ring again on the next tick".
    hal::clock_::sleep_ms(300);
    CHECK(svc::ui().snapshot().mode == svc::Ui::Mode::Idle);
    CHECK(run("chrono alarm arm off", r) == Status::Ok);
    CHECK(quiet());
}

}  // namespace

void run_storage_tests() {
    test_wav_accepts_48k_mono_16();
    test_wav_skips_a_list_chunk();
    test_wav_refuses_everything_else_by_name();
    test_wav_clips_a_data_chunk_that_lies();
    test_ring_wraps();
    test_mixer_waits_to_prime();
    test_mixer_short_file_plays_whole();
    test_mixer_ramps_from_the_floor();
    test_mixer_underrun_fades_and_counts_once();
    test_mixer_release_is_a_tail();
    make_card();
    test_sd_absent_until_there_is_a_card();
}

// Called from run_motion_service_tests, with the AOs (storage included) up and wired.
void run_storage_service_tests() {
    sim::set_sd_dir(g_dir.c_str());
    (void)answered(sto().mount());
    test_storage_plays_a_file_to_the_end();
    test_storage_loops_seamlessly();
    test_storage_refuses_a_wrong_file_by_name();
    test_storage_a_tone_replaces_the_stream();
    test_storage_alarm_tone_is_checked_and_persisted();
    test_alarm_rings_snoozes_and_dismisses();
    test_alarm_falls_back_to_the_beep();
    test_alarm_fires_at_its_minute_once();
    RecordingSink r;
    (void)run("chrono alarm tone none", r);
    sim::set_sd_dir("");
}
