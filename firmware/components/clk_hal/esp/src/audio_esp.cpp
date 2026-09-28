// hal::audio on the ESP32-S3: I2S0 + the TAS5760M's start-up order.  [FIRMWARE.md §6.2, §12.0.15]
//
// Its own file rather than another namespace in hal_esp.cpp, for one reason: this is the only
// peripheral in the HAL that owns a TASK.  DMA has to be fed, so somebody has to be running
// when nobody is calling -- and everything that follows from that (the request slot, the
// idle park, the teardown ordering) is easier to read next to the thing that causes it.
//
// The datasheet's order is not advisory (§9.2.1.2.1, and the NOTE under it):
//
//    SPK_SD pin LOW -> supplies up -> START MCLK/SCLK/LRCK -> configure over I2C, muted
//      -> SPK_SD pin HIGH -> unmute over I2C
//    ... and down again: mute -> SPK_SD LOW -> stop the clocks
//
// Two of those are easy to get backwards and both are silent when you do.  Configuring
// before the clocks are up is a chip that ACKs every write and does nothing; unmuting before
// SPK_SD goes high loses the mute the moment the output stage powers on.  The whole sequence
// therefore lives in ONE pair of functions here (`bring_up` / `bring_down`) and nothing else
// touches SPK_SD.
#include <atomic>
#include <cmath>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/pcm.hpp"
#include "clk/hal/tas5760m.hpp"
#include "clk/hal/tone.hpp"
#include "clk/log.hpp"
#include "clk/port.hpp"

