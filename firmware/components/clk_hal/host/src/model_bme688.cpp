// BME688 register model, behind the fake hal::i2c.               [FIRMWARE.md §11.2, §13.9]
//
// The BME688 is worth modelling for exactly one reason: its readings are not registers, they
// are forty-one calibration coefficients and three pages of arithmetic, and that arithmetic
// is right on paper and wrong in C more often than any other part of a driver.  Every way of
// getting it wrong -- a coefficient read as signed that is unsigned, the h1/h2 nibbles the
// wrong way round, the 20-bit ADC assembled with the xlsb in the low nibble -- produces a
// number that is plausible, stable, and off.  Nothing but a round trip catches that.
//
// So this model holds a realistic calibration set, and turns the SCENE (real degrees, real
// hPa, real %RH) into the raw ADC words that the datasheet's own formulas would compensate
// back to it.  It does that by BISECTING its own forward implementation, which is written
// from the datasheet independently of the driver's -- two transcriptions of the same tables
// that have to agree, rather than one checked against itself.
//
// NOT modelled, deliberately (§13.9 item 9 -- the firmware's decision surface, never the
// device's physics): the heater's thermal behaviour, any relationship between the scene's gas
// resistance and its temperature or humidity, the IIR filter, parallel mode, the second and
// third measurement fields, and conversion TIME -- a forced-mode trigger fills the data
// registers and raises new_data immediately, so the driver's own wait is still what gates it
// on silicon and a test does not spend 200 ms per reading.
#include "clk/hal/host/models.hpp"

