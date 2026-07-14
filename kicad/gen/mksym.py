"""Generate clock_custom.kicad_sym: the VBAT power flag + custom IC symbols
(rectangular body, pins on left/right/top/bottom) for parts KiCad lacks.
Pin `(at)` is the outer connection endpoint; library Y is up.
"""
from __future__ import annotations

HEADER = '''(kicad_symbol_lib
\t(version 20251024)
\t(generator "clock_gen")
\t(generator_version "10.0")
'''

VBAT = '''\t(symbol "VBAT"
\t\t(power)
\t\t(pin_names (offset 0))
\t\t(exclude_from_sim no)(in_bom no)(on_board yes)
\t\t(property "Reference" "#PWR" (at 0 -3.81 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Value" "VBAT" (at 0 3.556 0)(show_name no)(effects (font (size 1.27 1.27))))
\t\t(property "Footprint" "" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Datasheet" "" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(property "Description" "Battery-node system rail (LT3652 BAT)" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))
\t\t(symbol "VBAT_0_1"
\t\t\t(polyline (pts (xy -0.762 1.27)(xy 0 2.54)) (stroke (width 0)(type default))(fill (type none)))
\t\t\t(polyline (pts (xy 0 0)(xy 0 2.54)) (stroke (width 0)(type default))(fill (type none)))
\t\t\t(polyline (pts (xy 0 2.54)(xy 0.762 1.27)) (stroke (width 0)(type default))(fill (type none)))
\t\t)
\t\t(symbol "VBAT_1_1"
\t\t\t(pin power_in line (at 0 0 90)(length 0)
\t\t\t\t(name "VBAT" (effects (font (size 1.27 1.27))))
\t\t\t\t(number "1" (effects (font (size 1.27 1.27)))))
\t\t)
\t\t(embedded_fonts no)
\t)
'''


def ic_symbol(name, footprint, datasheet, pins, description="",
              width=15.24, pin_len=3.81, grid=2.54):
    """pins: list of dicts {num, name, side(L/R/T/B), etype, order}.
    order is the slot index along that side (0 = topmost for L/R,
    0 = leftmost for T/B)."""
    hw = width / 2.0
    # group by side
    sides = {"L": [], "R": [], "T": [], "B": []}
    for p in pins:
        sides[p["side"]].append(p)
    for s in sides:
        sides[s].sort(key=lambda p: p["order"])
    nL, nR = len(sides["L"]), len(sides["R"])
    nT, nB = len(sides["T"]), len(sides["B"])
    rows = max(nL, nR)
    half_h = ((rows - 1) * grid) / 2.0 + grid  # a little margin
    top = half_h
    bot = -half_h
    body = []
    body.append(f'(rectangle (start {-hw:g} {top:g})(end {hw:g} {bot:g})'
                f'(stroke (width 0.254)(type default))(fill (type background)))')

    pin_nodes = []

    def emit(num, pname, etype, x, y, angle):
        pin_nodes.append(
            f'(pin {etype} line (at {x:g} {y:g} {angle:g})(length {pin_len:g})'
            f'(name "{pname}" (effects (font (size 1.27 1.27))))'
            f'(number "{num}" (effects (font (size 1.27 1.27)))))')

    def yrow(i, n):
        return ((n - 1) / 2.0 - i) * grid

    for i, p in enumerate(sides["L"]):
        y = yrow(i, nL)
        emit(p["num"], p["name"], p["etype"], -hw - pin_len, y, 0)
    for i, p in enumerate(sides["R"]):
        y = yrow(i, nR)
        emit(p["num"], p["name"], p["etype"], hw + pin_len, y, 180)
    for i, p in enumerate(sides["T"]):
        x = ((i) - (nT - 1) / 2.0) * grid
        emit(p["num"], p["name"], p["etype"], x, top + pin_len, 270)
    for i, p in enumerate(sides["B"]):
        x = ((i) - (nB - 1) / 2.0) * grid
        emit(p["num"], p["name"], p["etype"], x, bot - pin_len, 90)

    out = []
    out.append(f'\t(symbol "{name}"')
    out.append('\t\t(pin_names (offset 1.016))')
    out.append('\t\t(exclude_from_sim no)(in_bom yes)(on_board yes)')
    out.append(f'\t\t(property "Reference" "U" (at {-hw:g} {top+2.54:g} 0)(show_name no)(effects (font (size 1.27 1.27))(justify left)))')
    out.append(f'\t\t(property "Value" "{name}" (at {-hw:g} {top+5.08:g} 0)(show_name no)(effects (font (size 1.27 1.27))(justify left)))')
    out.append(f'\t\t(property "Footprint" "{footprint}" (at 0 {bot-7.62:g} 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))')
    out.append(f'\t\t(property "Datasheet" "{datasheet}" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))')
    out.append(f'\t\t(property "Description" "{description}" (at 0 0 0)(show_name no)(hide yes)(effects (font (size 1.27 1.27))))')
    out.append(f'\t\t(symbol "{name}_0_1"')
    for b in body:
        out.append('\t\t\t' + b)
    out.append('\t\t)')
    out.append(f'\t\t(symbol "{name}_1_1"')
    for pn in pin_nodes:
        out.append('\t\t\t' + pn)
    out.append('\t\t)')
    out.append('\t\t(embedded_fonts no)')
    out.append('\t)')
    return "\n".join(out) + "\n"