namespace clk::hal::audio {
namespace {

// esp32.md's I2S block.  IO43 is the former UART0 TXD -- the boot-ROM banner still comes out
// of it before app_main runs, which is why the console is USB-CDC only on every profile that
// matters (sdkconfig.defaults).  On BOARD=devkit-uart the REPL is on UART0 and grabbing IO43
// would take the console with it; the presence mask is what stops that, since the devkit
// starts with `amp` absent and nothing here runs until something says otherwise.
constexpr gpio_num_t kMclk = GPIO_NUM_43;
constexpr gpio_num_t kBclk = GPIO_NUM_10;
constexpr gpio_num_t kWs = GPIO_NUM_11;
constexpr gpio_num_t kDout = GPIO_NUM_12;

// 256 frames = 5.3 ms at 48 kHz, which is §6.2's block size.  Six of them in DMA is 32 ms of
// slack -- enough that a `board i2c scan` or an SD hiccup does not underrun, and short enough
// that stop() takes effect while you are still listening for it.
constexpr std::size_t kBlockFrames = 256;

// The clock geometry, mutable from the bench (`audio clk`).  Defaults are the design's:
// MCLK 256 x f_S = 12.288 MHz, 16-bit slots = BCLK 32 x f_S.  Both are inside the amp's
// Table 6 at 48 kHz, and both are re-read on every port install.
uint16_t g_mclk_mult = 256;
uint8_t g_slot_bits = 16;
bool g_pins_held = false;  // a pin test owns the four pads; the port does not exist
constexpr int kDmaDescs = 6;
constexpr int kDmaFrames = 240;

// After the last block, hold the amp up briefly rather than tearing it down: `ui`'s preview
// chime fires every 1.5 s and re-running the start-up sequence for each one is ~20 ms of
// I2C and delay per beep, all of it audible as the relay-like tick of SPK_SD.
constexpr uint32_t kIdleParkMs = 500;

// §3.2's table, verbatim: motion(20) audio(18) storage(14) ... -- the second most urgent
// thing on the box, because a starved DMA is an audible click and a late hand is not.
constexpr int kTaskPrio = 18;
// 6 KB, not 4: configure() logs at Info with two %.1f, and on xtensa the float formatter is
// several hundred bytes on its own.  stream.cpp's producer task learned this the expensive
// way (a smashed neighbour presenting as a ringbuffer assert), and this task has the same
// shape -- rare, deep, and nobody watching when it overflows.
constexpr std::size_t kTaskStack = 6144;

port::Mutex g_mx;
// bring_up()/bring_down() walk a five-step hardware sequence and are reachable from two
// threads -- the writer task and whoever calls enable().  Interleaving them would drop
// SPK_SD in the middle of a configure.  One owner at a time, and it is NOT g_mx: this is held
// across ~25 ms of I2C and vTaskDelay, and g_mx is what volume_pct() takes.
port::Mutex g_seq_mx;
port::Signal g_sig;

i2s_chan_handle_t g_tx = nullptr;
bool g_task_started = false;

// --- guarded by g_mx ---
// Which source the writer is draining.  One at a time: starting either replaces the other.
enum class Src : uint8_t { None, Tone, Stream };
Src g_src = Src::None;
tone::Sine g_gen;
// The stream.  The ring's two indices are atomics (SPSC, pcm.hpp) and `storage` writes it
// without this lock; everything else -- clearing it, the mixer, which source is live -- is
// under g_mx, which the writer also holds while it fills a block.  That is what makes
// stream_open()'s clear safe: the writer cannot be halfway through a read of the ring.
pcm::Ring g_ring;
int16_t* g_ring_buf = nullptr;  // PSRAM, allocated on the first stream_open(), never freed
pcm::Mixer g_mix;
std::atomic<bool> g_eof{false};
std::atomic<uint32_t> g_stream_seq{0};
bool g_req_stream = false;  // the pending start is for the stream, not a tone
bool g_req_pending = false;
uint32_t g_req_hz = 0;
uint32_t g_req_ms = 0;
bool g_req_stop = false;
uint8_t g_vol_pct = kDefaultVolPct;

// Written by the writer task, read by everyone.  Atomic rather than under g_mx so that
// `audio status` cannot block behind a block being generated.
std::atomic<bool> g_clocks{false};
std::atomic<bool> g_amp_up{false};
std::atomic<bool> g_playing{false};
std::atomic<uint32_t> g_underruns{0};

// Why the last start attempt did not make a sound.  tone() is asynchronous, so this is the
// ONLY channel a failure on the writer task has back to the operator -- without it, `audio
// tone` prints a success for an amp that never came out of shutdown, which is exactly the
// F0.1 defect this project already paid for once.  `g_start_step` is a pointer to a string
// literal, so it is safe to publish as a bare atomic.
std::atomic<Status> g_start_st{Status::Ok};
std::atomic<const char*> g_start_step{nullptr};
std::atomic<uint32_t> g_start_seq{0};  // bumped on every completed attempt, so a waiter can
                                       //   tell "not tried yet" from "tried and failed"

Status fail_start(const char* step, Status st) noexcept {
    g_start_step.store(step, std::memory_order_relaxed);
    g_start_st.store(st, std::memory_order_relaxed);
    // Warn, not Debug.  A bring-up board with no sound is the case this line exists for, and
    // it must not need `sys debug drv.amp debug` to appear.
    CLK_LOGW(drv_amp, "amp start failed at %s: %s", step, clk::name(st));
    return st;
}

int16_t g_block[kBlockFrames * 2];

// ---- the port ---------------------------------------------------------------------------

bool clocks_on(bool on) noexcept;  // defined below, next to the port it drives

gpio_num_t pin_of(Pin p) noexcept {
    switch (p) {
        case Pin::Mclk:
            return kMclk;
        case Pin::Bclk:
            return kBclk;
        case Pin::Lrck:
            return kWs;
        default:
            return kDout;
    }
}

// Give the four pads back to the GPIO matrix so install_port() can re-route them.
void release_pins() noexcept {
    if (!g_pins_held) return;
    for (const gpio_num_t p : {kMclk, kBclk, kWs, kDout}) {
        (void)::gpio_set_direction(p, GPIO_MODE_INPUT);
        (void)::gpio_set_pull_mode(p, GPIO_FLOATING);
    }
    g_pins_held = false;
}

// Drop the port entirely.  `audio clk` and the pin test both need the pads back, and IDF will
// only delete a channel that is disabled -- which is what clocks_on(false) leaves it.
void teardown_port() noexcept {
    clocks_on(false);
    if (g_tx) {
        (void)::i2s_del_channel(g_tx);  // also revokes the GPIO reservations
        g_tx = nullptr;
    }
}

bool install_port() noexcept {
    release_pins();
    if (g_tx) return true;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = kDmaDescs;
    chan.dma_frame_num = kDmaFrames;
    // On an underrun the hardware repeats whatever is still in the descriptor.  A repeated
    // 5 ms of sine is a buzz that sounds like a broken amp; zeros are silence, which sounds
    // like what it is.
    chan.auto_clear_after_cb = true;
    chan.auto_clear_before_cb = true;
    if (const esp_err_t e = ::i2s_new_channel(&chan, &g_tx, nullptr); e != ESP_OK) {
        g_tx = nullptr;
        CLK_LOGE(drv_amp, "i2s_new_channel: %s", ::esp_err_to_name(e));
        return false;
    }

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRateHz),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = kMclk,
                .bclk = kBclk,
                .ws = kWs,
                .dout = kDout,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
            },
    };
    // I2S_STD_CLK_DEFAULT_CONFIG already asks for 256 x f_S; assert it rather than trust it,
    // because the amp REQUIRES an MCLK in 128-512 f_S and a silently different multiple is a
    // chip that clocks and never makes a sound (Table 6).
    static_assert(I2S_MCLK_MULTIPLE_256 == 256, "the amp wants 128-512 f_S; 256 is the target");
    cfg.clk_cfg.mclk_multiple = static_cast<i2s_mclk_multiple_t>(g_mclk_mult);
    // Slot width wider than the sample is legal and is how BCLK moves from 32 to 64 x f_S
    // without touching the 16-bit data -- the amp accepts both (Table 6) and 64 is what most
    // of the world runs, so it is the first thing to try when CLKE will not clear.
    cfg.slot_cfg.slot_bit_width =
        g_slot_bits == 32 ? I2S_SLOT_BIT_WIDTH_32BIT : I2S_SLOT_BIT_WIDTH_16BIT;

    if (const esp_err_t e = ::i2s_channel_init_std_mode(g_tx, &cfg); e != ESP_OK) {
        CLK_LOGE(drv_amp, "i2s init std: %s", ::esp_err_to_name(e));
        (void)::i2s_del_channel(g_tx);
        g_tx = nullptr;
        return false;
    }
    return true;
}

