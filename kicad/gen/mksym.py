"""Generate clock_custom.kicad_sym for kicad2 (single-page schematic).

Contents:
- VBAT / PVDD power flags
- Custom IC symbols KiCad lacks: TPS61023, TPS55340, AOSD32334C, TAS5760M (DAP-32)
- ESP32S3_CLOCK: ESP32-S3-WROOM-1 with *functionally grouped* pins (same pad
  numbers as the stock RF_Module symbol / footprint) so the single-page layout
  can wire peripheral groups straight to their blocks.
- Flattened clones of stock symbols that use `extends` (AO3400A, AO3401A,
  2N7002, DM3AT microSD, MCP23017) so the embedded copies always match the
  library file -> no lib_symbol_mismatch ERC noise.

Pin `(at)` is the outer connection endpoint; library Y is up.
"""
from __future__ import annotations
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from sexp import dumps, QStr  # noqa: E402
from schlib import SymbolCache  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "clock_custom.kicad_sym")

HEADER = '''(kicad_symbol_lib
\t(version 20251024)
\t(generator "clock_gen")
\t(generator_version "10.0")
'''


def rail_symbol(name, desc):
    """An up-pointing power-rail flag symbol (like +5V); net = name."""
    return f'''\t(symbol "{name}"
\t\t(power)
\t\t(pin_numbers (hide yes))
\t\t(pin_names (offset 0) (hide yes))
\t\t(exclude_from_sim no)(in_bom no)(on_board yes)
\t\t(property "Reference" "#PWR" (at 0 -3.81 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Value" "{name}" (at 0 3.556 0)(show_name no)(effects (font (size 1.27 1.27))))
\t\t(property "Footprint" "" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Datasheet" "" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Description" "{desc}" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(symbol "{name}_0_1"
\t\t\t(polyline (pts (xy -0.762 1.27)(xy 0 2.54)) (stroke (width 0)(type default))(fill (type none)))
\t\t\t(polyline (pts (xy 0 0)(xy 0 2.54)) (stroke (width 0)(type default))(fill (type none)))
\t\t\t(polyline (pts (xy 0 2.54)(xy 0.762 1.27)) (stroke (width 0)(type default))(fill (type none)))
\t\t)
\t\t(symbol "{name}_1_1"
\t\t\t(pin power_in line (at 0 0 90)(length 0)
\t\t\t\t(name "{name}" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "1" (effects (font (size 1.27 1.27)))))
\t\t)
\t\t(embedded_fonts no)
\t)
'''