def P(num, name, side, order, etype="passive"):
    return dict(num=str(num), name=name, side=side, order=order, etype=etype)


def build():
    parts = []

    # ---- TPS61023 boost 5V (SOT-563) ----
    tps61023 = ic_symbol(
        "TPS61023", "Package_TO_SOT_SMD:SOT-563",
        "http://www.ti.com/lit/ds/symlink/tps61023.pdf",
        [P(3, "VIN", "L", 0, "passive"), P(2, "EN", "L", 1, "input"),
         P(1, "FB", "L", 2, "input"),
         P(6, "VOUT", "R", 0, "passive"), P(5, "SW", "R", 1, "passive"),
         P(4, "GND", "B", 0, "power_in")],
        description="3.7A boost converter, 0.5-5.5V in (5V rail)", width=12.7)
    parts.append(tps61023)

    # ---- TPS55340 boost 12V (HTSSOP-14 PWP) ----
    tps55340 = ic_symbol(
        "TPS55340", "Package_SO:Texas_HTSSOP-14-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask3.155x3.255mm",
        "http://www.ti.com/lit/ds/symlink/tps55340.pdf",
        [P(3, "VIN", "L", 0, "passive"), P(4, "EN", "L", 1, "input"),
         P(5, "SS", "L", 2, "input"), P(6, "SYNC", "L", 3, "input"),
         P(9, "FB", "L", 4, "input"), P(10, "FREQ", "L", 5, "input"),
         P(7, "COMP", "L", 6, "passive"),
         P(1, "SW", "R", 0, "passive"), P(2, "SW", "R", 1, "passive"),
         P(11, "NC", "R", 2, "no_connect"),
         P(8, "AGND", "R", 4, "power_in"),
         P(12, "PGND", "R", 5, "power_in"), P(13, "PGND", "R", 6, "power_in"),
         P(14, "PGND", "B", 0, "power_in"), P("15", "PAD", "B", 1, "power_in")],
        description="5A/40V boost converter (12V audio+wake rail)", width=17.78)
    parts.append(tps55340)

    # ---- AOSD32334C dual N-FET protector (SO-8): common-drain back-to-back ----
    # Pinout (datasheet): 1 S2, 2 G2, 3 S1, 4 G1, 5 D1, 6 D1, 7 D2, 8 D2
    aos = ic_symbol(
        "AOSD32334C", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
        "https://www.aosmd.com/pdfs/datasheet/AOSD32334C.pdf",
        [P(4, "G1", "L", 0, "input"), P(3, "S1", "L", 1, "passive"),
         P(2, "G2", "L", 2, "input"), P(1, "S2", "L", 3, "passive"),
         P(5, "D1", "R", 0, "passive"), P(6, "D1", "R", 1, "passive"),
         P(7, "D2", "R", 2, "passive"), P(8, "D2", "R", 3, "passive")],
        description="Dual N-MOSFET, 30V, cell- protection FET pair", width=15.24)
    parts.append(aos)

    # ---- TAS5760M Class-D amp (DCA 48-pin TSSOP, per repo datasheet SLOS772) ----
    # NOTE: BOM specifies TAS5760MDAPR (DAP 32-pin); the datasheet in /datasheet
    # is the DCA 48-pin. Built to the datasheet in hand. Connections are by pin
    # NAME (identical across packages) -> a DAP swap only renumbers pins+footprint.
    tas_pins = []
    # left: digital/control
    L = ["15", "14", "16", "13", "7", "8", "10", "11", "12", "6", "5"]
    Lname = {"15": "SDIN", "14": "SCLK", "16": "LRCK", "13": "MCLK",
             "7": "FREQ/SDA", "8": "PBTL/SCL", "10": "SPK_GAIN0",
             "11": "SPK_GAIN1", "12": "SPK_SLEEP/ADR", "6": "SPK_SD", "5": "SPK_FAULT"}
    # left lower: analog / reg / power-in
    L2 = ["9", "46", "48", "2", "4", "3", "1"]
    L2name = {"9": "DVDD", "46": "AVDD", "48": "GVDD_REG", "2": "ANA_REG",
              "4": "ANA_REF", "3": "VCOM", "1": "SFT_CLIP"}
    L2etype = {"9": "power_in", "46": "power_in", "48": "passive", "2": "passive",
               "4": "passive", "3": "passive", "1": "input"}
    # right: outputs / bootstrap / PVDD
    R = ["42", "40", "35", "37", "43", "39", "34", "38", "32", "33", "44", "45"]
    Rname = {"42": "SPK_OUTA+", "40": "SPK_OUTA-", "35": "SPK_OUTB+",
             "37": "SPK_OUTB-", "43": "BSTRPA+", "39": "BSTRPA-",
             "34": "BSTRPB+", "38": "BSTRPB-", "32": "PVDD", "33": "PVDD",
             "44": "PVDD", "45": "PVDD"}
    # SPK_OUT pins are 'passive' (they are paralleled in PBTL -> avoids ERC
    # output-output conflict) ; PVDD are power_in ; bootstraps passive
    Retype = {p: ("power_in" if Rname[p] == "PVDD" else "passive") for p in R}
    # bottom: grounds + pad
    B = ["17", "36", "41", "47", "49"]
    Bname = {"17": "DGND", "36": "PGND", "41": "PGND", "47": "GGND", "49": "PAD"}
    # top: NC (18..31) tied to GND on the sheet
    T = [str(n) for n in range(18, 32)]

    order = 0
    for i, p in enumerate(L):
        tas_pins.append(P(p, Lname[p], "L", i, "input"))
    for i, p in enumerate(L2):
        tas_pins.append(P(p, L2name[p], "L", len(L) + i, L2etype[p]))
    for i, p in enumerate(R):
        tas_pins.append(P(p, Rname[p], "R", i, Retype[p]))
    for i, p in enumerate(B):
        tas_pins.append(P(p, Bname[p], "B", i, "power_in"))
    for i, p in enumerate(T):
        tas_pins.append(P(p, "NC", "T", i, "passive"))

    tas = ic_symbol(
        "TAS5760M", "Package_SO:TSSOP-48_6.1x12.5mm_P0.5mm",
        "https://www.ti.com/lit/ds/symlink/tas5760m.pdf",
        tas_pins,
        description="Class-D amp, I2S+I2C, PBTL mono (DCA-48; verify vs DAP-32)",
        width=45.72)
    parts.append(tas)

    with open("clock_custom.kicad_sym", "w") as f:
        f.write(HEADER)
        f.write(VBAT)
        for p in parts:
            f.write(p)
        f.write(")\n")
    print("wrote clock_custom.kicad_sym with VBAT + %d IC symbols" % len(parts))


if __name__ == "__main__":
    build()