bool clocks_on(bool on) noexcept {
    if (!g_tx) return false;
    if (g_clocks.load(std::memory_order_relaxed) == on) return true;
    const esp_err_t e = on ? ::i2s_channel_enable(g_tx) : ::i2s_channel_disable(g_tx);
    if (e != ESP_OK) {
        CLK_LOGE(drv_amp, "i2s %s: %s", on ? "enable" : "disable", ::esp_err_to_name(e));
        return false;
    }
    g_clocks.store(on, std::memory_order_relaxed);
    return true;
}

// Feed one block of silence so the amp sees valid LRCK-framed data before it is unmuted.
// Cheap, and it is the difference between coming out of shutdown into a defined stream and
// coming out of it into whatever the DMA descriptors were left holding.
void prime_silence() noexcept {
    std::memset(g_block, 0, sizeof g_block);
    for (int i = 0; i < kDmaDescs; ++i) {
        std::size_t wrote = 0;
        (void)::i2s_channel_write(g_tx, g_block, sizeof g_block, &wrote, pdMS_TO_TICKS(50));
    }
}

// ---- the sequence -------------------------------------------------------------------------

// Every exit names the step, because on a silent bench the five of them are indistinguishable
// from the outside and four of them are silent by nature.
Status bring_up() noexcept {
    port::Lock seq{g_seq_mx};
    if (g_amp_up.load(std::memory_order_relaxed)) return Status::Ok;
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    if (!install_port()) return fail_start("i2s-install", Status::Failed);

    // 2. SPK_SD stays low.  It is low at POR (expander hi-Z, OLAT 0) and nothing else writes
    //    it, so this is belt-and-braces against a previous teardown that failed halfway.
    (void)expander::set(expander::Sig::SpkSd, false);

    // 4. Clocks BEFORE the control port.
    if (!clocks_on(true)) return fail_start("i2s-enable", Status::Failed);
    prime_silence();

    // 5. Configure, muted, at the volume that is actually set.
    uint8_t vol = 0;
    {
        port::Lock lk{g_mx};
        vol = g_vol_pct;
    }
    if (const Status st = tas5760m::configure(true, tas5760m::db_for_pct(vol)); st != Status::Ok) {
        clocks_on(false);
        return fail_start("cfg", st);
    }

    // 6. SPK_SD high, then let the output stage come up before it is asked to make a sound.
    if (const Status st = expander::set(expander::Sig::SpkSd, true); st != Status::Ok) {
        clocks_on(false);
        return fail_start("spk_sd", st);
    }
    ::vTaskDelay(pdMS_TO_TICKS(10));

    // 7. Unmute.
    if (const Status st = tas5760m::set_mute(false); st != Status::Ok) {
        (void)expander::set(expander::Sig::SpkSd, false);
        clocks_on(false);
        return fail_start("unmute", st);
    }
    g_amp_up.store(true, std::memory_order_relaxed);
    g_start_st.store(Status::Ok, std::memory_order_relaxed);
    g_start_step.store(nullptr, std::memory_order_relaxed);
    CLK_LOGD(drv_amp, "amp up: clocks, PBTL, vol %u%%", static_cast<unsigned>(vol));
    return Status::Ok;
}