def ic_symbol(name, footprint, datasheet, pins, description="",
              width=15.24, pin_len=3.81, grid=2.54, value=None, refdes="U"):
    """pins: list of dicts {num, name, side(L/R/T/B), order, etype}.
    `order` is the ROW SLOT along that side (0 = topmost for L/R, 0 = leftmost
    for T/B); slots may be skipped to leave visual gaps between pin groups."""
    hw = width / 2.0
    sides = {"L": [], "R": [], "T": [], "B": []}
    for p in pins:
        sides[p["side"]].append(p)
    for s in sides:
        sides[s].sort(key=lambda p: p["order"])
    max_slot = 0
    for s in ("L", "R"):
        for p in sides[s]:
            max_slot = max(max_slot, p["order"])
    rows = max_slot + 1
    half_h = ((rows - 1) * grid) / 2.0 + grid
    top, bot = half_h, -half_h
    body = [f'(rectangle (start {-hw:g} {top:g})(end {hw:g} {bot:g})'
            f'(stroke (width 0.254)(type default))(fill (type background)))']
    pin_nodes = []

    def emit(num, pname, etype, x, y, angle):
        pin_nodes.append(
            f'(pin {etype} line (at {x:g} {y:g} {angle:g})(length {pin_len:g})'
            f'(name "{pname}" (effects (font (size 1.27 1.27))))'
            f'(number "{num}" (effects (font (size 1.27 1.27)))))')

    def yslot(i):
        return ((rows - 1) / 2.0 - i) * grid

    for p in sides["L"]:
        emit(p["num"], p["name"], p["etype"], -hw - pin_len, yslot(p["order"]), 0)
    for p in sides["R"]:
        emit(p["num"], p["name"], p["etype"], hw + pin_len, yslot(p["order"]), 180)
    nT, nB = len(sides["T"]), len(sides["B"])
    for i, p in enumerate(sides["T"]):
        x = (i - (nT - 1) / 2.0) * grid
        emit(p["num"], p["name"], p["etype"], x, top + pin_len, 270)
    for i, p in enumerate(sides["B"]):
        x = (i - (nB - 1) / 2.0) * grid
        emit(p["num"], p["name"], p["etype"], x, bot - pin_len, 90)

    val = value or name
    out = [f'\t(symbol "{name}"',
           '\t\t(pin_names (offset 1.016))',
           '\t\t(exclude_from_sim no)(in_bom yes)(on_board yes)',
           f'\t\t(property "Reference" "{refdes}" (at {-hw:g} {top + 2.54:g} 0)(show_name no)(effects (font (size 1.27 1.27))(justify left)))',
           f'\t\t(property "Value" "{val}" (at {-hw:g} {top + 5.08:g} 0)(show_name no)(effects (font (size 1.27 1.27))(justify left)))',
           f'\t\t(property "Footprint" "{footprint}" (at 0 {bot - 7.62:g} 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))',
           f'\t\t(property "Datasheet" "{datasheet}" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))',
           f'\t\t(property "Description" "{description}" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))',
           f'\t\t(symbol "{name}_0_1"']
    out += ['\t\t\t' + b for b in body]
    out.append('\t\t)')
    out.append(f'\t\t(symbol "{name}_1_1"')
    out += ['\t\t\t' + pn for pn in pin_nodes]
    out.append('\t\t)')
    out.append('\t\t(embedded_fonts no)')
    out.append('\t)')
    return "\n".join(out) + "\n"


def P(num, name, side, order, etype="passive"):
    return dict(num=str(num), name=name, side=side, order=order, etype=etype)


# ---------------------------------------------------------------- HY2111
def hy2111():
    """HYCON HY2111 1-cell protector. Same body/pin geometry as the stock
    Battery_Management:AP9101CK6 symbol it replaces (2026-07-17, AP9101C
    NRND) so the hand-drawn wires in b_charger.py stay valid; pin names per
    the HYCON datasheet (OD/CS/OC), same SOT-23-6 pad map."""
    return '''\t(symbol "HY2111"
\t\t(pin_names (offset 1.016))
\t\t(exclude_from_sim no)(in_bom yes)(on_board yes)
\t\t(property "Reference" "U" (at -7.62 6.35 0)(show_name no)(effects (font (size 1.27 1.27))(justify left)))
\t\t(property "Value" "HY2111-GB" (at 7.62 6.35 0)(show_name no)(effects (font (size 1.27 1.27))))
\t\t(property "Footprint" "Package_TO_SOT_SMD:SOT-23-6" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Datasheet" "https://www.hycontek.com/hy_battery/DS-HY2111_EN.pdf" (at 0 1.27 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Description" "HYCON 1-cell Li+ protection IC, external dual-N FET, SOT-23-6" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(symbol "HY2111_0_1"
\t\t\t(rectangle (start -7.62 5.08)(end 7.62 -5.08)(stroke (width 0.254)(type default))(fill (type background)))
\t\t)
\t\t(symbol "HY2111_1_1"
\t\t\t(pin output line (at 10.16 -2.54 180)(length 2.54)
\t\t\t\t(name "OD" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "1" (effects (font (size 1.27 1.27)))))
\t\t\t(pin passive line (at -10.16 0 0)(length 2.54)
\t\t\t\t(name "CS" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "2" (effects (font (size 1.27 1.27)))))
\t\t\t(pin output line (at 10.16 2.54 180)(length 2.54)
\t\t\t\t(name "OC" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "3" (effects (font (size 1.27 1.27)))))
\t\t\t(pin no_connect line (at -7.62 -2.54 0)(length 2.54)(hide yes)
\t\t\t\t(name "NC" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "4" (effects (font (size 1.27 1.27)))))
\t\t\t(pin power_in line (at 0 7.62 270)(length 2.54)
\t\t\t\t(name "VDD" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "5" (effects (font (size 1.27 1.27)))))
\t\t\t(pin power_in line (at 0 -7.62 90)(length 2.54)
\t\t\t\t(name "VSS" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "6" (effects (font (size 1.27 1.27)))))
\t\t)
\t\t(embedded_fonts no)
\t)
'''


