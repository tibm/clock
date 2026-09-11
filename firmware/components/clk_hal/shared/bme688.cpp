// BME688 temperature / humidity / pressure / gas.  One copy, both backends.
// [FIRMWARE.md §6.5, §11.2 · datasheet BST-BME688-DS000-03 rev 1.3]
//
// This driver answers what the SILICON measured and stops there.  There is no IAQ index in
// it and there must not be: an index is Bosch's BSEC blob (license-gated, not vendored --
// §6.5 and §13 open question 3) or a gas-resistance baseline algorithm, and either one is a
// judgement about the room built on top of this reading.  `gas_ohms` is the measurement;
// high is clean air, low is something volatile, and the mapping between them belongs to a
// layer that can hold state across days.
//
// Every register address, coefficient signedness and formula below is cited to its table in
// the datasheet, because a transcription slip here does not crash -- it reads 6 °C in a
// 21 °C room and looks like a sensor fault for a week.
#include "clk/hal/bme688.hpp"

#include "clk/board.hpp"
#include "clk/log.hpp"

namespace clk::hal::bme688 {
namespace {

enum Reg : uint8_t {
    kCoeff3 = 0x00,  // res_heat_val 0x00, res_heat_range 0x02<5:4>, range_sw_err 0x04<7:4>
    kField0 = 0x1D,  // meas_status .. gas_r_lsb, one 17-byte block
    kResHeat0 = 0x5A,
    kGasWait0 = 0x64,
    kCtrlGas0 = 0x70,
    kCtrlGas1 = 0x71,
    kCtrlHum = 0x72,
    kCtrlMeas = 0x74,
    kConfig = 0x75,
    kCoeff1 = 0x8A,  // 23 bytes, 0x8A..0xA0
    kChipId = 0xD0,
    kReset = 0xE0,
    kCoeff2 = 0xE1,  // 14 bytes, 0xE1..0xEE
};

constexpr uint8_t kChipIdValue = 0x61;  // datasheet §5.3, Chip_Id row
constexpr uint8_t kSoftReset = 0xB6;
constexpr uint8_t kLenCoeff1 = 23, kLenCoeff2 = 14, kLenCoeff3 = 5;
constexpr uint8_t kLenField = 17;  // 0x1D..0x2D inclusive

// Oversampling codes (§5.3.3): 0 = skipped, 1..5 = 1x, 2x, 4x, 8x, 16x.
constexpr uint8_t kOsT = 2;  // 2x
constexpr uint8_t kOsP = 5;  // 16x
constexpr uint8_t kOsH = 1;  // 1x
// IIR filter OFF.  The filter exists to damp the pressure spikes a door slam makes in a
// CONTINUOUS measurement; in forced mode each conversion stands alone and every sample would
// carry a fraction of a reading taken minutes ago, which for a once-a-second bench command is
// staleness with no benefit.
constexpr uint8_t kFilter = 0;

// One heater set-point, profile 0.  320 °C for ~150 ms is Bosch's ordinary VOC operating
// point and the datasheet notes 20-30 ms is enough for the plate to reach temperature
// (§3.6.5), so 150 ms is soak, not ramp.
constexpr uint16_t kHeatTargetC = 320;
constexpr uint16_t kHeatMs = 150;

// The ambient temperature that res_heat_0 is computed against.  Fixed at 25 °C rather than
// fed back from the last reading, and the arithmetic says why: in the §3.6.5 formula the
// ambient enters only as par_g3/1024 * amb, which for a typical par_g3 of ~18 is 0.44
// against a var4 term of ~85 at 320 °C.  A bedroom moving between 15 and 30 °C therefore
// shifts the heater code by well under one percent, and paying an extra register write per
// reading to chase it would be precision theatre.
constexpr int8_t kAmbC = 25;

struct Calib {
    uint16_t t1;
    int16_t t2;
    int8_t t3;
    uint16_t p1;
    int16_t p2;
    int8_t p3;
    int16_t p4, p5;
    int8_t p6, p7;
    int16_t p8, p9;
    uint8_t p10;
    uint16_t h1, h2;
    int8_t h3, h4, h5;
    uint8_t h6;
    int8_t h7;
    int8_t g1;
    int16_t g2;
    int8_t g3;
    uint8_t res_heat_range;
    int8_t res_heat_val;
};

bool g_ready = false;
Calib g_cal{};
uint32_t g_meas_ms = 0;  // computed once from the oversampling above + the heater soak

// The two-byte reads are all little-endian on this part (the tables list LSB first), and the
// SIGNEDNESS is per-coefficient and not guessable from the width -- par_t1 is unsigned while
// par_t2 is signed, par_p10 is unsigned while par_p3 is signed.  Getting one wrong shifts a
// reading by a plausible-looking amount, so each is named at its use site below.
constexpr uint16_t u16le(uint8_t lo, uint8_t hi) noexcept {
    return static_cast<uint16_t>(static_cast<unsigned>(hi) << 8 | lo);
}
constexpr int16_t s16le(uint8_t lo, uint8_t hi) noexcept {
    return static_cast<int16_t>(u16le(lo, hi));
}

Status write(uint8_t reg, uint8_t val) noexcept { return i2c::write_reg(kAddr, reg, val); }

// gas_wait_x is a 6-bit value times a 2-bit multiplier (x1/x4/x16/x64), so 4032 ms is the
// ceiling and anything above it saturates rather than wrapping into a 1 ms soak.  (§3.6.5.)
constexpr uint8_t gas_wait_code(uint16_t ms) noexcept {
    if (ms >= 0xFC0) return 0xFF;
    uint8_t factor = 0;
    while (ms > 0x3F) {
        ms = static_cast<uint16_t>(ms / 4);
        ++factor;
    }
    return static_cast<uint8_t>(ms | static_cast<uint8_t>(factor << 6));
}

// §3.6.5, floating-point form.  The heater loop works on the plate's RESISTANCE, so a target
// in degrees has to be turned into a device-specific register code through three gas
// calibration coefficients plus two trim values that live outside the coefficient blocks.
uint8_t res_heat_code(int16_t target_c, int8_t amb_c) noexcept {
    const double var1 = (static_cast<double>(g_cal.g1) / 16.0) + 49.0;
    const double var2 = ((static_cast<double>(g_cal.g2) / 32768.0) * 0.0005) + 0.00235;
    const double var3 = static_cast<double>(g_cal.g3) / 1024.0;
    const double var4 = var1 * (1.0 + (var2 * static_cast<double>(target_c)));
    const double var5 = var4 + (var3 * static_cast<double>(amb_c));
    const double code = 3.4 * ((var5 * (4.0 / (4.0 + static_cast<double>(g_cal.res_heat_range))) *
                                (1.0 / (1.0 + (static_cast<double>(g_cal.res_heat_val) * 0.002)))) -
                               25.0);
    if (code < 0.0) return 0;
    if (code > 255.0) return 255;
    return static_cast<uint8_t>(code);
}

// Bosch's own bme68x_get_meas_dur(), because the datasheet publishes the measurement phases
// (Figure 2) but no closed form for their total.  1963 us per oversampling cycle, four TPH
// switching slots and five gas slots at 477 us, a 500 us rounding nudge and 1 ms of wake --
// then the heater soak on top, which is the part that dominates.
uint32_t meas_duration_ms() noexcept {
    constexpr uint8_t kCycles[] = {0, 1, 2, 4, 8, 16};
    const uint32_t cycles = kCycles[kOsT] + kCycles[kOsP] + kCycles[kOsH];
    uint32_t us = cycles * 1963u + 477u * 4u + 477u * 5u + 500u;
    return (us / 1000u) + 1u + kHeatMs;
}

Status read_calibration() noexcept {
    uint8_t c1[kLenCoeff1]{}, c2[kLenCoeff2]{}, c3[kLenCoeff3]{};
    Status st = i2c::read_regs(kAddr, kCoeff1, c1, sizeof c1);
    if (st == Status::Ok) st = i2c::read_regs(kAddr, kCoeff2, c2, sizeof c2);
    if (st == Status::Ok) st = i2c::read_regs(kAddr, kCoeff3, c3, sizeof c3);
    if (st != Status::Ok) return st;

    // Three bursts, not forty-one byte reads, and the block base is subtracted at each use so
    // the constants below are the datasheet's own addresses (Tables 13-16) and can be checked
    // against it line by line.
    auto b1 = [&c1](uint8_t a) { return c1[a - kCoeff1]; };
    auto b2 = [&c2](uint8_t a) { return c2[a - kCoeff2]; };

    g_cal.t1 = u16le(b2(0xE9), b2(0xEA));  // UNSIGNED -- t2/t3 below are not
    g_cal.t2 = s16le(b1(0x8A), b1(0x8B));
    g_cal.t3 = static_cast<int8_t>(b1(0x8C));

    g_cal.p1 = u16le(b1(0x8E), b1(0x8F));  // unsigned
    g_cal.p2 = s16le(b1(0x90), b1(0x91));
    g_cal.p3 = static_cast<int8_t>(b1(0x92));
    g_cal.p4 = s16le(b1(0x94), b1(0x95));
    g_cal.p5 = s16le(b1(0x96), b1(0x97));
    g_cal.p6 = static_cast<int8_t>(b1(0x99));  // note the order: p7 is at 0x98, p6 at 0x99
    g_cal.p7 = static_cast<int8_t>(b1(0x98));
    g_cal.p8 = s16le(b1(0x9C), b1(0x9D));
    g_cal.p9 = s16le(b1(0x9E), b1(0x9F));
    g_cal.p10 = b1(0xA0);  // unsigned, unlike every other single-byte p coefficient

    // h1 and h2 SHARE the nibbles of 0xE2, and they share them the awkward way round: h1
    // takes the low nibble with 0xE3 above it, h2 takes the high nibble with 0xE1 above it.
    // Both are 12-bit unsigned.  This is the single most-transcribed-wrong pair on the part.
    g_cal.h1 = static_cast<uint16_t>((static_cast<unsigned>(b2(0xE3)) << 4) | (b2(0xE2) & 0x0F));
    g_cal.h2 = static_cast<uint16_t>((static_cast<unsigned>(b2(0xE1)) << 4) | (b2(0xE2) >> 4));
    g_cal.h3 = static_cast<int8_t>(b2(0xE4));
    g_cal.h4 = static_cast<int8_t>(b2(0xE5));
    g_cal.h5 = static_cast<int8_t>(b2(0xE6));
    g_cal.h6 = b2(0xE7);  // unsigned; h7 either side of it is signed
    g_cal.h7 = static_cast<int8_t>(b2(0xE8));

    g_cal.g1 = static_cast<int8_t>(b2(0xED));
    g_cal.g2 = s16le(b2(0xEB), b2(0xEC));
    g_cal.g3 = static_cast<int8_t>(b2(0xEE));

    // The heater trim lives at the very bottom of the map, in the block nothing else uses.
    g_cal.res_heat_range = static_cast<uint8_t>((c3[0x02 - kCoeff3] >> 4) & 0x03);
    g_cal.res_heat_val = static_cast<int8_t>(c3[0x00 - kCoeff3]);
    return Status::Ok;
}

// §3.5.1 floating point.  t_fine is carried out because pressure and humidity both need the
// un-rounded temperature, which is the whole reason the three formulas cannot be reordered.
double comp_temp(uint32_t adc, double& t_fine) noexcept {
    const double a = static_cast<double>(adc);
    const double var1 =
        ((a / 16384.0) - (static_cast<double>(g_cal.t1) / 1024.0)) * static_cast<double>(g_cal.t2);
    const double d = (a / 131072.0) - (static_cast<double>(g_cal.t1) / 8192.0);
    const double var2 = d * d * static_cast<double>(g_cal.t3) * 16.0;
    t_fine = var1 + var2;
    return t_fine / 5120.0;
}

// §3.5.2 floating point, in Pascal.  The datasheet's printed listing has three typos in it
// (`var1_p`, `var2_p`, `var3_p` for `var1`, `var2`, `var3`); this is the same arithmetic with
// the names resolved, and it matches Bosch's published API line for line.
double comp_press(uint32_t adc, double t_fine) noexcept {
    double var1 = (t_fine / 2.0) - 64000.0;
    double var2 = var1 * var1 * (static_cast<double>(g_cal.p6) / 131072.0);
    var2 = var2 + (var1 * static_cast<double>(g_cal.p5) * 2.0);
    var2 = (var2 / 4.0) + (static_cast<double>(g_cal.p4) * 65536.0);
    var1 = (((static_cast<double>(g_cal.p3) * var1 * var1) / 16384.0) +
            (static_cast<double>(g_cal.p2) * var1)) /
           524288.0;
    var1 = (1.0 + (var1 / 32768.0)) * static_cast<double>(g_cal.p1);
    double p = 1048576.0 - static_cast<double>(adc);
    // var1 is a divisor built entirely from calibration; a zero here means the coefficients
    // are garbage (an unprogrammed part, or a burst read that came back short), and dividing
    // by it would produce an infinity that propagates all the way to the CLI as "inf hPa".
    if (static_cast<int>(var1) == 0) return 0.0;
    p = ((p - (var2 / 4096.0)) * 6250.0) / var1;
    const double v1 = (static_cast<double>(g_cal.p9) * p * p) / 2147483648.0;
    const double v2 = p * (static_cast<double>(g_cal.p8) / 32768.0);
    const double v3 =
        (p / 256.0) * (p / 256.0) * (p / 256.0) * (static_cast<double>(g_cal.p10) / 131072.0);
    return p + (v1 + v2 + v3 + (static_cast<double>(g_cal.p7) * 128.0)) / 16.0;
}

// §3.5.3 floating point, in %RH.  Temperature-dependent, which is why it takes the compensated
// temperature rather than t_fine.
double comp_hum(uint16_t adc, double temp_c) noexcept {
    const double var1 =
        static_cast<double>(adc) -
        ((static_cast<double>(g_cal.h1) * 16.0) + ((static_cast<double>(g_cal.h3) / 2.0) * temp_c));
    const double var2 = var1 * ((static_cast<double>(g_cal.h2) / 262144.0) *
                                (1.0 + ((static_cast<double>(g_cal.h4) / 16384.0) * temp_c) +
                                 ((static_cast<double>(g_cal.h5) / 1048576.0) * temp_c * temp_c)));
    const double var3 = static_cast<double>(g_cal.h6) / 16384.0;
    const double var4 = static_cast<double>(g_cal.h7) / 2097152.0;
    return var2 + ((var3 + (var4 * temp_c)) * var2 * var2);
}

// §3.7, floating point.  Note this one needs no calibration at all -- the range code selects
// a reference and the ADC reading divides into it.
uint32_t comp_gas(uint16_t gas_adc, uint8_t gas_range) noexcept {
    const uint32_t var1 = 262144u >> (gas_range & 0x0F);
    int32_t var2 = static_cast<int32_t>(gas_adc) - 512;
    var2 *= 3;
    var2 = 4096 + var2;
    if (var2 <= 0) return 0;
    const double r = 1000000.0 * static_cast<double>(var1) / static_cast<double>(var2);
    return r > 4294967000.0 ? 4294967295u : static_cast<uint32_t>(r);
}

}  // namespace

Status init() noexcept {
    if (g_ready) return Status::Ok;
    if (!board::present(board::Dev::Env)) return Status::NotPresent;

    const auto id = i2c::read_reg(kAddr, kChipId);
    if (!id.ok()) return id.st;
    if (id.v != kChipIdValue) {
        CLK_LOGW(drv_env, "0x%02X answers, chip_id reads 0x%02X, expected 0x%02X", kAddr, id.v,
                 kChipIdValue);
        return Status::Failed;
    }

    // Reset before configuring.  This driver's init is idempotent and a warm restart is the
    // normal case (`sys reboot` leaves the part powered), so starting from a known map beats
    // inheriting whatever the previous image left in ctrl_gas_1.
    if (const Status st = write(kReset, kSoftReset); st != Status::Ok) return st;
    clock_::sleep_ms(10);  // datasheet start-up time is 2 ms; 10 is free and unambiguous

    if (const Status st = read_calibration(); st != Status::Ok) return st;

    // §3.6.1's order is a hardware requirement, not a preference: osrs_h is in a different
    // register from osrs_t/osrs_p, and ctrl_meas latches all of its fields on write -- so
    // humidity goes first and the mode bits go last, in read().
    Status st = write(kCtrlHum, kOsH);
    if (st == Status::Ok) st = write(kConfig, static_cast<uint8_t>(kFilter << 2));
    if (st == Status::Ok) st = write(kGasWait0, gas_wait_code(kHeatMs));
    if (st == Status::Ok) st = write(kResHeat0, res_heat_code(kHeatTargetC, kAmbC));
    if (st == Status::Ok) st = write(kCtrlGas0, 0x00);  // heat_off = 0: the heater may run
    // ⚠ run_gas is BIT 5 on the BME688 (§5.3.4.7, "run_gas<5>"), not bit 4.  It was bit 4 on
    // the BME680 and that is the value in a great deal of code written for the older part.
    // Getting it wrong does not fail: the measurement completes, T/RH/P are all correct, and
    // the gas conversion simply never runs -- so `gas_valid_r` reads 0 and the resistance
    // pegs at the top of its range.  Which is exactly what the bench saw on 2026-09-10:
    // `t=27.05C rh=49.0% p=1011.1hPa gas=6400000ohm (gas invalid)`.  nb_conv = 0 selects
    // heater profile 0, the pair programmed just above.
    if (st == Status::Ok) st = write(kCtrlGas1, 0x20);
    if (st == Status::Ok) {
        st = write(kCtrlMeas, static_cast<uint8_t>((kOsT << 5) | (kOsP << 2)));  // mode = sleep
    }
    if (st != Status::Ok) {
        if (st != Status::NotPresent) CLK_LOGW(drv_env, "config: %s", clk::name(st));
        return st;
    }

    g_meas_ms = meas_duration_ms();
    g_ready = true;
    CLK_LOGI(drv_env, "BME688 at 0x%02X: id 0x%02X, T%ux P%ux H%ux, heater %u C/%u ms, %lu ms/meas",
             kAddr, id.v, 1u << (kOsT - 1), 1u << (kOsP - 1), 1u << (kOsH - 1), kHeatTargetC,
             kHeatMs, static_cast<unsigned long>(g_meas_ms));
    return Status::Ok;
}

Result<env::State> read() noexcept {
    if (const Status st = init(); st != Status::Ok) return Result<env::State>::bad(st);

    // ⚠ This BLOCKS for the whole conversion -- about 200 ms with the heater soak above.
    // That is why the `sensor env` row is capped at 1 Hz and why nothing may call this from
    // an ISR, a timer callback or a fast AO tick.  Forced mode is the right shape for a
    // reading nobody needs more than once a minute: the part sleeps between conversions and
    // the heater is only ever on while we are waiting for it.
    const uint8_t meas = static_cast<uint8_t>((kOsT << 5) | (kOsP << 2) | 0x01);  // mode = forced
    if (const Status st = write(kCtrlMeas, meas); st != Status::Ok) {
        return Result<env::State>::bad(st);
    }
    clock_::sleep_ms(g_meas_ms);

    // Then poll new_data rather than trusting the sleep.  The duration above is computed from
    // Bosch's own model of the part's phases, but the heater soak is timed by the same
    // internal oscillator as everything else and the datasheet gives it no tolerance -- so
    // the sleep is the estimate and this bit is the fact.
    uint8_t b[kLenField]{};
    const uint32_t deadline = clock_::millis() + g_meas_ms;
    for (;;) {
        const Status st = i2c::read_regs(kAddr, kField0, b, sizeof b);
        if (st != Status::Ok) return Result<env::State>::bad(st);
        if (b[0] & 0x80) break;  // new_data
        if (clock_::expired(clock_::millis(), deadline)) {
            return Result<env::State>::bad(Status::NotReady);
        }
        clock_::sleep_ms(5);
    }

    // Field 0 spans 0x1D..0x2D, so every offset below is (address - 0x1D).  Temperature and
    // pressure are 20-bit across three registers with the low nibble in the xlsb's TOP four
    // bits; humidity is a plain 16-bit pair; the gas ADC is 10 bits with its low two in the
    // top of gas_r_lsb, which also carries the two status flags and the range code.
    const uint32_t press_adc = (static_cast<uint32_t>(b[2]) << 12) |
                               (static_cast<uint32_t>(b[3]) << 4) |
                               (static_cast<uint32_t>(b[4]) >> 4);
    const uint32_t temp_adc = (static_cast<uint32_t>(b[5]) << 12) |
                              (static_cast<uint32_t>(b[6]) << 4) |
                              (static_cast<uint32_t>(b[7]) >> 4);
    const auto hum_adc = static_cast<uint16_t>((static_cast<unsigned>(b[8]) << 8) | b[9]);
    const auto gas_adc = static_cast<uint16_t>((static_cast<unsigned>(b[15]) << 2) | (b[16] >> 6));
    const uint8_t gas_lsb = b[16];

    double t_fine = 0.0;
    env::State s{};
    const double t = comp_temp(temp_adc, t_fine);
    s.temp_c = static_cast<float>(t);
    s.press_hpa = static_cast<float>(comp_press(press_adc, t_fine) / 100.0);  // Pa -> hPa
    s.rh_pct = static_cast<float>(comp_hum(hum_adc, t));
    s.gas_ohms = comp_gas(gas_adc, static_cast<uint8_t>(gas_lsb & 0x0F));
    // These two are part of the measurement, not metadata about it.  heat_stab_r low means
    // the plate never reached 320 °C -- the soak was short or the target unreachable -- and a
    // gas resistance taken then is a number that measures nothing.  Reporting it without the
    // flag is how a "the air quality reading drifts" bug gets built.
    s.gas_valid = (gas_lsb & 0x20) != 0;
    s.heat_stable = (gas_lsb & 0x10) != 0;
    return Result<env::State>::good(s);
}

void forget() noexcept {
    g_ready = false;
    g_cal = Calib{};
    g_meas_ms = 0;
}

}  // namespace clk::hal::bme688