// Caller holds g_seq_mx.  port::Mutex is not recursive, and the two bench tools below have to
// tear the amp down from inside the lock they already hold.
void bring_down_locked() noexcept {
    if (!g_amp_up.load(std::memory_order_relaxed) && !g_clocks.load(std::memory_order_relaxed))
        return;
    // §9.2.1.2.2, in order.  The 5 ms is the chip's own volume fade finishing before the
    // output stage is cut out from under it.
    (void)tas5760m::set_mute(true);
    ::vTaskDelay(pdMS_TO_TICKS(5));
    (void)expander::set(expander::Sig::SpkSd, false);
    g_amp_up.store(false, std::memory_order_relaxed);
    clocks_on(false);
    CLK_LOGD(drv_amp, "amp parked");
}

void bring_down() noexcept {
    port::Lock seq{g_seq_mx};
    bring_down_locked();
}

// ---- the writer -----------------------------------------------------------------------------

void writer(void*) noexcept {
    uint32_t idle_since = 0;
    for (;;) {
        bool start_req = false;
        bool stream_req = false;
        uint32_t hz = 0, ms = 0;
        bool stop_req = false;
        {
            port::Lock lk{g_mx};
            start_req = g_req_pending;
            stream_req = g_req_stream;
            hz = g_req_hz;
            ms = g_req_ms;
            stop_req = g_req_stop;
            g_req_pending = false;
            g_req_stop = false;
        }

        if (start_req) {
            if (bring_up() == Status::Ok) {
                port::Lock lk{g_mx};
                // The stream's mixer was started by stream_open() itself -- it has to be, so
                // that the ring is clear before `storage` writes the first sample into it.
                if (!stream_req) {
                    g_src = Src::Tone;
                    g_gen.start(hz, kRateHz, 1.0f, ms);
                }
                g_playing.store(true, std::memory_order_relaxed);
            } else {
                port::Lock lk{g_mx};
                if (stream_req) g_mix.release();  // unprimed: done at once, `storage` sees it
                g_src = Src::None;
                g_playing.store(false, std::memory_order_relaxed);
            }
            // Published LAST, so a waiter that sees the new sequence number also sees the
            // outcome that produced it.
            g_start_seq.fetch_add(1, std::memory_order_release);
        }
        if (stop_req) {
            port::Lock lk{g_mx};
            g_gen.release();
            g_mix.release();
        }

        if (!g_playing.load(std::memory_order_relaxed)) {
            const uint32_t now = clock_::millis();
            if (g_amp_up.load(std::memory_order_relaxed)) {
                if (idle_since == 0) idle_since = now;
                if (clock_::expired(now, idle_since + kIdleParkMs)) {
                    bring_down();
                    idle_since = 0;
                }
            }
            g_sig.wait_real_ms(50);
            continue;
        }
        idle_since = 0;

        bool finished = false;
        {
            port::Lock lk{g_mx};
            if (g_src == Src::Stream) {
                g_mix.fill(g_ring, g_eof.load(std::memory_order_acquire), g_block, kBlockFrames);
                finished = g_mix.done();
            } else {
                (void)g_gen.fill(g_block, kBlockFrames);
                finished = g_gen.done();
            }
            if (finished) g_src = Src::None;
        }
        std::size_t wrote = 0;
        const esp_err_t e =
            ::i2s_channel_write(g_tx, g_block, sizeof g_block, &wrote, pdMS_TO_TICKS(200));
        if (e != ESP_OK || wrote != sizeof g_block) {
            g_underruns.fetch_add(1, std::memory_order_relaxed);
        }
        if (finished) g_playing.store(false, std::memory_order_relaxed);
    }
}