# ---------------------------------------------------------------- ESP32-S3
# Functionally-grouped WROOM-1 symbol. Pad numbers identical to the stock
# RF_Module:ESP32-S3-WROOM-1 symbol -> stock footprint stays valid.
def esp32s3():
    L = [  # (slot, num, name, etype)
        (0, 3, "EN", "input"),
        (1, 27, "IO0 BOOT", "bidirectional"),
        (3, 13, "IO19 USB_D-", "bidirectional"),
        (4, 14, "IO20 USB_D+", "bidirectional"),
        (6, 8, "IO15 XTAL_P", "bidirectional"),
        (7, 9, "IO16 XTAL_N", "bidirectional"),
        (9, 39, "IO1 ADC_VBAT", "input"),
        (10, 38, "IO2 ADC_HOME", "input"),
        (12, 24, "IO47 ENC_A", "bidirectional"),
        (13, 25, "IO48 ENC_B", "bidirectional"),
        (15, 35, "IO42 SENS_INT", "bidirectional"),
        (16, 36, "IO44 EXP_INT", "bidirectional"),
        (18, 28, "IO35 PSRAM", "no_connect"),
        (19, 29, "IO36 PSRAM", "no_connect"),
        (20, 30, "IO37 PSRAM", "no_connect"),
    ]
    R = [
        (0, 21, "IO13 SCLK", "bidirectional"),
        (1, 22, "IO14 MOSI", "bidirectional"),
        (2, 23, "IO21 MISO", "bidirectional"),
        (3, 10, "IO17 ENC_SW", "bidirectional"),
        (4, 11, "IO18 SD_CS", "bidirectional"),
        (6, 37, "IO43 MCLK", "bidirectional"),
        (7, 18, "IO10 BCLK", "bidirectional"),
        (8, 19, "IO11 LRCLK", "bidirectional"),
        (9, 20, "IO12 DOUT", "bidirectional"),
        (11, 12, "IO8 SDA", "bidirectional"),
        (12, 17, "IO9 SCL", "bidirectional"),
        (14, 7, "IO7 NEOPIX", "bidirectional"),
        (15, 26, "IO45 PWM_WARM", "bidirectional"),
        (16, 16, "IO46 PWM_COOL", "bidirectional"),
        (18, 4, "IO4 M_AIN1", "bidirectional"),
        (19, 5, "IO5 M_AIN2", "bidirectional"),
        (20, 6, "IO6 M_BIN1", "bidirectional"),
        (21, 15, "IO3 M_BIN2", "bidirectional"),
        (23, 31, "IO38 H_AIN1", "bidirectional"),
        (24, 32, "IO39 H_AIN2", "bidirectional"),
        (25, 33, "IO40 H_BIN1", "bidirectional"),
        (26, 34, "IO41 H_BIN2", "bidirectional"),
    ]
    pins = [P(num, nm, "L", slot, et) for slot, num, nm, et in L]
    pins += [P(num, nm, "R", slot, et) for slot, num, nm, et in R]
    pins.append(P(2, "3V3", "T", 0, "power_in"))
    pins.append(P(1, "GND", "B", 0, "power_in"))
    pins.append(P(40, "GND", "B", 1, "passive"))
    pins.append(P(41, "GND", "B", 2, "passive"))
    return ic_symbol(
        "ESP32S3_CLOCK", "RF_Module:ESP32-S3-WROOM-1",
        "https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf",
        pins,
        description="ESP32-S3-WROOM-1-N16R8, pins grouped by function (clock project)",
        width=40.64, value="ESP32-S3-WROOM-1-N16R8")