namespace clk::hal::host::model {
namespace {

// A plausible part.  These are coefficient VALUES, encoded into register bytes below rather
// than typed as bytes, because the encoding is half of what the driver has to get right and
// hand-writing the bytes would bake the driver's own view of them into the model.
constexpr uint16_t kT1 = 26126;
constexpr int16_t kT2 = 26436;
constexpr int8_t kT3 = 3;
constexpr uint16_t kP1 = 36406;
constexpr int16_t kP2 = -10513;
constexpr int8_t kP3 = 88;
constexpr int16_t kP4 = 7268;
constexpr int16_t kP5 = -117;
constexpr int8_t kP6 = 30;
constexpr int8_t kP7 = 24;
constexpr int16_t kP8 = -1218;
constexpr int16_t kP9 = -2799;
constexpr uint8_t kP10 = 30;
constexpr uint16_t kH1 = 754;   // 12-bit, low nibble of 0xE2 + 0xE3
constexpr uint16_t kH2 = 1017;  // 12-bit, high nibble of 0xE2 + 0xE1
constexpr int8_t kH3 = 0;
constexpr int8_t kH4 = 45;
constexpr int8_t kH5 = 20;
constexpr uint8_t kH6 = 120;
constexpr int8_t kH7 = -100;
constexpr int8_t kG1 = -30;
constexpr int16_t kG2 = -11350;
constexpr int8_t kG3 = 18;
constexpr uint8_t kResHeatRange = 1;
constexpr int8_t kResHeatVal = 45;

constexpr std::size_t kRegSpace = 256;
constexpr uint8_t kChipId = 0xD0;
constexpr uint8_t kReset = 0xE0;
constexpr uint8_t kCtrlMeas = 0x74;
constexpr uint8_t kField0 = 0x1D;

uint8_t g_reg[kRegSpace]{};
bool g_por_done = false;

float g_t = 21.5f, g_rh = 44.0f, g_p = 1013.2f;
uint32_t g_gas = 120000;

// ---- the datasheet's forward arithmetic, transcribed here a second time -----------------

double fwd_temp(uint32_t adc, double& t_fine) noexcept {
    const double a = static_cast<double>(adc);
    const double var1 = ((a / 16384.0) - (static_cast<double>(kT1) / 1024.0)) * kT2;
    const double d = (a / 131072.0) - (static_cast<double>(kT1) / 8192.0);
    t_fine = var1 + (d * d * static_cast<double>(kT3) * 16.0);
    return t_fine / 5120.0;
}

double fwd_press(uint32_t adc, double t_fine) noexcept {
    double var1 = (t_fine / 2.0) - 64000.0;
    double var2 = var1 * var1 * (static_cast<double>(kP6) / 131072.0);
    var2 = var2 + (var1 * static_cast<double>(kP5) * 2.0);
    var2 = (var2 / 4.0) + (static_cast<double>(kP4) * 65536.0);
    var1 =
        (((static_cast<double>(kP3) * var1 * var1) / 16384.0) + (static_cast<double>(kP2) * var1)) /
        524288.0;
    var1 = (1.0 + (var1 / 32768.0)) * static_cast<double>(kP1);
    if (static_cast<int>(var1) == 0) return 0.0;
    double p = 1048576.0 - static_cast<double>(adc);
    p = ((p - (var2 / 4096.0)) * 6250.0) / var1;
    const double v1 = (static_cast<double>(kP9) * p * p) / 2147483648.0;
    const double v2 = p * (static_cast<double>(kP8) / 32768.0);
    const double v3 =
        (p / 256.0) * (p / 256.0) * (p / 256.0) * (static_cast<double>(kP10) / 131072.0);
    return p + (v1 + v2 + v3 + (static_cast<double>(kP7) * 128.0)) / 16.0;
}

double fwd_hum(uint32_t adc, double temp_c) noexcept {
    const double var1 = static_cast<double>(adc) - ((static_cast<double>(kH1) * 16.0) +
                                                    ((static_cast<double>(kH3) / 2.0) * temp_c));
    const double var2 = var1 * ((static_cast<double>(kH2) / 262144.0) *
                                (1.0 + ((static_cast<double>(kH4) / 16384.0) * temp_c) +
                                 ((static_cast<double>(kH5) / 1048576.0) * temp_c * temp_c)));
    const double var3 = static_cast<double>(kH6) / 16384.0;
    const double var4 = static_cast<double>(kH7) / 2097152.0;
    return var2 + ((var3 + (var4 * temp_c)) * var2 * var2);
}

// Fifty halvings of a 20-bit range is far more than it needs; the loop is fixed-count rather
// than convergence-tested so that a non-monotone or unreachable target terminates instead of
// spinning, and the caller gets the closest achievable word rather than a hang.
template <class F>
uint32_t bisect(uint32_t lo, uint32_t hi, double target, bool rising, F&& f) noexcept {
    for (int i = 0; i < 40 && lo < hi; ++i) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const bool below = rising ? (f(mid) < target) : (f(mid) > target);
        if (below) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// ---- scene -> registers ------------------------------------------------------------------

void store20(uint8_t msb_addr, uint32_t v) noexcept {
    g_reg[msb_addr] = static_cast<uint8_t>((v >> 12) & 0xFF);
    g_reg[msb_addr + 1] = static_cast<uint8_t>((v >> 4) & 0xFF);
    g_reg[msb_addr + 2] = static_cast<uint8_t>((v & 0x0F) << 4);
}

void convert() noexcept {
    // Temperature first, because t_fine feeds pressure and the compensated temperature feeds
    // humidity.  That ordering is the datasheet's and it is not an optimisation -- the other
    // two formulas have no meaning without it.
    const uint32_t t_adc = bisect(0, 0xFFFFF, static_cast<double>(g_t), true, [](uint32_t a) {
        double tf = 0.0;
        return fwd_temp(a, tf);
    });
    double t_fine = 0.0;
    const double t_c = fwd_temp(t_adc, t_fine);

    // Pressure DECREASES as the ADC word increases (press_comp starts life as 1048576 - adc),
    // so the bisection runs the other way round.  A model that assumed both were rising would
    // return an endpoint and every pressure test would compare two constants.
    const uint32_t p_adc = bisect(0, 0xFFFFF, static_cast<double>(g_p) * 100.0, false,
                                  [t_fine](uint32_t a) { return fwd_press(a, t_fine); });

    const uint32_t h_adc = bisect(0, 0xFFFF, static_cast<double>(g_rh), true,
                                  [t_c](uint32_t a) { return fwd_hum(a, t_c); });

    store20(0x1F, p_adc);  // press_msb / lsb / xlsb
    store20(0x22, t_adc);  // temp_msb  / lsb / xlsb
    g_reg[0x25] = static_cast<uint8_t>((h_adc >> 8) & 0xFF);
    g_reg[0x26] = static_cast<uint8_t>(h_adc & 0xFF);

    // Gas has no calibration and a closed-form inverse, but it DOES have a range code, and
    // picking it is the interesting part: gas_adc is ten bits, so only one or two of the
    // sixteen ranges put a given resistance inside them.  Walking the ranges is what the real
    // part's own autoranging does, and it is what makes the driver's `262144 >> gas_range`
    // matter rather than being a constant it could get away with ignoring.
    uint8_t range = 0;
    uint16_t gas_adc = 0;
    const double want = g_gas > 0 ? static_cast<double>(g_gas) : 1.0;
    for (uint8_t r = 0; r < 16; ++r) {
        const double var1 = static_cast<double>(262144u >> r);
        const double var2 = 1000000.0 * var1 / want;  // = 4096 + 3*(adc-512)
        const double adc = 512.0 + (var2 - 4096.0) / 3.0;
        if (adc >= 0.0 && adc <= 1023.0) {
            range = r;
            gas_adc = static_cast<uint16_t>(adc);
            break;
        }
    }
    g_reg[0x2C] = static_cast<uint8_t>((gas_adc >> 2) & 0xFF);
    // gas_r_lsb: gas_r<1:0> in 7:6, gas_valid_r bit 5, heat_stab_r bit 4, gas_range_r in 3:0.
    g_reg[0x2D] = static_cast<uint8_t>(((gas_adc & 0x03) << 6) | 0x20 | 0x10 | (range & 0x0F));
}

void por() noexcept {
    for (auto& r : g_reg) r = 0;
    g_reg[kChipId] = 0x61;

    // Calibration block 1: 0x8A..0xA0.  Note 0x98 is par_p7 and 0x99 is par_p6 -- the pair is
    // out of numerical order in the memory map, which is one of the two places a careful
    // transcription still goes wrong.
    g_reg[0x8A] = static_cast<uint8_t>(kT2 & 0xFF);
    g_reg[0x8B] = static_cast<uint8_t>((kT2 >> 8) & 0xFF);
    g_reg[0x8C] = static_cast<uint8_t>(kT3);
    g_reg[0x8E] = static_cast<uint8_t>(kP1 & 0xFF);
    g_reg[0x8F] = static_cast<uint8_t>((kP1 >> 8) & 0xFF);
    g_reg[0x90] = static_cast<uint8_t>(kP2 & 0xFF);
    g_reg[0x91] = static_cast<uint8_t>((kP2 >> 8) & 0xFF);
    g_reg[0x92] = static_cast<uint8_t>(kP3);
    g_reg[0x94] = static_cast<uint8_t>(kP4 & 0xFF);
    g_reg[0x95] = static_cast<uint8_t>((kP4 >> 8) & 0xFF);
    g_reg[0x96] = static_cast<uint8_t>(kP5 & 0xFF);
    g_reg[0x97] = static_cast<uint8_t>((kP5 >> 8) & 0xFF);
    g_reg[0x98] = static_cast<uint8_t>(kP7);
    g_reg[0x99] = static_cast<uint8_t>(kP6);
    g_reg[0x9C] = static_cast<uint8_t>(kP8 & 0xFF);
    g_reg[0x9D] = static_cast<uint8_t>((kP8 >> 8) & 0xFF);
    g_reg[0x9E] = static_cast<uint8_t>(kP9 & 0xFF);
    g_reg[0x9F] = static_cast<uint8_t>((kP9 >> 8) & 0xFF);
    g_reg[0xA0] = kP10;

    // Block 2: 0xE1..0xEE.  The other place transcription goes wrong: par_h1 and par_h2 share
    // 0xE2, h1 taking the LOW nibble with 0xE3 above it and h2 the HIGH nibble with 0xE1
    // above it.  Encoding them here from the values, rather than writing the byte by hand,
    // means the model cannot accidentally agree with a driver that has them swapped.
    g_reg[0xE1] = static_cast<uint8_t>((kH2 >> 4) & 0xFF);
    g_reg[0xE2] = static_cast<uint8_t>(((kH2 & 0x0F) << 4) | (kH1 & 0x0F));
    g_reg[0xE3] = static_cast<uint8_t>((kH1 >> 4) & 0xFF);
    g_reg[0xE4] = static_cast<uint8_t>(kH3);
    g_reg[0xE5] = static_cast<uint8_t>(kH4);
    g_reg[0xE6] = static_cast<uint8_t>(kH5);
    g_reg[0xE7] = kH6;
    g_reg[0xE8] = static_cast<uint8_t>(kH7);
    g_reg[0xE9] = static_cast<uint8_t>(kT1 & 0xFF);
    g_reg[0xEA] = static_cast<uint8_t>((kT1 >> 8) & 0xFF);
    g_reg[0xEB] = static_cast<uint8_t>(kG2 & 0xFF);
    g_reg[0xEC] = static_cast<uint8_t>((kG2 >> 8) & 0xFF);
    g_reg[0xED] = static_cast<uint8_t>(kG1);
    g_reg[0xEE] = static_cast<uint8_t>(kG3);

    // Block 3: the heater trim, at the very bottom of the map.
    g_reg[0x00] = static_cast<uint8_t>(kResHeatVal);
    g_reg[0x02] = static_cast<uint8_t>((kResHeatRange & 0x03) << 4);

    g_por_done = true;
    convert();
}

void ensure_por() noexcept {
    if (!g_por_done) por();
}

}  // namespace

Status bme688(const uint8_t* w, std::size_t wn, uint8_t* r, std::size_t rn) noexcept {
    ensure_por();
    if (wn == 0) return Status::NotPresent;  // always addressed with a register first
    uint8_t reg = w[0];
    for (std::size_t i = 1; i < wn; ++i) {
        g_reg[reg] = w[i];
        if (reg == kReset && w[i] == 0xB6) {
            por();  // calibration survives a soft reset on the real part, and so does the scene
        } else if (reg == kCtrlMeas && (w[i] & 0x03) == 0x01) {
            // A forced-mode trigger.  Fill the data registers from the scene and raise
            // new_data.  The part returns itself to sleep when the conversion ends, so the
            // mode bits are cleared here too -- a driver that polls ctrl_meas expecting to
            // see that happen is relying on real behaviour and should find it.
            convert();
            g_reg[kField0] = 0x80;  // new_data, gas_measuring/measuring already finished
            g_reg[kCtrlMeas] = static_cast<uint8_t>(w[i] & 0xFC);
        }
        reg = static_cast<uint8_t>(reg + 1);
    }
    for (std::size_t i = 0; i < rn; ++i) {
        r[i] = g_reg[reg];
        reg = static_cast<uint8_t>(reg + 1);
    }
    return Status::Ok;
}

void bme688_reset() noexcept {
    g_t = 21.5f;
    g_rh = 44.0f;
    g_p = 1013.2f;
    g_gas = 120000;
    por();
}

void set_env(float temp_c, float rh_pct, float press_hpa, uint32_t gas_ohms) noexcept {
    ensure_por();
    g_t = temp_c;
    g_rh = rh_pct;
    g_p = press_hpa;
    g_gas = gas_ohms;
    convert();
}

void env_scene(float& temp_c, float& rh_pct, float& press_hpa, uint32_t& gas_ohms) noexcept {
    ensure_por();
    temp_c = g_t;
    rh_pct = g_rh;
    press_hpa = g_p;
    gas_ohms = g_gas;
}

}  // namespace clk::hal::host::model