bool ensure_task() noexcept {
    if (g_task_started) return true;
    void* h = nullptr;
    if (!port::thread_start({"audio", kTaskPrio, kTaskStack, 1}, &writer, nullptr, &h)) {
        CLK_LOGE(drv_amp, "audio writer task would not start");
        return false;
    }
    g_task_started = true;
    return true;
}

}  // namespace

Status enable(bool on) noexcept {
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    if (on) {
        if (!ensure_task()) return Status::Failed;
        return bring_up();
    }
    {
        port::Lock lk{g_mx};
        g_req_pending = false;
        g_gen.release();
        g_mix.release();
        g_src = Src::None;
    }
    g_playing.store(false, std::memory_order_relaxed);
    bring_down();
    return Status::Ok;
}

bool active() noexcept {
    return g_amp_up.load(std::memory_order_relaxed) && !tas5760m::shadow().muted;
}

Status set_volume_pct(uint8_t pct) noexcept {
    if (pct > 100) return Status::BadArg;
    if (pct > kMaxVolPct) return Status::Denied;
    {
        port::Lock lk{g_mx};
        g_vol_pct = pct;
    }
    // A volume change is the one register write the datasheet allows while the device is
    // running (§9.2.1.2.2's carve-out), so it does not have to wait for the writer task.
    return tas5760m::set_volume_db(tas5760m::db_for_pct(pct));
}

uint8_t volume_pct() noexcept {
    port::Lock lk{g_mx};
    return g_vol_pct;
}

Status tone(uint32_t hz, uint32_t ms) noexcept {
    if (hz < tone::kMinHz || hz > tone::kMaxHz) return Status::BadArg;
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    if (!ensure_task()) return Status::Failed;
    {
        port::Lock lk{g_mx};
        g_req_pending = true;
        g_req_stream = false;
        g_req_hz = hz;
        g_req_ms = ms;
        g_req_stop = false;
        // A tone replaces a stream.  Released rather than cut, so the ~5 ms before the writer
        // picks the request up are a fade and not a step.
        g_mix.release();
    }
    g_sig.notify();
    return Status::Ok;
}