def build():
    cache = SymbolCache()
    parts = []

    parts.append(ic_symbol(
        "TPS61023", "Package_TO_SOT_SMD:SOT-563",
        "http://www.ti.com/lit/ds/symlink/tps61023.pdf",
        [P(3, "VIN", "L", 0, "passive"), P(2, "EN", "L", 1, "input"),
         P(1, "FB", "L", 2, "input"),
         P(6, "VOUT", "R", 0, "passive"), P(5, "SW", "R", 1, "passive"),
         P(4, "GND", "B", 0, "power_in")],
        description="3.7A boost converter, 0.5-5.5V in (5V rail)", width=12.7))

    parts.append(ic_symbol(
        "TPS55340", "Package_SO:Texas_HTSSOP-14-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask3.155x3.255mm",
        "http://www.ti.com/lit/ds/symlink/tps55340.pdf",
        [P(3, "VIN", "L", 0, "passive"), P(4, "EN", "L", 1, "input"),
         P(5, "SS", "L", 2, "input"), P(6, "SYNC", "L", 3, "input"),
         P(9, "FB", "L", 4, "input"), P(10, "FREQ", "L", 5, "input"),
         # HTSSOP-14 (Table 5-1): AGND=7, COMP=8 (pin swap fixed 2026-07-21;
         # they were reversed, which grounded COMP -> the boost never starts).
         # NC (11) is "reserved, must be connected to ground" -> passive.
         P(8, "COMP", "L", 6, "passive"),
         P(1, "SW", "R", 0, "passive"), P(2, "SW", "R", 1, "passive"),
         P(11, "NC", "R", 2, "passive"),
         P(7, "AGND", "R", 4, "power_in"),
         P(12, "PGND", "R", 5, "power_in"), P(13, "PGND", "R", 6, "power_in"),
         P(14, "PGND", "B", 0, "power_in"), P("15", "PAD", "B", 1, "power_in")],
        description="5A/40V boost converter (12V audio+wake rail)", width=17.78))

    parts.append(ic_symbol(
        "AOSD32334C", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
        "https://www.aosmd.com/pdfs/datasheet/AOSD32334C.pdf",
        [P(4, "G1", "L", 0, "input"), P(3, "S1", "L", 1, "passive"),
         P(2, "G2", "L", 2, "input"), P(1, "S2", "L", 3, "passive"),
         P(5, "D1", "R", 0, "passive"), P(6, "D1", "R", 1, "passive"),
         P(7, "D2", "R", 2, "passive"), P(8, "D2", "R", 3, "passive")],
        description="Dual N-MOSFET, 30V, cell- protection FET pair", width=15.24))

    parts.append(hy2111())

    # X40.879 dual-shaft stepper, direct PCB mount (replaced connector J4
    # 2026-07-17). Pin numbers/names per SP-X40-e-A-Pinout: 1-4 = external
    # shaft coils (1e+ 2e- / 4e+ 3e-), 5-8 = internal (1i+ 2i- / 4i+ 3i-).
    parts.append(ic_symbol(
        "X40_879", "clock:Juken_X40-879_DualShaft",
        "https://www.jukenswisstech.com/wp-content/uploads/SP-X40-e-A-Pinout.pdf",
        [P(1, "1e", "L", 0), P(2, "2e", "L", 1),
         P(3, "3e", "L", 2), P(4, "4e", "L", 3),
         P(5, "1i", "L", 5), P(6, "2i", "L", 6),
         P(7, "3i", "L", 7), P(8, "4i", "L", 8)],
        description="Juken X40.879 dual-shaft stepper, PCB-mounted, "
                    "shafts through-board (custom footprint)",
        width=15.24, value="X40.879", refdes="M"))

    # TAS5760M (DAP 32-pin HTSSOP/PowerPAD, datasheet p.6)
    tas_pins = []
    L = [("16", "SDIN", "input"), ("15", "SCLK", "input"), ("17", "LRCK", "input"),
         ("14", "MCLK", "input"), ("8", "FREQ/SDA", "input"), ("9", "PBTL/SCL", "input"),
         ("11", "SPK_GAIN0", "input"), ("12", "SPK_GAIN1", "input"),
         ("13", "SPK_SLEEP/ADR", "input"), ("7", "SPK_SD", "input"),
         ("6", "SPK_FAULT", "output"),
         ("10", "DVDD", "power_in"), ("1", "AVDD", "power_in"),
         ("32", "GVDD_REG", "passive"), ("3", "ANA_REG", "passive"),
         ("5", "ANA_REF", "passive"), ("4", "VCOM", "passive"),
         ("2", "SFT_CLIP", "input")]
    R = [("29", "SPK_OUTA+", "passive"), ("26", "SPK_OUTA-", "passive"),
         ("20", "SPK_OUTB+", "passive"), ("23", "SPK_OUTB-", "passive"),
         ("30", "BSTRPA+", "passive"), ("25", "BSTRPA-", "passive"),
         ("19", "BSTRPB+", "passive"), ("24", "BSTRPB-", "passive"),
         ("21", "PVDD", "power_in"), ("28", "PVDD", "power_in")]
    B = [("18", "DGND"), ("22", "PGND"), ("27", "PGND"), ("31", "GGND"), ("33", "PAD")]
    for i, (num, name, et) in enumerate(L):
        tas_pins.append(P(num, name, "L", i, et))
    for i, (num, name, et) in enumerate(R):
        # gap after the 8 output/bootstrap pins, before PVDD
        slot = i if i < 8 else i + 1
        tas_pins.append(P(num, name, "R", slot, et))
    for i, (num, name) in enumerate(B):
        tas_pins.append(P(num, name, "B", i, "power_in"))
    parts.append(ic_symbol(
        "TAS5760M",
        "Package_SO:HTSSOP-32-1EP_6.1x11mm_P0.65mm_EP5.2x11mm_Mask4.11x4.36mm",
        "https://www.ti.com/lit/ds/symlink/tas5760m.pdf",
        tas_pins,
        description="TAS5760MDAPR Class-D amp, I2S+I2C, PBTL mono (DAP 32-pin)",
        width=40.64, value="TAS5760MDAPR"))

    parts.append(esp32s3())

    # ---- flattened clones of stock `extends` symbols (identical geometry) ----
    clones = ["Transistor_FET:AO3400A", "Transistor_FET:AO3401A",
              "Transistor_FET:2N7002",
              "Connector:Micro_SD_Card_Det_Hirose_DM3AT",
              "Interface_Expansion:MCP23017x-x-SS",
              "74xGxx:74AHCT1G125",
              "Power_Protection:USBLC6-2SC6"]  # U16 USB D+/D- ESD array
    clone_txt = []
    for lib_id in clones:
        nick, name = lib_id.split(":", 1)
        node = cache._standalone_node(nick, name)
        import copy
        node = copy.deepcopy(node)
        clone_txt.append(dumps(node, indent=1))

    with open(OUT, "w") as f:
        f.write(HEADER)
        f.write(rail_symbol("VBAT", "Battery-node system rail (LT3652 BAT)"))
        f.write(rail_symbol("PVDD", "Amp PVDD rail (LTC4412 mux: 12V/5V)"))
        for p in parts:
            f.write(p)
        for t in clone_txt:
            f.write(t + "\n")
        f.write(")\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    build()
