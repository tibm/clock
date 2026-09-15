// The `audio` group -- the amp and the port, before there is an `audio` AO.  [FIRMWARE.md §9.3,
// §6.2]
//
// Same arrangement as `board`: milestone 6 is "the alarm can be loud without killing the
// driver", and the first question in it is whether the amp makes a sound at all.  That
// question does not need the AO (§6.2) -- it needs I2S and six register writes, which
// `hal::audio` now is.  These rows read the HAL directly and move behind `audio`'s Command
// surface when the AO lands; the verbs are chosen now so that move is a re-implementation.
//
// Missing on purpose, because the things under them do not exist yet: `audio play <file>`
// (needs `storage` and the PSRAM ring, §6.3) and the `audio dsp` trio (needs the firmware
// biquad + limiter).  A row that parses and then does nothing is worse than no row.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/board.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/tas5760m.hpp"
#include "clk/hal/tone.hpp"

namespace clk::cli {
namespace {

constexpr uint32_t kDefaultToneHz = 1000;
// 0 = until `audio stop`, and that is the right default for a BENCH tool.  It was 1000 ms,
// which meant `audio tone 1000` was a one-second beep that had ended -- and the amp parked
// half a second after it -- before you could type `audio status` to see anything.  A tone you
// have to end deliberately is a tone you can put a scope on.
constexpr uint32_t kDefaultToneMs = 0;
constexpr uint32_t kMaxToneMs = 30000;

// How long to wait for the writer task to actually bring the amp up before reporting.  The
// sequence is ~25 ms of I2C and vTaskDelay plus one 50 ms poll of the writer's idle wait, so
// 400 ms is generous by a factor of five; a caller that hits it has a wedged I2C bus, and
// saying THAT is better than either hanging or lying.
constexpr uint32_t kStartWaitMs = 400;

// NEXT_STEPS.md ground rule 2, and its own closing line names this group: "re-run the audit
// when `audio` and `board` land, because both are full of things the hardware can say no to."
// Four of them here, and three are indistinguishable from the outside.
Status refused(Sink& out, const char* what, Status st) {
    out.printf("%s refused: %s", what, cmd::name(st));
    switch (st) {
        case Status::NotPresent:
            out.line("  no amp fitted -- board.hpp's presence mask says so");
            out.line("  `board i2c scan` should show 0x6C; `sim present amp on` on the host");
            break;
        case Status::Denied:
            out.printf("  over the bring-up volume ceiling of %u%% (hal.hpp kMaxVolPct)",
                       static_cast<unsigned>(hal::audio::kMaxVolPct));
            out.line("  PVDD is the 5 V rail until a 15 V brick is in, and R-AUDIO-1 puts a");
            out.line("  full-scale tone at the -GB protector's trip -- which reads as a reboot");
            break;
        case Status::Failed:
            out.line("  the amp ACKed and would not take a register, or I2S would not start");
            out.line("  `audio reg 6` should read 0xD1; `sys debug drv.amp debug` for the write");
            break;
        default:
            out.line("  see `audio status`");
            break;
    }
    return st;
}

bool parse_u32(const char* s, uint32_t& out) noexcept {
    if (!s || !*s) return false;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 0);
    if (end == s || *end != '\0' || v < 0) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

Status cmd_status(Args const&, Sink& out) {
    const auto s = hal::audio::state();
    if (!s.ok()) return refused(out, "status", s.st);

    // The chain in the order a bench walks it: is there a clock, is the pin up, is the chip
    // configured, is it unmuted, is anything playing.  Four of those five can be true with no
    // sound coming out, and knowing WHICH one is false is the whole of the diagnosis.
    out.printf("amp   clocks=%s sd_pin=%s configured=%s muted=%s playing=%s",
               s.v.clocks ? "on" : "off", s.v.sd_pin ? "high" : "LOW",
               s.v.configured ? "yes" : "no", s.v.muted ? "YES" : "no", s.v.playing ? "yes" : "no");
    out.printf("vol   %u%%  = %.1f dB at the amp's ladder  (ceiling %u%%)", s.v.vol_pct,
               static_cast<double>(s.v.vol_db), hal::audio::kMaxVolPct);
    if (s.v.clocks) {
        out.printf("i2s   %u Hz  mclk %lu Hz (%lu x fs)  bclk %lu Hz",
                   static_cast<unsigned>(hal::audio::kRateHz),
                   static_cast<unsigned long>(s.v.mclk_hz),
                   static_cast<unsigned long>(s.v.mclk_hz / hal::audio::kRateHz),
                   static_cast<unsigned long>(s.v.bclk_hz));
    } else {
        out.line("i2s   down -- MCLK/BCLK/LRCK are not running");
    }
    if (s.v.underruns) out.printf("  ⚠ %lu underruns", static_cast<unsigned long>(s.v.underruns));

    if (s.v.last_error != Status::Ok) {
        out.printf("⚠ last start FAILED at step `%s`: %s", s.v.last_step ? s.v.last_step : "?",
                   cmd::name(s.v.last_error));
    } else if (!s.v.clocks) {
        out.line("      (parked is normal -- the amp comes up on demand and drops 500 ms after)");
    }

    // Live off the chip, not out of the shadow.  A part that has been back through POR keeps
    // ACKing and reads 0x51 at 0x06 -- which is BTL, both halves fighting across the speaker.
    if (s.v.regs_live) {
        const bool ok02 = s.v.reg_digital == 0x04;
        const bool ok06 = s.v.reg_analog == 0xD1;
        out.printf("regs  0x02=0x%02X%s 0x06=0x%02X%s 0x04=0x%02X 0x08=0x%02X   %s, %.1f dBV",
                   s.v.reg_digital, ok02 ? "" : "!", s.v.reg_analog, ok06 ? "" : "!", s.v.reg_vol,
                   s.v.reg_fault, (s.v.reg_analog & 0x80) ? "PBTL mono" : "BTL",
                   static_cast<double>(hal::tas5760m::kAnalogGainDbv));
        if (!ok02 || !ok06) {
            out.line("  ⚠ `!` = not what the driver wrote.  Expect 0x02=0x04 (boost +0 dB, I2S)");
            out.line("  and 0x06=0xD1 (PBTL, 19.2 dBV).  A POR value there means the chip reset");
            out.line("  under us -- 0x06 reads 0x51 at POR, which is BTL into a PBTL speaker.");
        }
    } else {
        out.line("regs  unreadable -- the amp is not answering on I2C (`board i2c scan`)");
    }

    // SPK_FAULT is a pin and reg 0x08 is the reason.  Both, because the pin can be asserted by
    // a chip that is too hot to answer I2C, and the register can latch an event whose pin has
    // already released.
    if (s.v.fault_pin) out.line("⚠ SPK_FAULT asserted (expander GPB6)");
    const auto f = hal::tas5760m::faults();
    if (f.ok() && (f.v.clk || f.v.oc || f.v.dc || f.v.ot)) {
        out.printf("⚠ reg 0x08:%s%s%s%s", f.v.clk ? " CLK" : "", f.v.oc ? " OC" : "",
                   f.v.dc ? " DC" : "", f.v.ot ? " OT" : "");
        out.line("  OC/DC/OT latch -- they clear only when SPK_SD is toggled (`audio stop`)");
        if (f.v.clk) out.line("  CLK does not latch: it means MCLK/BCLK/LRCK are wrong RIGHT NOW");
    }

    // The rail the watts are computed against, and it is the reason the ceiling is where it
    // is.  Two facts, not one: the LTC4412 gives the amp 12 V only when the boost is actually
    // ENABLED, and the boost is interlocked on PD_PG (§6.8 interlock 1).  Plugged in with the
    // boost still off is the state this whole bring-up runs in, and it looks like 12 V if you
    // only print `plugged`.
    const auto p = hal::power::read();
    const auto b = hal::expander::get(hal::expander::Sig::Boost12En);
    if (p.ok() && b.ok()) {
        const bool twelve = p.v.plugged && b.v;
        out.printf("pvdd  %s -- plugged=%d boost12=%s",
                   twelve ? "12 V boost" : "5 V rail, clipping at ~3.5 V rms = 3.1 W",
                   p.v.plugged ? 1 : 0, b.v ? "on" : "off");
    }
    return Status::Ok;
}

// tone() only queues; the amp comes up on the writer task and can fail there.  Waiting for
// the start-attempt counter to move is what turns an asynchronous HAL into an honest command
// (ground rule 2) -- without it this row printed "tone 1000 Hz at 10 %" for an amp that never
// left shutdown, which is F0.1 with a different peripheral.
Status await_start(Sink& out, uint32_t seq_before) {
    for (uint32_t waited = 0; waited < kStartWaitMs; waited += 10) {
        if (hal::audio::start_seq() != seq_before) break;
        hal::clock_::sleep_ms(10);
    }
    const auto s = hal::audio::state();
    if (!s.ok()) return refused(out, "tone", s.st);
    if (hal::audio::start_seq() == seq_before) {
        out.line("tone refused: the writer task never answered");
        out.line("  the I2C bus is most likely wedged -- `board i2c scan`, then `sys top`");
        return Status::Failed;
    }
    if (s.v.last_error != Status::Ok) {
        out.printf("tone refused at step `%s`: %s", s.v.last_step ? s.v.last_step : "?",
                   cmd::name(s.v.last_error));
        out.line("  `audio status` has the whole chain; the step names map to the datasheet's");
        out.line("  start-up order -- i2s-install / i2s-enable / cfg / spk_sd / unmute");
        return s.v.last_error;
    }
    return Status::Ok;
}

Status cmd_tone(Args const& a, Sink& out) {
    uint32_t hz = kDefaultToneHz;
    uint32_t ms = kDefaultToneMs;
    if (a.arg(0) && !parse_u32(a.arg(0), hz)) {
        out.line("usage: audio tone [<hz>] [<ms>]   (ms omitted or 0 = until `audio stop`)");
        return Status::BadArg;
    }
    if (a.arg(1) && !parse_u32(a.arg(1), ms)) {
        out.line("usage: audio tone [<hz>] [<ms>]   (ms omitted or 0 = until `audio stop`)");
        return Status::BadArg;
    }
    if (hz < hal::tone::kMinHz || hz > hal::tone::kMaxHz) {
        out.printf("hz must be %lu..%lu", static_cast<unsigned long>(hal::tone::kMinHz),
                   static_cast<unsigned long>(hal::tone::kMaxHz));
        return Status::BadArg;
    }
    if (ms > kMaxToneMs) {
        out.printf("ms must be 0..%lu -- 0 plays until `audio stop`",
                   static_cast<unsigned long>(kMaxToneMs));
        return Status::BadArg;
    }
    const uint32_t seq = hal::audio::start_seq();
    if (const Status st = hal::audio::tone(hz, ms); st != Status::Ok)
        return refused(out, "tone", st);
    if (const Status st = await_start(out, seq); st != Status::Ok) return st;
    out.printf("tone %lu Hz at %u%%, %s", static_cast<unsigned long>(hz), hal::audio::volume_pct(),
               ms ? "for the requested time" : "until `audio stop`");
    return Status::Ok;
}

Status cmd_stop(Args const&, Sink& out) {
    if (const Status st = hal::audio::stop(); st != Status::Ok) return refused(out, "stop", st);
    out.line("stopping -- the tail fades, then the amp parks itself");
    return Status::Ok;
}

Status cmd_vol(Args const& a, Sink& out) {
    if (!a.arg(0)) {
        const uint8_t pct = hal::audio::volume_pct();
        out.printf("vol %u%%  = %.1f dB  (ceiling %u%%, default %u%%)", pct,
                   static_cast<double>(hal::tas5760m::db_for_pct(pct)), hal::audio::kMaxVolPct,
                   hal::audio::kDefaultVolPct);
        out.line("  percent is AMPLITUDE: 100 % = 0 dB, 50 % = -6 dB, 10 % = -20 dB");
        return Status::Ok;
    }
    uint32_t pct = 0;
    if (!parse_u32(a.arg(0), pct) || pct > 100) {
        out.line("usage: audio vol [<0-100>]");
        return Status::BadArg;
    }
    const Status st = hal::audio::set_volume_pct(static_cast<uint8_t>(pct));
    if (st != Status::Ok) return refused(out, "vol", st);
    out.printf("vol %lu%%  = %.1f dB at the amp", static_cast<unsigned long>(pct),
               static_cast<double>(hal::tas5760m::db_for_pct(static_cast<uint8_t>(pct))));
    return Status::Ok;
}

// Unsafe for the same reason `board i2c write` is: reg 0x06 alone can take the bridge out of
// PBTL with the speaker still wired across both halves, and reg 0x01 can undo the shutdown
// that every other gate here relies on.
Status cmd_reg(Args const& a, Sink& out) {
    uint32_t reg = 0, val = 0;
    if (!parse_u32(a.arg(0), reg) || reg > 0xFF) {
        out.line("usage: audio reg <r> [<v>]");
        return Status::BadArg;
    }
    if (!a.arg(1)) {
        const auto r = hal::tas5760m::read_reg(static_cast<uint8_t>(reg));
        if (!r.ok()) return refused(out, "reg", r.st);
        out.printf("0x%02X[0x%02X] = 0x%02X", hal::tas5760m::kAddr, static_cast<unsigned>(reg),
                   r.v);
        return Status::Ok;
    }
    if (!parse_u32(a.arg(1), val) || val > 0xFF) {
        out.line("usage: audio reg <r> [<v>]");
        return Status::BadArg;
    }
    const Status st =
        hal::tas5760m::write_reg(static_cast<uint8_t>(reg), static_cast<uint8_t>(val));
    if (st != Status::Ok) return refused(out, "reg", st);
    out.printf("0x%02X[0x%02X] <- 0x%02X", hal::tas5760m::kAddr, static_cast<unsigned>(reg),
               static_cast<unsigned>(val));
    out.line("  ⚠ the next `audio tone` rewrites the whole set (§9.2.1.2.1 wants it in shutdown)");
    return Status::Ok;
}

// ---- the two clock tools ------------------------------------------------------------------
// `CLKE` in reg 0x08 is ONE bit for two completely different faults: a clock that never
// reaches the pin, and a clock whose ratios the amp rejects.  These split them, and the pin
// test deliberately needs nothing faster than a multimeter -- the real signals are 12.288 MHz
// and 1.536 MHz, which most benches cannot see.
Status cmd_pins(Args const& a, Sink& out) {
    const char* which = a.arg(0);
    if (!which) {
        out.line("usage: audio pins <mclk|bclk|lrck|dout|all> <0|1>   ·   audio pins off");
        out.line("  drives one I2S pad as a STATIC GPIO so a DMM on the amp's own pin proves");
        out.line("  the module pad, the trace and the joint in one reading:");
        out.line("    mclk -> U9 pin 14 (module 37, IO43)   bclk -> U9 pin 15 (module 18, IO10)");
        out.line("    lrck -> U9 pin 17 (module 19, IO11)   dout -> U9 pin 16 (module 20, IO12)");
        out.line("  I2S is DOWN while a pin is held; the next `audio tone` takes them back");
        return Status::BadArg;
    }
    if (std::strcmp(which, "off") == 0) {
        if (const Status st = hal::audio::pin_release(); st != Status::Ok) {
            out.line("pins refused: this backend has no pads (target only)");
            return st;
        }
        out.line("pins released -- `audio tone` will re-install the port");
        return Status::Ok;
    }
    hal::audio::Pin p{};
    if (std::strcmp(which, "mclk") == 0) {
        p = hal::audio::Pin::Mclk;
    } else if (std::strcmp(which, "bclk") == 0) {
        p = hal::audio::Pin::Bclk;
    } else if (std::strcmp(which, "lrck") == 0) {
        p = hal::audio::Pin::Lrck;
    } else if (std::strcmp(which, "dout") == 0) {
        p = hal::audio::Pin::Dout;
    } else if (std::strcmp(which, "all") == 0) {
        p = hal::audio::Pin::All;
    } else {
        out.line("usage: audio pins <mclk|bclk|lrck|dout|all> <0|1>   ·   audio pins off");
        return Status::BadArg;
    }
    uint32_t level = 0;
    if (!parse_u32(a.arg(1), level) || level > 1) {
        out.line("usage: audio pins <mclk|bclk|lrck|dout|all> <0|1>");
        return Status::BadArg;
    }
    if (const Status st = hal::audio::pin_drive(p, level != 0); st != Status::Ok) {
        out.line("pins refused: this backend has no pads (target only)");
        return st;
    }
    out.printf("%s driven %s -- I2S is DOWN; measure at the amp, not at the module",
               hal::audio::name(p), level ? "HIGH (3.3 V)" : "LOW (0 V)");
    out.line("  a pad that reads 0 V when driven HIGH is a broken trace or a cold joint");
    return Status::Ok;
}

Status cmd_clk(Args const& a, Sink& out) {
    uint16_t mult = 0;
    uint8_t bits = 0;
    if (!a.arg(0)) {
        hal::audio::clocking(mult, bits);
        out.printf("clk   mclk %u x fs = %lu Hz   bclk %u x fs = %lu Hz   %u-bit slots", mult,
                   static_cast<unsigned long>(hal::audio::kRateHz) * mult, 2u * bits,
                   static_cast<unsigned long>(hal::audio::kRateHz) * 2u * bits, bits);
        out.line("  the amp REQUIRES mclk in 128-512 x fs and accepts bclk 32/48/64 (Table 6)");
        out.line("  `audio clk 256 32` = the usual 64 x fs bclk, which is what most codecs see");
        return Status::Ok;
    }
    uint32_t m = 0, b = 32;
    if (!parse_u32(a.arg(0), m) || (a.arg(1) && !parse_u32(a.arg(1), b))) {
        out.line("usage: audio clk [<mclk_mult>] [<slot_bits>]   e.g. `audio clk 256 32`");
        return Status::BadArg;
    }
    const Status st = hal::audio::set_clocking(static_cast<uint16_t>(m), static_cast<uint8_t>(b));
    if (st != Status::Ok) {
        out.line("clk refused: mclk_mult must be 128|192|256|384|512, slot_bits 16|32,");
        out.line("  and mclk_mult must divide by 2 x slot_bits exactly");
        return st;
    }
    hal::audio::clocking(mult, bits);
    out.printf("clk   mclk %u x fs   bclk %u x fs   %u-bit slots -- takes effect on the next tone",
               mult, 2u * bits, bits);
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"audio", nullptr, "status", "", "clocks, pin, registers, faults", ReleaseOk, cmd_status},
    {"audio", nullptr, "tone", "[<hz>] [<ms>]", "a generated sine; no <ms> = until stop", None,
     cmd_tone},
    {"audio", nullptr, "stop", "", "fade the tone out and park the amp", ReleaseOk, cmd_stop},
    {"audio", nullptr, "vol", "[<0-100>]", "amplitude percent; refuses over the ceiling", None,
     cmd_vol},
    {"audio", nullptr, "reg", "<r> [<v>]", "one TAS5760M register -- drives real pins", Unsafe,
     cmd_reg},
    {"audio", nullptr, "pins", "<name> <0|1> | off", "hold one I2S pad -- a DMM trace test", Unsafe,
     cmd_pins},
    {"audio", nullptr, "clk", "[<mclk_mult>] [<slot_bits>]", "sweep the I2S clock geometry", None,
     cmd_clk},
};

}  // namespace

extern const CmdTable kTableAudio{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