Status stream_open(uint32_t ramp_ms) noexcept {
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    if (!ensure_task()) return Status::Failed;
    constexpr std::size_t kCap = static_cast<std::size_t>(kRateHz) * kStreamRingMs / 1000u;
    port::Lock lk{g_mx};
    if (!g_ring_buf) {
        // 192 KB.  PSRAM, and it has to be: that is two thirds of what internal RAM has left
        // with BLE up.  The writer copies out of it a block at a time into internal g_block,
        // so the DMA never sees PSRAM.
        g_ring_buf = static_cast<int16_t*>(
            ::heap_caps_malloc(kCap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!g_ring_buf) {
            CLK_LOGE(drv_amp, "no PSRAM for the %u ms stream ring",
                     static_cast<unsigned>(kStreamRingMs));
            return Status::Failed;
        }
        g_ring.attach(g_ring_buf, kCap);
    }
    g_gen.release();
    g_ring.clear();
    g_mix.start(kRateHz, ramp_ms);
    g_eof.store(false, std::memory_order_release);
    g_src = Src::Stream;
    g_req_pending = true;
    g_req_stream = true;
    g_req_stop = false;
    g_stream_seq.fetch_add(1, std::memory_order_relaxed);
    g_sig.notify();
    return Status::Ok;
}

std::size_t stream_write(const int16_t* mono, std::size_t n) noexcept {
    if (!mono || !g_ring_buf) return 0;
    return g_ring.write(mono, n);
}

std::size_t stream_space() noexcept {
    port::Lock lk{g_mx};
    if (g_src != Src::Stream || g_mix.done() || g_eof.load(std::memory_order_relaxed)) return 0;
    return g_ring.space();
}

void stream_end() noexcept { g_eof.store(true, std::memory_order_release); }

Stream stream() noexcept {
    Stream st{};
    port::Lock lk{g_mx};
    st.open = g_src == Src::Stream && !g_mix.done();
    st.primed = st.open && g_mix.primed();
    st.level = static_cast<uint32_t>(g_ring_buf ? g_ring.level() : 0u);
    st.cap = static_cast<uint32_t>(g_ring.capacity());
    st.underruns = g_mix.underruns();
    st.played = g_mix.frames_played();
    st.seq = g_stream_seq.load(std::memory_order_relaxed);
    return st;
}

uint32_t start_seq() noexcept { return g_start_seq.load(std::memory_order_acquire); }

Status stop() noexcept {
    {
        port::Lock lk{g_mx};
        g_req_pending = false;
        g_req_stop = true;
    }
    g_sig.notify();
    return Status::Ok;
}

bool playing() noexcept { return g_playing.load(std::memory_order_relaxed); }

Status pin_drive(Pin which, bool level) noexcept {
    port::Lock seq{g_seq_mx};
    bring_down_locked();
    teardown_port();
    g_pins_held = true;
    const bool all = which == Pin::All;
    for (const Pin p : {Pin::Mclk, Pin::Bclk, Pin::Lrck, Pin::Dout}) {
        if (!all && p != which) continue;
        const gpio_num_t g = pin_of(p);
        (void)::gpio_set_direction(g, GPIO_MODE_OUTPUT);
        (void)::gpio_set_level(g, level ? 1 : 0);
    }
    CLK_LOGW(drv_amp, "pin test: %s driven %s -- I2S is DOWN until the next `audio tone`",
             name(which), level ? "HIGH" : "LOW");
    return Status::Ok;
}

Status pin_release() noexcept {
    port::Lock seq{g_seq_mx};
    release_pins();
    return Status::Ok;
}

Result<Probe> probe_pins() noexcept {
    if (!g_clocks.load(std::memory_order_relaxed))
        return Result<Probe>::bad(Status::NotReady);  // nothing to look at

    constexpr gpio_num_t kPads[4] = {kMclk, kBclk, kWs, kDout};
    // Enabling the input buffer does NOT disturb the output matrix -- the peripheral keeps
    // driving the pad and we read what it is driving.  That is the whole trick.
    for (const gpio_num_t p : kPads) (void)::gpio_input_enable(p);

    Probe pr{};
    // 2000 samples per pad.  The loop runs at roughly 1 MHz, so the slowest signal here
    // (LRCK, 48 kHz) still turns over ~50 times inside the window and the fastest aliases
    // into an even mix -- either way, both levels appear unless the pad has stopped.
    constexpr uint32_t kSamples = 2000;
    pr.samples = kSamples;
    for (int i = 0; i < 4; ++i) {
        uint32_t hi = 0;
        // Interrupts left alone: a preemption lengthens the window, which can only help a
        // toggling pad show both levels.  This is a presence test, not a frequency counter.
        for (uint32_t n = 0; n < kSamples; ++n) hi += ::gpio_get_level(kPads[i]) ? 1u : 0u;
        pr.high[i] = hi;
        pr.toggling[i] = hi != 0 && hi != kSamples;
    }
    return Result<Probe>::good(pr);
}

Status set_clocking(uint16_t mclk_multiple, uint8_t slot_bits) noexcept {
    if (slot_bits != 16 && slot_bits != 32) return Status::BadArg;
    switch (mclk_multiple) {
        case 128:
        case 192:
        case 256:
        case 384:
        case 512:
            break;
        default:
            return Status::BadArg;
    }
    // MCLK must divide down to BCLK exactly, or IDF warns and the ratio the amp counts stops
    // being an integer.  BCLK is 2 slots x slot_bits per frame.
    const uint32_t bclk_mult = 2u * slot_bits;
    if (mclk_multiple % bclk_mult != 0) return Status::BadArg;

    port::Lock seq{g_seq_mx};
    bring_down_locked();
    teardown_port();  // the next tone() re-installs at the new geometry
    g_mclk_mult = mclk_multiple;
    g_slot_bits = slot_bits;
    return Status::Ok;
}

void clocking(uint16_t& mclk_multiple, uint8_t& slot_bits) noexcept {
    mclk_multiple = g_mclk_mult;
    slot_bits = g_slot_bits;
}

Result<State> state() noexcept {
    if (!board::present(board::Dev::Amp)) return Result<State>::bad(Status::NotPresent);
    State s{};
    s.clocks = g_clocks.load(std::memory_order_relaxed);
    s.playing = g_playing.load(std::memory_order_relaxed);
    s.underruns = g_underruns.load(std::memory_order_relaxed);
    {
        port::Lock lk{g_mx};
        s.vol_pct = g_vol_pct;
    }
    s.vol_db = tas5760m::db_for_pct(s.vol_pct);
    const auto sh = tas5760m::shadow();
    s.configured = sh.configured;
    s.muted = sh.muted;
    s.last_error = g_start_st.load(std::memory_order_relaxed);
    s.last_step = g_start_step.load(std::memory_order_relaxed);

    // Live, off the chip.  The shadow says what the driver wrote; these say what the part is
    // holding now, and the interesting case on a silent bench is the one where they differ --
    // a chip that went back through POR keeps ACKing and reads 0x51 at 0x06, which is BTL.
    const auto d = tas5760m::read_reg(0x02);
    const auto a = tas5760m::read_reg(0x06);
    const auto v = tas5760m::read_reg(0x04);
    const auto f = tas5760m::read_reg(0x08);
    s.regs_live = d.ok() && a.ok() && v.ok() && f.ok();
    if (s.regs_live) {
        s.reg_digital = d.v;
        s.reg_analog = a.v;
        s.reg_vol = v.v;
        s.reg_fault = f.v;
    }
    // The two expander pins, read off the chip rather than off a shadow: SPK_SD is what this
    // file drove and SPK_FAULT is what the amp is saying back, and on a bench the interesting
    // case is exactly the one where the two disagree with the software's idea of them.
    if (const auto sd = expander::get(expander::Sig::SpkSd); sd.ok()) s.sd_pin = sd.v;
    if (const auto fp = expander::get(expander::Sig::SpkFault); fp.ok()) s.fault_pin = !fp.v;
    if (g_tx && s.clocks) {
        i2s_chan_info_t info{};
        if (::i2s_channel_get_info(g_tx, &info) == ESP_OK) {
            s.mclk_hz = info.mclk_hz;
            s.bclk_hz = info.bclk_hz;
            s.sclk_hz = info.sclk_hz;
        }
    }
    return Result<State>::good(s);
}

}  // namespace clk::hal::audio
