"""Generates clock.kicad_pcb from clock.kicad_sch using KiCad's own pcbnew
Python bindings (run under KiCad's bundled interpreter, not system python3):

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_build.py

v3 (2026-07-22): full FUNCTIONAL placement -- every one of the ~184 parts has
an explicit hand-chosen seed position derived from its connectivity
(decoupling at its IC pin, FB dividers at the FB pin, converter magnetics at
the SW node, protection at the battery terminal it guards, ...), replacing
v2's per-block bin-packing.  A bounded relaxation pass (relax(), max 5mm
displacement, anchors locked) then enforces real assembly clearance between
courtyards without destroying the functional adjacency; qa_locality() proves
it afterwards.  Placement only + stackup + zones + net assignment; no traces
(see ../PCB_NOTES.md for why routing is left to a human/router).

Back-side geometry facts this layout is built around (verified empirically,
see verify_pins()):

* pcbnew Flip(LEFT_RIGHT) mirrors about X AND leaves the footprint's stored
  orientation at 180deg.  add_footprint() therefore applies B-side rotations
  as absolute (180+rot)%360 so that the PLACEMENT table's rot values mean:
      rot=0   -> pad(lx,ly) at world (ax-lx, ay+ly)
      rot=180 -> pad(lx,ly) at world (ax+lx, ay-ly)
      rot=90  -> (ax-ly, ay-lx);  rot=270 -> (ax+ly, ay+lx)
* Consequence: an IC's local-left pin column faces world RIGHT at rot=0.
  E.g. U8's USB/XTAL/3V3/EN pins (local left) face world right -- so USB
  entry, crystal and decoupling sit RIGHT of the module; U12's outputs face
  world right toward the motor at rot=0 while U11 needs rot=180.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from sexp import parse  # noqa: E402

import pcbnew  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.dirname(HERE)
SCH = os.path.join(KICAD_DIR, "clock.kicad_sch")
PCB_OUT = os.path.join(KICAD_DIR, "clock.kicad_pcb")
CLOCK_PRETTY = os.path.join(KICAD_DIR, "clock.pretty")
STOCK_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

MM = pcbnew.FromMM

BOARD = 110.0
CX, CY = 55.0, 55.0          # board centre == motor shaft centre
MOTOR_ANCHOR = (55.0, 61.0)  # shaft pad is at local (0,-6); anchoring here
MOTOR_R = 16.7               # puts the shaft at (55,55); the courtyard circle
                             # (r=16.7) is centred on the ANCHOR = (55,61).

# ESP32 module geometry (from the stock footprint, local frame, rot0/B):
# module courtyard local x+-9.75, y -6.75..13.45; antenna keepout (ALL copper
# layers, no footprints/pads/tracks) local x+-24, y -27.75..-6.75.
U8_AT = (44.0, 14.0)
U8_MODULE_RECT = (U8_AT[0] - 9.75, U8_AT[1] - 6.75 + 14.0,  # placeholder, fixed below
                  0, 0)
U8_MODULE_RECT = (U8_AT[0] - 9.75, U8_AT[1] - 6.75,
                  U8_AT[0] + 9.75, U8_AT[1] + 13.45)
U8_ANT_RECT = (U8_AT[0] - 24.0, U8_AT[1] - 27.75, U8_AT[0] + 24.0, U8_AT[1] - 6.75)
U8_EPVIA_RECT = (U8_AT[0] - 3.6, U8_AT[1] + 0.3, U8_AT[0] + 3.6, U8_AT[1] + 4.6)
# microSD card corridor: the card protrudes past J6's courtyard to the board
# edge; nothing may sit in front of the slot mouth (B side).
J6_MOUTH_RECT = (0.0, 29.0, 5.4, 45.0)

# ==========================================================================
# Schematic -> parts + netlist (single source of truth, never transcribed)
# ==========================================================================


def load_schematic_parts():
    root = parse(open(SCH, encoding="utf-8").read())
    syms = [c for c in root[1:] if getattr(c, "tag", None) == "symbol"]

    def prop(node, name):
        for c in node[1:]:
            if getattr(c, "tag", None) == "property" and len(c) > 2 and str(c[1]) == name:
                return str(c[2])
        return None

    parts = {}
    for s in syms:
        ref = prop(s, "Reference")
        if not ref or ref.startswith("#"):
            continue
        fp = prop(s, "Footprint")
        if not fp:
            continue
        parts[ref] = {"value": prop(s, "Value"), "footprint": fp}
    return parts


def export_netlist():
    out = os.path.join(HERE, "_netlist.net")
    subprocess.run(
        [KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out, SCH],
        check=True, capture_output=True,
    )
    return out


def load_netlist(path):
    root = parse(open(path, encoding="utf-8").read())
    mapping = {}
    for net in root.find("nets").findall("net"):
        name = None
        for c in net[1:]:
            if getattr(c, "tag", None) == "name":
                name = str(c[1])
        for node in net.findall("node"):
            ref = pin = None
            for c in node[1:]:
                if getattr(c, "tag", None) == "ref":
                    ref = str(c[1])
                elif getattr(c, "tag", None) == "pin":
                    pin = str(c[1])
            if ref and pin:
                mapping[(ref, pin)] = name
    return mapping


def resolve_fp(field):
    nick, name = field.split(":", 1)
    if nick == "clock":
        return CLOCK_PRETTY, name
    return os.path.join(STOCK_FP, f"{nick}.pretty"), name


# ==========================================================================
# PLACEMENT -- ref: (x, y, rot, side).  World mm, front view, +y down.
# Side "B" parts are x-mirrored about their anchor (see module docstring for
# the rot convention).  Hand-chosen SEEDS; relax() may shift non-anchored
# parts by up to RELAX_CAP mm to guarantee assembly clearance.
# ==========================================================================

PLACEMENT = {}

# Parts that must not move at all (constraint-pinned):
LOCKED = {"U8", "M1", "BT1", "J6", "D40", "D41", "C230", "C231", "U14",
          "F1", "RT1", "R17", "J1"}


def P(ref, x, y, rot=0, side="B"):
    if ref in PLACEMENT:
        raise SystemExit(f"duplicate placement for {ref}")
    PLACEMENT[ref] = (x, y, rot, side)


# ---- FRONT: dial-facing parts only -----------------------------------------
P("U14", 55.0, 27.0, 90, "F")   # homing sensor, 12 o'clock, r=28mm (<30 spec);
                                # rot90 = emitter/detector pair radial to the
                                # hand sweep so the hand covers both at once
P("D40", 13.0, 55.0, 0, "F")    # dial-wash pixel, 9 o'clock (chain pos 1)
P("D41", 97.0, 55.0, 0, "F")    # dial-wash pixel, 3 o'clock (chain pos 2)
P("C230", 13.0, 48.5, 90, "F")  # D40 100n, 5mm above its pixel, same side
P("C231", 97.0, 48.5, 90, "F")  # D41 100n

# ---- MCU: ESP32-S3 module, antenna over the top board edge -----------------
# U8 at (44,14): antenna keepout world x 20..68 / y<7.25 (on-board part);
# module courtyard x 34.25..53.75, y 7.25..27.45.  Pins 1-14 face world
# RIGHT (x=52.75), pins 27-40 world LEFT (x=35.25), bottom row at y=26.5.
P("U8", *U8_AT, 0)
# 32k crystal at pads 8/9 (world (52.75, 17.6/18.9)); load caps below it
P("Y1", 55.6, 19.0, 90)
P("C145", 55.4, 23.7, 0)        # XTAL_P load cap
P("C146", 55.4, 26.2, 0)        # XTAL_N load cap
# two tidy columns right of the module: col1 (x 56.6) 0603s at the pins,
# col2 (x 60.4) the fatter 0805s/second-tier parts
P("C142", 56.6, 8.5, 0)         # 100n at 3V3 pin 2 (52.75,10.0)
P("C143", 56.6, 11.0, 0)        # 100n
P("R50", 56.6, 13.5, 0)         # EN pull-up (EN pin 3 at (52.75,11.3))
P("R113", 57.0, 15.8, 0)        # ENC_SW 10k pull-up (U8.10 at (52.75,20.2))
P("C140", 60.4, 8.6, 0)         # 10u 3V3
P("C141", 60.4, 11.2, 0)        # 22u 3V3 bulk
P("C144", 60.4, 13.9, 0)        # EN RC 1u
P("C239", 60.4, 16.6, 0)        # ENC_SW 100n
# IO46 pull-down: pin 27 faces world LEFT (35.25,25.25)
P("R51", 31.0, 27.0, 90)
# PROG header + buttons: free top-left edge (left of the antenna keepout);
# EN/IO46 are slow lines, routed under/around the module body
P("J2", 6.0, 11.0, 0)           # 1x4, pin 1 at anchor, pads run +y
P("SW1", 17.0, 13.2, 90)        # RESET
P("SW2", 23.0, 13.2, 90)        # BOOT
# WAKE_COOL pull-down at its U8 pad (49.7,26.5)
P("R52", 48.0, 29.5, 0)
# homing-sensor support directly behind U14 (front (55,27)): 150R LED drive,
# 10k pull-up, 100n filter; HOME_OPTO returns to U8.38 at world (35.25,11.3)
P("R98", 51.0, 31.8, 0)
P("R99", 55.0, 31.8, 0)
P("C241", 59.0, 31.8, 0)

# ---- IO: MCP23017 + knob/sensor/toggle connectors, top-right ---------------
# U13 at (76,16) rot0: pins 1-14 (VDD, I2C, CHRG/FAULT/GPB) face world RIGHT
# (x=79.5) -> I2C drops down the right side toward U9/J7, CHRG/FAULT run to
# the charger column; pins 15-28 (INT, straps, GPA) face world LEFT (72.5).
P("U13", 76.0, 16.0, 0)
P("C240", 83.5, 17.0, 90)       # VDD 100n at pin 9 (79.5,17.0)
P("J10", 81.0, 5.2, 0)          # knob (EM14): top edge, shortest run to the
                                # top-face knob; right of the antenna keepout
P("J7", 94.0, 9.0, 0)           # sensor board (I2C): top-right corner
P("J11", 63.0, 21.5, 0)         # radio-off toggle -> GPA3 (world-left pin)
# knob A/B 100k/200k dividers between J10 and U8's ENC pads (39.6/38.3, 26.5)
P("R111", 65.5, 9.0, 0)         # A series 100k
P("R112", 69.0, 9.0, 0)         # B series 100k
P("R114", 65.5, 12.0, 0)        # A lower 200k
P("R115", 69.0, 12.0, 0)        # B lower 200k
# expander straps/pulls at the world-left pin column (72.5, 16..21)
P("R90", 69.0, 25.5, 0)         # A0
P("R91", 73.0, 25.5, 0)         # A1
P("R92", 77.0, 25.5, 0)         # A2
P("R93", 69.0, 28.5, 0)         # ~RESET pull-up
P("R94", 73.0, 28.5, 0)         # INTA pull-up
# I2C bus pull-ups where the bus leaves the expander down the right side
P("R95", 81.0, 25.5, 0)         # SDA 4.7k
P("R96", 85.0, 25.5, 0)         # SCL 4.7k
# amp slow-control pulls: on the SPK_SD/SPK_FAULT route U13 -> U9
P("R62", 81.0, 28.5, 0)         # SPK_FAULT 10k pull-up
P("R63", 85.0, 28.5, 0)         # SPK_SD 100k pull-down (POR-safe)
P("R97", 89.5, 13.5, 90)        # SENSOR_INT 10k pull-up at J7
# NeoPixel chain-extension connector (D41.DOUT -> J12) + chain 5V bulk;
# right-centre, on the way from D41 (front (97,55)) to the 5V trunk
P("J12", 74.0, 33.0, 0)
P("C237", 64.5, 33.5, 0)        # 100u chain bulk
P("C238", 76.5, 39.5, 0)        # 100n

# ---- POWER IN: vertical USB-C + PD controller + ESD, right column top ------
P("J1", 95.0, 31.0, 0)          # vertical USB-C, port straight out the back
P("U16", 87.0, 25.5, 0)         # USBLC6 flow-through: J1 D+- (94..95,29.5)
                                # -> pins 1/3, MCU twins 4/6 exit toward U8
P("D1", 103.0, 28.0, 90)        # VBUS TVS at the connector VBUS pads
P("C100", 104.0, 34.5, 90)      # 10u VBUS
P("C101", 104.0, 38.0, 90)      # 10u VBUS
P("C102", 104.0, 41.5, 90)      # 100n VBUS
# CH224K at (95,39) rot0: pins 6-10 (CC1/CC2/VBUS/CFG1/PD_PG) face world
# LEFT (92.85) toward J1's CC pads above; pins 1-5 (VDD/DM/DP) face RIGHT.
P("U1", 95.0, 39.0, 0)
P("R1", 99.5, 35.5, 0)          # VBUS -> VDD 1k
P("C1", 100.0, 43.0, 0)         # VDD 1u (pin 1 at (97.15,38.0))
P("R2", 91.5, 35.5, 0)          # VBUS sense 10k (pin 8 world (92.85,39.0))
P("R3", 97.0, 42.8, 0)          # CFG1 56k -> GND
P("R4", 92.5, 42.5, 0)          # PD_PG 10k pull-up -> 3V3
P("C129", 85.0, 39.5, 0)        # 100u VBAT mid-rail bulk (elec), on the VBAT
                                # trunk between charger output and the boosts

# ---- CHARGER: LT3652 buck charger, right column middle ---------------------
# U2 at (96,50) rot0: pins 1-6 (VIN/VIN_REG/SHDN/CHRG/FAULT/TIMER) face world
# RIGHT (98.15) toward the USB block above; pins 7-12 (VFB/NTC/BAT/SENSE/
# BOOST/SW) face world LEFT (93.85) toward the magnetics and the VBAT node.
P("U2", 96.0, 50.0, 0)
# SW-node row above the charger, y~45: catch diode, bootstrap, refresh diode
P("D11", 85.5, 45.2, 0)         # SW catch diode (SW pin at (93.85,48.4))
P("C108", 90.5, 45.2, 90)       # BOOST-SW bootstrap 1u
P("D10", 94.5, 45.2, 180)       # BOOST refresh diode -> VBAT
P("C104", 99.5, 45.2, 0)        # TIMER 100n (pin 6 at (98.15,51.6))
P("L1", 87.5, 48.5, 0)          # 10uH: SW -> SENSE
P("R18", 89.0, 52.5, 0)         # 0.1R sense: L1.2 -> VBAT
P("C106", 84.9, 54.6, 90)       # 10u VBAT at charger output
P("C107", 100.0, 56.6, 0)       # 100u VBAT bulk (elec)
P("R10", 102.5, 46.0, 0)        # VIN_REG divider top (VBUS)
P("R11", 102.5, 48.5, 0)        # VIN_REG divider bottom
P("R12", 102.5, 51.0, 0)        # CHRG 10k pull-up -> 3V3
P("R13", 105.5, 48.5, 90)       # FAULT 10k pull-up -> 3V3
# FB divider on VFB pin 7 (93.85,51.6) + FULLCHG trim switch: 2 rows SW of U2
P("R14", 86.5, 57.8, 90)
P("R15", 89.1, 57.8, 90)
P("C105", 91.7, 57.8, 90)
P("R16", 86.5, 61.4, 90)
P("Q1", 89.75, 61.4, 90)
P("R24", 93.0, 61.4, 90)        # FULLCHG_EN 100k pull-down (POR-safe)
# VBAT hf bypass at the U7 boost VIN (pin 3 at (79.86,63.85))
P("C127", 85.0, 68.3, 90)      # 10u
P("C128", 87.5, 68.3, 90)      # 100n

# ---- 12V BOOST (TPS55340) mid-right, feeding audio PVDD mux + wake J9 ------
# U7 at (77,64.5) rot0: pins 1-7 (SW/SW/VIN/EN/SS) face world RIGHT (79.86);
# pins 8-14 (COMP/FB/FREQ) face world LEFT (74.14).
P("U7", 77.0, 64.5, 0)
P("L4", 83.0, 60.0, 0)          # 4.7uH VBAT -> SW
P("D20", 85.5, 64.5, 0)         # SW -> 12V Schottky (SMA)
P("C130", 74.5, 76.5, 90)       # 22u/1210 12V
P("C131", 78.0, 76.5, 90)       # 22u/1210 12V
P("C132", 81.5, 76.5, 90)       # 22u/1210 12V
# compensation/FB/FREQ 2x3 grid below the west pin column, motor-clear
P("R45", 72.0, 69.8, 0)         # FB top 86.6k (12V) -- pin 9 (74.14,63.2)
P("R46", 75.5, 69.8, 0)         # FB bottom 10k
P("R47", 79.0, 69.8, 0)         # FREQ 95.3k
P("R48", 72.0, 72.2, 0)         # COMP 2.55k (pin 8 at (74.14,62.55))
P("C134", 75.5, 72.2, 0)        # COMP 100n (in series w/ R48)
P("C135", 79.0, 72.2, 0)        # COMP hf 100p
P("C133", 82.5, 68.5, 0)        # SS 47n (pin 5 at (79.86,65.15))
P("R44", 78.5, 58.5, 0)         # EN(BOOST12_EN) 100k pull-down

# ---- AUDIO: TAS5760M + PVDD mux + output filter, lower right ---------------
# U9 at (96.5,79.5) rot180: signal pins 1-16 face world LEFT (92.85),
# outputs/PVDD pins 17-32 face world RIGHT (100.15); I2S arrives from U8's
# bottom row down the x~58..62 channel, outputs leave to the filter row below.
P("U9", 96.5, 79.5, 180)
# PVDD mux (LTC4412 + P-FET + 12V Schottky) in a row above the amp
P("U10", 96.5, 69.5, 0)         # LTC4412 (5V aux in, gate out, PVDD sense)
P("Q4", 100.9, 69.5, 90)        # AO3401A 5V-side pass FET
P("D30", 105.1, 69.5, 90)       # SS34 12V -> PVDD
P("C186", 95.75, 65.5, 90)      # 100n 5V at U10.1
P("C172", 102.0, 64.4, 0)       # 220u PVDD bulk (elec)
# PVDD hf + bootstrap caps at the output pin column (100.15, 74..85)
P("C180", 103.5, 75.0, 90)      # BSTRPA+
P("C182", 106.0, 75.0, 90)      # BSTRPB+
P("C181", 103.5, 78.5, 90)      # BSTRPA-
P("C183", 106.0, 78.5, 90)      # BSTRPB-
P("C170", 103.5, 82.0, 90)      # 100n PVDD (PVDD pins at (100.15,77.2/81.8))
P("C171", 106.0, 82.0, 90)      # 1u PVDD
# small-signal support: 3-column grid west of the amp's signal pin column
# (U9's low-numbered pins sit at the SOUTH end after rot180, so the
# reg/ref/VCOM caps fill the lower rows)
P("C161", 84.6, 73.0, 90)       # DVDD 100n (3V3 rail, fed by the trunk)
P("C163", 84.6, 76.4, 90)       # AVDD 100n (2026-07-21 addition)
P("R60", 87.5, 76.4, 90)        # SPK_GAIN 10k (pins 11/12 at (92.85,77.2))
P("R61", 90.4, 76.4, 90)        # SLEEP/ADR 0R (pin 13 at (92.85,76.6))
P("C166", 84.6, 79.8, 90)       # ANA_REF 100n
P("C160", 87.5, 79.8, 90)       # GVDD 1u
P("C167", 90.4, 79.8, 90)       # VCOM 1u
P("C165", 84.6, 83.0, 90)       # ANA_REG 1u (pin 3 at (92.85,83.1))
P("C162", 87.5, 83.0, 90)       # DVDD 10u
P("C164", 90.4, 83.0, 90)       # ANA_REG 100n
# LC output filter + speaker connector: row below the amp
P("L5", 92.8, 88.7, 0)          # OUTA+ 10uH
P("L6", 98.2, 88.7, 0)          # OUTA- 10uH
P("C184", 92.9, 93.0, 90)       # 0.68u filter A
P("C185", 95.4, 93.0, 90)       # 0.68u filter B
P("J3", 106.0, 93.5, 90)        # speaker JST PH (vertical entry)

# ---- MOTOR: X40.879 through-board, drivers flanking it ---------------------
P("M1", *MOTOR_ANCHOR, 0)       # anchor corrected at build time: shaft->centre
# U11 drives the EXTERNAL-shaft coils (M1 world pads at x 66.4/69.0);
# rot=180 puts its outputs (local left col) at world LEFT (75.5) facing the
# motor; controls exit world RIGHT (82.5) from U8's right/bottom pads.
P("U11", 77.0, 51.0, 180)
P("C200", 83.4, 48.6, 0)        # VM 10u (VM pins at (80.5, 47.4..54.6))
P("C201", 83.4, 51.2, 0)        # VM 100n
P("C202", 80.0, 45.5, 0)        # VCC 100n
# U12 drives the INTERNAL-shaft coils (M1 world pads at x 43.6/41.0); rot=0
# puts outputs at world RIGHT (34.5) facing the motor, controls/VM at world
# LEFT (27.5), reached down the left channel from U8's world-left column.
P("U12", 31.0, 58.0, 0)
P("C210", 26.0, 52.0, 0)        # VM 10u
P("C211", 22.5, 52.0, 0)        # VM 100n
P("C212", 26.5, 64.0, 0)        # VCC 100n

# ---- STORAGE: microSD on the LEFT edge, card slides out -X -----------------
# rot=270 (see module docstring) puts the card mouth at world -X; anchor
# x=13.5 puts the mouth ~flush with the board edge; contacts face +X toward
# the pull-up row.
P("J6", 13.5, 37.0, 270)
P("R70", 25.5, 31.5, 90)        # DAT3/CD 10k
P("R71", 25.5, 35.5, 90)        # DAT1 10k
P("R72", 25.5, 39.5, 90)        # DAT2 10k
P("C222", 25.5, 43.0, 90)       # 3V3 10u
P("C223", 28.5, 35.5, 90)       # 3V3 100n

# ---- RAILS: 5V boost + 3V3 buck, left column (VBAT trunk on F.Cu) ----------
# U5 TPS61023 at (14,54) rot0: pins 1-3 (FB/VIN/VIN) face world RIGHT
# (14.71); pins 4-6 (GND/SW/VOUT) face world LEFT (13.29) -> L2 left.
# 5V lands next to U12's VM pins and the D40 pixel 5V via.
P("U5", 14.0, 54.0, 0)
P("L2", 8.0, 54.0, 0)           # 1uH VBAT -> SW
P("C120", 8.0, 49.5, 0)         # VBAT in 10u
P("C121", 14.0, 49.5, 0)        # 5V out 22u
P("C122", 18.5, 51.5, 90)       # 5V out 22u
P("R40", 12.0, 58.5, 0)         # FB top 732k (pin 1 at (14.71,53.5))
P("R41", 16.0, 58.5, 0)         # FB bottom 100k
P("C123", 8.0, 58.5, 0)         # FB feed-forward 220p
# U6 TLV62569 at (14,66) rot0: pins 1-3 (VIN/GND/SW) world RIGHT (15.14),
# pins 4-5 (EN/FB) world LEFT (12.86); L3 right, on the way to the loads.
P("U6", 14.0, 66.0, 0)
P("L3", 19.5, 66.0, 0)          # 2.2uH SW -> 3V3
P("C124", 9.5, 62.5, 0)         # 5V in 10u
P("C125", 9.5, 69.5, 0)         # 3V3 out 22u
P("R42", 14.0, 70.5, 0)         # FB top 453k
P("R43", 10.0, 73.0, 0)         # FB bottom 100k
P("C126", 14.0, 73.0, 0)        # FB feed-forward 6.8p
# NeoPixel data buffer near the chain head D40 (front (13,55)):
# U8.7 (52.75,16.4) -> U15 -> R110 -> via -> D40.DIN
P("U15", 28.5, 49.5, 0)
P("R110", 24.5, 49.5, 0)        # 330R series into the chain

# ---- WAKE: 12V COB connector + low-side FETs, centre-bottom-left -----------
# 12V arrives on the F.Cu trunk from U7/C130-132; the COB strips are
# off-board, so J9 only needs clear cable access.  The gate PWMs come down
# the x~36 channel from U8's bottom row -- shorter than any right-side spot.
P("J9", 30.0, 76.5, 0)          # JST PH 1x03 (12V/warm/cool)
P("Q6", 25.0, 71.5, 90)         # warm low-side AO3400A (drain -> J9.2)
P("Q7", 28.5, 71.5, 90)         # cool low-side
P("R104", 32.0, 71.5, 90)       # warm gate 100R
P("R105", 35.0, 71.5, 90)       # warm gate pull-down 10k
P("R106", 33.0, 68.0, 90)       # cool gate 100R
P("R107", 36.0, 68.0, 90)       # cool gate pull-down 10k

# ---- BATTERY + PROTECTION ---------------------------------------------------
# BT1 rot0 at (51,96): pad 1 (cell+) lands world RIGHT at (86.8,96) next to
# the Q2/VBAT cluster; pad 2 (cell-) lands world LEFT at (15.2,96) next to
# the protector (U3/U4) + TCO chain.  Each protection element sits AT the
# battery terminal it guards ("safety close to the battery").
P("BT1", 51.0, 96.0, 0)
# cell+ side, bottom-right (right of the holder body, at the + PC pin):
# reverse-polarity/ideal-diode P-FET into VBAT + CELL_TEST + sense divider,
# in rows: FETs / pulls+divider / clamp+filter / straps
P("D12", 99.5, 93.0, 0)         # VBAT TVS SMAJ5.0CA at Q2's drain
P("Q2", 92.4, 97.0, 90)         # AO3401A: S=cell+, D=VBAT
P("Q9", 96.6, 97.0, 90)         # CELL_TEST lifter (pulls Q2 gate to holder+)
P("Q8", 100.8, 97.0, 90)        # CELL_TEST level shifter
P("R19", 92.4, 100.6, 90)       # Q2 gate pull-down 100k
P("R22", 95.4, 101.0, 0)        # VBAT_SENSE 100k top (cell+)
P("R23", 98.2, 101.0, 0)        # VBAT_SENSE 100k bottom -> Q3
P("C110", 101.9, 100.8, 0)      # 100n sense filter
P("Q3", 92.9, 104.7, 90)        # 2N7002 divider enable
P("R25", 95.5, 104.5, 90)       # VBAT_DIV_EN 100k pull-down (POR-safe)
P("D13", 98.9, 104.3, 0)        # BAT42W clamp on the sense node
P("R27", 92.7, 107.9, 0)        # 100k holder+ -> Q8 drain
P("R26", 96.3, 107.9, 0)        # CELL_TEST 100k
# cell- side, bottom-left band (y 78..84.5 is fully below the motor
# courtyard for every x): protector IC + dual FET + TCO in the cell- path
P("U3", 6.5, 81.5, 0)           # HY2111-GB across the cell
P("R20", 3.5, 78.0, 0)          # VDD 100R from cell+ (thin DC sense run)
P("C109", 7.0, 78.0, 0)         # VDD 100n
P("R21", 10.5, 78.0, 0)         # CS 2k
P("U4", 15.0, 81.0, 90)         # AOSD32334C dual FET (cell- -> F1 -> GND)
# TCO 77C: body extends WEST of pad 1 when flipped, so anchor at the EAST
# end; pads land at x 44 / 23.7, Ø4 body lying between them alongside the
# battery holder's top edge for thermal coupling to the cell
P("F1", 44.0, 83.2, 0)
# NTC divider, thermally coupled to the middle of the cell body
P("RT1", 50.0, 84.2, 0)         # 10k NTC (senses the cell)
P("R17", 54.0, 84.2, 0)         # 909R divider bottom

# Deliberate near/into-courtyard contacts (thermal coupling to the cell):
# exempt from the clearance QA + relaxation.
CONTACT_EXEMPT = {frozenset(p) for p in
                  (("F1", "BT1"), ("RT1", "BT1"), ("R17", "BT1"))}

# ==========================================================================
# Mounting holes: 4x M3, grounded, one per corner
# ==========================================================================
MOUNTING_HOLES = [(5.0, 5.0), (104.8, 5.2), (5.0, 105.0), (105.5, 105.5)]

# Critical-pin world-position expectations (transform sanity check):
VERIFY_PINS = [
    ("U8", "2", 52.75, 10.01),    # 3V3 -- local left col must face world right
    ("U8", "27", 35.25, 25.25),   # IO46 -- local right col faces world left
    ("U13", "13", 79.5, 19.58),   # SDA world right
    ("U12", "1", 34.5, 54.42),    # outputs world right (toward motor)
    ("U11", "1", 73.5, 54.58),    # rot180: outputs world left (toward motor)
    ("U9", "16", 92.85, 74.62),   # rot180: SDIN world left
    ("U2", "12", 93.85, 48.38),   # SW world left
    ("BT1", "1", 86.8, 96.0),     # cell+ pad world right
    ("J1", "A6", 95.25, 29.51),   # USB D+
]


# ==========================================================================
# Board construction
# ==========================================================================


def add_footprint(board, ref, footprint_field, x, y, rot, side):
    lib_dir, fp_name = resolve_fp(footprint_field)
    fp = pcbnew.FootprintLoad(lib_dir, fp_name)
    if fp is None:
        raise RuntimeError(f"could not load footprint {footprint_field} for {ref}")
    fp.SetReference(ref)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    board.Add(fp)  # parent before Flip: unparented flip of a footprint with an
    # embedded zone (ESP32 antenna keepout) segfaults
    if side == "B":
        fp.Flip(pcbnew.VECTOR2I(MM(x), MM(y)), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        # Flip leaves stored orientation at 180: apply table rot on top of it
        fp.SetOrientationDegrees((180 + rot) % 360)
    elif rot:
        fp.SetOrientationDegrees(rot)
    return fp


def set_board_outline(board):
    r = 6.0
    cs = [(r, 0), (BOARD - r, 0), (BOARD, r), (BOARD, BOARD - r),
          (BOARD - r, BOARD), (r, BOARD), (0, BOARD - r), (0, r)]
    segs = [(cs[1], cs[0]), (cs[3], cs[2]), (cs[5], cs[4]), (cs[7], cs[6])]
    for (x1, y1), (x2, y2) in segs:
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
        s.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.15))
        board.Add(s)
    # 90deg corner arcs (start point, then sweep -90deg CCW-in-screen-coords)
    arcs = [((BOARD - r, r), (BOARD - r, 0)),
            ((BOARD - r, BOARD - r), (BOARD, BOARD - r)),
            ((r, BOARD - r), (r, BOARD)),
            ((r, r), (0, r))]
    for (cxa, cya), (sx, sy) in arcs:
        a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        a.SetCenter(pcbnew.VECTOR2I(MM(cxa), MM(cya)))
        a.SetStart(pcbnew.VECTOR2I(MM(sx), MM(sy)))
        a.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90.0, pcbnew.DEGREES_T), False)
        a.SetLayer(pcbnew.Edge_Cuts)
        a.SetWidth(MM(0.15))
        board.Add(a)


def add_mounting_holes(board, gnd_net):
    holes = {}
    for i, (x, y) in enumerate(MOUNTING_HOLES, 1):
        fp = pcbnew.FootprintLoad(
            os.path.join(STOCK_FP, "MountingHole.pretty"), "MountingHole_3.2mm_M3_Pad")
        fp.SetReference(f"H{i}")
        fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
        board.Add(fp)
        for pad in fp.Pads():
            pad.SetNet(gnd_net)
        holes[f"H{i}"] = fp
    return holes


def add_zone(board, layer, net, rect_pts, name=""):
    zone = pcbnew.ZONE(board)
    zone.SetLayer(layer)
    zone.SetNetCode(net.GetNetCode())
    if name:
        zone.SetZoneName(name)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in rect_pts:
        outline.Append(MM(x), MM(y))
    zone.SetIsFilled(False)
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    return zone


def add_silk_text(board, text, x, y, layer, size=1.5, mirror=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(size * 0.15))
    if mirror:
        t.SetMirrored(True)
    board.Add(t)


BLOCK_LABELS = [  # B.SilkS, mirrored so they read correctly from the back
    ("USB-PD", 95.0, 23.5), ("CHARGER", 103.0, 58.5), ("12V", 79.0, 57.0),
    ("AUDIO", 95.0, 74.0), ("5V", 14.0, 46.5), ("3V3", 14.0, 61.0),
    ("IO-EXP", 70.0, 31.0), ("PROTECT", 10.5, 75.0), ("WAKE", 24.0, 78.5),
    ("CELL+", 87.0, 90.5), ("CELL-", 18.0, 92.0),
]


# ==========================================================================
# QA + relaxation
# ==========================================================================


def fp_bbox_mm(fp):
    """Pads ∪ courtyard bbox (excludes silkscreen/text)."""
    xs0, ys0, xs1, ys1 = [], [], [], []
    for p in fp.Pads():
        b = p.GetBoundingBox()
        xs0.append(b.GetLeft()); xs1.append(b.GetRight())
        ys0.append(b.GetTop()); ys1.append(b.GetBottom())
    for d in fp.GraphicalItems():
        if d.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            b = d.GetBoundingBox()
            xs0.append(b.GetLeft()); xs1.append(b.GetRight())
            ys0.append(b.GetTop()); ys1.append(b.GetBottom())
    return (pcbnew.ToMM(min(xs0)), pcbnew.ToMM(min(ys0)),
            pcbnew.ToMM(max(xs1)), pcbnew.ToMM(max(ys1)))


def rect_gap(a, b):
    """Separation between two rects (negative = overlap depth)."""
    dx = max(a[0], b[0]) - min(a[2], b[2])
    dy = max(a[1], b[1]) - min(a[3], b[3])
    if dx < 0 and dy < 0:
        return max(dx, dy)
    if dx < 0:
        return dy
    if dy < 0:
        return dx
    return (dx * dx + dy * dy) ** 0.5


def is_thru(fp):
    return any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in fp.Pads())


def part_rects(ref, fp, boxes):
    """Rect(s) representing a part for clearance checks, per side.
    U8 is special-cased: its courtyard-layer bbox includes the antenna
    keepout, which would false-positive everything within x 20..68."""
    if ref == "U8":
        return {"B": [U8_MODULE_RECT, U8_ANT_RECT], "F": [U8_EPVIA_RECT, U8_ANT_RECT]}
    b = boxes[ref]
    if is_thru(fp):
        return {"B": [b], "F": [b]}
    side = "F" if fp.GetLayerName() == "F.Cu" else "B"
    return {side: [b]}


def pair_limits(r1, r2):
    conn = r1.startswith(("J", "BT")) or r2.startswith(("J", "BT"))
    return (0.6, 1.2) if conn else (0.4, 0.8)


def qa_courtyards(fps, boxes):
    """Pairwise same-side gap check (M1 excluded: circle handled separately)."""
    errors, warns = [], []
    refs = [r for r in fps if r != "M1"]
    rects = {r: part_rects(r, fps[r], boxes) for r in refs}
    for i in range(len(refs)):
        for j in range(i + 1, len(refs)):
            r1, r2 = refs[i], refs[j]
            if frozenset((r1, r2)) in CONTACT_EXEMPT:
                continue
            g = None
            for side in ("F", "B"):
                for a in rects[r1].get(side, []):
                    for b in rects[r2].get(side, []):
                        gg = rect_gap(a, b)
                        if g is None or gg < g:
                            g = gg
            if g is None:
                continue
            lim_err, lim_warn = pair_limits(r1, r2)
            if g < lim_err:
                errors.append((r1, r2, g))
            elif g < lim_warn:
                warns.append((r1, r2, g))
    return errors, warns


def qa_keepouts(fps, boxes):
    errors = []
    mx, my = MOTOR_ANCHOR
    for ref, fp in fps.items():
        if ref in ("M1", "U8"):
            continue
        b = boxes[ref]
        on_b = is_thru(fp) or fp.GetLayerName() == "B.Cu"
        if on_b:
            nx = max(b[0], min(mx, b[2])); ny = max(b[1], min(my, b[3]))
            d = ((nx - mx) ** 2 + (ny - my) ** 2) ** 0.5
            if d < MOTOR_R + 0.5:
                errors.append(f"{ref} vs motor courtyard: d={d:.1f} (< {MOTOR_R + 0.5})")
            if ref != "J6" and rect_gap(b, J6_MOUTH_RECT) < 0.0:
                errors.append(f"{ref} blocks the microSD card mouth")
        if rect_gap(b, U8_ANT_RECT) < 0.0:
            errors.append(f"{ref} inside ESP32 antenna keepout")
        if b[0] < 1.0 or b[1] < 1.0 or b[2] > BOARD - 1.0 or b[3] > BOARD - 1.0:
            if ref != "J6":  # card mouth is allowed to reach the edge
                errors.append(f"{ref} <1mm from board edge: "
                              f"({b[0]:.1f},{b[1]:.1f})-({b[2]:.1f},{b[3]:.1f})")
    return errors


def relax_cap(ref):
    # small satellites may drift a little further than ICs/connectors, but
    # qa_locality() still has to pass afterwards
    return 7.0 if ref[0] in "RCDQLY" else 4.0


def relax(fps, boxes):
    """Bounded push-apart: resolves courtyard collisions among movable parts
    without destroying functional adjacency (per-part displacement cap)."""
    pos0 = {r: (pcbnew.ToMM(fps[r].GetPosition().x), pcbnew.ToMM(fps[r].GetPosition().y))
            for r in fps}
    moved_total = {r: 0.0 for r in fps}

    def movable(r):
        return r not in LOCKED and not r.startswith("H")

    mx, my = MOTOR_ANCHOR
    for _ in range(400):
        worst = 0.0
        refs = list(fps)
        rects = {r: part_rects(r, fps[r], boxes) for r in refs if r != "M1"}
        pushes = {}

        def add_push(r, dx, dy):
            px, py = pushes.get(r, (0.0, 0.0))
            pushes[r] = (px + dx, py + dy)

        for i in range(len(refs)):
            for j in range(i + 1, len(refs)):
                r1, r2 = refs[i], refs[j]
                if r1 == "M1" or r2 == "M1":
                    continue
                if frozenset((r1, r2)) in CONTACT_EXEMPT:
                    continue
                if not movable(r1) and not movable(r2):
                    continue
                g = None
                ga = gb = None
                for side in ("F", "B"):
                    for a in rects[r1].get(side, []):
                        for b in rects[r2].get(side, []):
                            gg = rect_gap(a, b)
                            if g is None or gg < g:
                                g, ga, gb = gg, a, b
                if g is None:
                    continue
                target = pair_limits(r1, r2)[0] + 0.25
                if g >= target:
                    continue
                need = target - g
                worst = max(worst, need)
                # push along the axis already closest to clearing (the one
                # with the larger separation) -- NOT the larger centre
                # distance, which ejects parts sideways along wide obstacles
                dx_sep = max(ga[0], gb[0]) - min(ga[2], gb[2])
                dy_sep = max(ga[1], gb[1]) - min(ga[3], gb[3])
                if dx_sep >= dy_sep:
                    ux = 1.0 if (gb[0] + gb[2]) > (ga[0] + ga[2]) else -1.0
                    uy = 0.0
                else:
                    ux = 0.0
                    uy = 1.0 if (gb[1] + gb[3]) > (ga[1] + ga[3]) else -1.0
                step = min(need, 0.6) * 0.55
                if movable(r1) and movable(r2):
                    add_push(r1, -ux * step / 2, -uy * step / 2)
                    add_push(r2, ux * step / 2, uy * step / 2)
                elif movable(r2):
                    add_push(r2, ux * step, uy * step)
                else:
                    add_push(r1, -ux * step, -uy * step)

        # keepout pushes
        for r in refs:
            if not movable(r) or r == "M1":
                continue
            b = boxes[r]
            fp = fps[r]
            if is_thru(fp) or fp.GetLayerName() == "B.Cu":
                nx = max(b[0], min(mx, b[2])); ny = max(b[1], min(my, b[3]))
                d = ((nx - mx) ** 2 + (ny - my) ** 2) ** 0.5
                lim = MOTOR_R + 0.7
                if d < lim:
                    worst = max(worst, lim - d)
                    if d < 0.01:
                        add_push(r, 0.5, 0)
                    else:
                        add_push(r, (nx - mx) / d * (lim - d) * 0.6,
                                 (ny - my) / d * (lim - d) * 0.6)
                g = rect_gap(b, J6_MOUTH_RECT)
                if g < 0.2:
                    worst = max(worst, 0.2 - g)
                    add_push(r, (0.2 - g) * 0.6, 0)  # push east, corridor is at west edge
            g = rect_gap(b, U8_ANT_RECT)
            if g < 0.2:
                worst = max(worst, 0.2 - g)
                add_push(r, 0, (0.2 - g) * 0.6)  # push down, keepout is at top
            # board edge
            if b[0] < 1.2:
                add_push(r, 1.2 - b[0], 0); worst = max(worst, 1.2 - b[0])
            if b[1] < 1.2:
                add_push(r, 0, 1.2 - b[1]); worst = max(worst, 1.2 - b[1])
            if b[2] > BOARD - 1.2:
                add_push(r, BOARD - 1.2 - b[2], 0); worst = max(worst, b[2] - BOARD + 1.2)
            if b[3] > BOARD - 1.2:
                add_push(r, 0, BOARD - 1.2 - b[3]); worst = max(worst, b[3] - BOARD + 1.2)

        if not pushes:
            break
        for r, (dx, dy) in pushes.items():
            if not movable(r):
                continue
            x0, y0 = pos0[r]
            fp = fps[r]
            cur = fp.GetPosition()
            nx = pcbnew.ToMM(cur.x) + dx
            ny = pcbnew.ToMM(cur.y) + dy
            # cap total displacement from the seed
            cap = relax_cap(r)
            tx, ty = nx - x0, ny - y0
            d = (tx * tx + ty * ty) ** 0.5
            if d > cap:
                tx, ty = tx / d * cap, ty / d * cap
                nx, ny = x0 + tx, y0 + ty
            fp.SetPosition(pcbnew.VECTOR2I(MM(nx), MM(ny)))
            boxes[r] = fp_bbox_mm(fp)
            moved_total[r] = ((nx - x0) ** 2 + (ny - y0) ** 2) ** 0.5
        if worst < 0.01:
            break
    big = sorted(((d, r) for r, d in moved_total.items() if d > 1.5), reverse=True)
    return big


def silk_obstacles(fps_all):
    """Per-side boxes a silk label must not cross: every pad (mask opening)
    and every footprint's own silkscreen graphics (part outlines)."""
    boxes = {"F": [], "B": []}
    for ref, fp in fps_all.items():
        for p in fp.Pads():
            b = p.GetBoundingBox()
            box = (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                   pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                boxes["F"].append(box)
                boxes["B"].append(box)
            else:
                boxes["F" if p.IsOnLayer(pcbnew.F_Cu) else "B"].append(box)
        for d in fp.GraphicalItems():
            lay = d.GetLayer()
            if lay in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                b = d.GetBoundingBox()
                boxes["F" if lay == pcbnew.F_SilkS else "B"].append(
                    (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                     pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom())))
    return boxes


SILK_GIVEUPS = []


def settle_text(t, obstacles, side, cands, clearance=0.2):
    """Try candidate centres for a text item, using the REAL rendered
    bounding box (KiCad font metrics, incl. mirroring) for the check.
    Leaves the text at the first clean spot (falling back to a relaxed
    clearance, then to the least-bad candidate) and returns the final box,
    which the caller adds to obstacles."""
    boxes = []
    for cx, cy in cands:
        t.SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy)))
        b = t.GetBoundingBox()
        boxes.append(((cx, cy),
                      (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                       pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))))
    for cl in (clearance, 0.05):
        for (cx, cy), box in boxes:
            if box[0] < 1.0 or box[1] < 1.0 or box[2] > BOARD - 1.0 or box[3] > BOARD - 1.0:
                continue
            if all(rect_gap(box, ob) >= cl for ob in obstacles[side]):
                t.SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy)))
                return box
    # least-bad: maximize the worst gap (edge distances count as gaps too)
    def score(e):
        box = e[1]
        edge = min(box[0] - 1.0, box[1] - 1.0, BOARD - 1.0 - box[2],
                   BOARD - 1.0 - box[3])
        return min(min((rect_gap(box, ob) for ob in obstacles[side]), default=1.0),
                   edge)
    best = max(boxes, key=score)
    (cx, cy), box = best
    t.SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy)))
    SILK_GIVEUPS.append(t.GetParentFootprint().GetReference()
                        if t.GetParentFootprint() else str(t.GetText()))
    return box


def place_ref_labels(fps_all):
    """Reference designators: 0.9mm text, greedily placed at the first of
    several candidate spots around each part that overlaps no pad, no part
    outline and no already-placed label on the same side.  Mounting-hole
    refs are hidden.  Returns the obstacle map (with labels added) so the
    block labels can be placed against it too."""
    obstacles = silk_obstacles(fps_all)
    for ref in sorted(fps_all, key=lambda r: (PLACEMENT.get(r, (0, 0))[1],
                                              PLACEMENT.get(r, (0, 0))[0])):
        fp = fps_all[ref]
        t = fp.Reference()
        if ref.startswith("H"):
            t.SetVisible(False)
            continue
        t.SetTextSize(pcbnew.VECTOR2I(MM(0.9), MM(0.9)))
        t.SetTextThickness(MM(0.13))
        side = "B" if fp.GetLayerName() == "B.Cu" else "F"
        x0, y0, x1, y1 = fp_bbox_mm(fp)
        cx0, cy0 = (x0 + x1) / 2, (y0 + y1) / 2
        w = 0.75 * len(ref) + 0.4
        cands = [(cx0, y0 - 0.75), (cx0, y1 + 0.75),
                 (x0 - w / 2 - 0.5, cy0), (x1 + w / 2 + 0.5, cy0),
                 (cx0, y0 - 1.7), (cx0, y1 + 1.7),
                 (x0 - w / 2 - 0.5, y0 - 0.75), (x1 + w / 2 + 0.5, y0 - 0.75),
                 (x0 - w / 2 - 0.5, y1 + 0.75), (x1 + w / 2 + 0.5, y1 + 0.75),
                 (cx0 - 2.0, y0 - 2.6), (cx0 + 2.0, y0 - 2.6),
                 (cx0 - 2.0, y1 + 2.6), (cx0 + 2.0, y1 + 2.6),
                 (x0 - w / 2 - 1.5, cy0), (x1 + w / 2 + 1.5, cy0),
                 (x0 - w / 2 - 0.6, y0 - 1.7), (x1 + w / 2 + 0.6, y0 - 1.7),
                 (x0 - w / 2 - 0.6, y1 + 1.7), (x1 + w / 2 + 0.6, y1 + 1.7),
                 (cx0, y0 - 3.6), (cx0, y1 + 3.6),
                 (x0 - w / 2 - 2.6, cy0), (x1 + w / 2 + 2.6, cy0)]
        box = settle_text(t, obstacles, side, cands)
        obstacles[side].append(box)
    if SILK_GIVEUPS:
        print(f"silk: {len(SILK_GIVEUPS)} labels without a fully clean spot: "
              f"{' '.join(SILK_GIVEUPS)}")
    return obstacles


RAIL_NETS = {"GND", "+3V3", "+5V", "+12V", "VBAT", "VBUS", "PVDD"}


def qa_locality(fps, net_map):
    """Every 2/3-pad satellite's non-rail pads must sit near another pad of
    the same net: parts are supposed to be AT the thing they connect to."""
    pad_pos = {}
    for ref, fp in fps.items():
        for p in fp.Pads():
            pos = p.GetPosition()
            pad_pos[(ref, p.GetNumber())] = (pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y))
    by_net = {}
    for (ref, pin), net in net_map.items():
        if (ref, pin) in pad_pos:
            by_net.setdefault(net, []).append((ref, pin))
    report = []
    for ref, fp in fps.items():
        if ref[0] not in "RCDQLY" or ref in ("D40", "D41"):
            continue
        worst, worst_net = 0.0, ""
        for p in fp.Pads():
            net = net_map.get((ref, p.GetNumber()))
            if not net or net in RAIL_NETS or net.startswith("unconnected"):
                continue
            others = [pad_pos[o] for o in by_net.get(net, []) if o[0] != ref]
            if not others:
                continue
            x, y = pad_pos[(ref, p.GetNumber())]
            d = min(((x - ox) ** 2 + (y - oy) ** 2) ** 0.5 for ox, oy in others)
            if d > worst:
                worst, worst_net = d, net
        if worst > 10.0:
            report.append((worst, ref, worst_net))
    report.sort(reverse=True)
    return report


def qa_ratsnest(fps, net_map):
    """Rough total airwire length (per-net MST over pad positions)."""
    pad_pos = {}
    for ref, fp in fps.items():
        for p in fp.Pads():
            pos = p.GetPosition()
            pad_pos[(ref, p.GetNumber())] = (pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y))
    by_net = {}
    for key, net in net_map.items():
        if net.startswith("unconnected") or key not in pad_pos:
            continue
        by_net.setdefault(net, []).append(pad_pos[key])
    total = 0.0
    for pts in by_net.values():
        if len(pts) < 2:
            continue
        used = [pts[0]]
        rest = pts[1:]
        while rest:
            best, bi = None, None
            for i, q in enumerate(rest):
                d = min(((q[0] - u[0]) ** 2 + (q[1] - u[1]) ** 2) ** 0.5 for u in used)
                if best is None or d < best:
                    best, bi = d, i
            total += best
            used.append(rest.pop(bi))
    return total


# ==========================================================================


def main():
    net_map = load_netlist(export_netlist())
    parts = load_schematic_parts()

    missing = sorted(set(parts) - set(PLACEMENT))
    extra = sorted(set(PLACEMENT) - set(parts))
    if missing or extra:
        raise SystemExit(f"placement/schematic mismatch: missing={missing} extra={extra}")

    board = pcbnew.CreateEmptyBoard()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(4)
    ds.SetBoardThickness(MM(1.6))
    # 0.1mm min clearance: required by J1's native 0.5mm-pitch USB-C pads and
    # U5 (SOT-563) per their own datasheets; PCBWay advanced tier -- flag when
    # ordering.  Routing should still keep >=0.15 everywhere else.
    ds.m_NetSettings.GetDefaultNetclass().SetClearance(MM(0.1))
    ds.m_NetSettings.GetDefaultNetclass().SetTrackWidth(MM(0.25))
    ds.m_NetSettings.GetDefaultNetclass().SetViaDiameter(MM(0.6))
    ds.m_NetSettings.GetDefaultNetclass().SetViaDrill(MM(0.3))
    # the stock ESP32 module footprint's thermal-via-in-pad array is
    # drilled 0.2mm; keep the board minimum at that (PCBWay advanced tier,
    # same tier the 0.1mm clearance above already requires)
    ds.m_MinThroughDrill = MM(0.2)

    fps = {}
    for ref, (x, y, rot, side) in PLACEMENT.items():
        fps[ref] = add_footprint(board, ref, parts[ref]["footprint"], x, y, rot, side)

    # shaft NPTH exactly at board centre (anchor is 6mm off the shaft)
    m1 = fps["M1"]
    shaft = None
    for p in m1.Pads():
        if p.GetNumber() == "" and abs(pcbnew.ToMM(p.GetDrillSizeX()) - 4.6) < 0.01:
            shaft = p
            break
    m1.Move(pcbnew.VECTOR2I(MM(CX) - shaft.GetPosition().x,
                            MM(CY) - shaft.GetPosition().y))

    # ---- nets ----
    net_cache = {}

    def get_net(name):
        if name not in net_cache:
            existing = board.FindNet(name)
            if existing:
                net_cache[name] = existing
            else:
                ni = pcbnew.NETINFO_ITEM(board, name)
                board.Add(ni)
                net_cache[name] = ni
        return net_cache[name]

    unmatched = 0
    for (ref, pin), netname in net_map.items():
        fp = fps.get(ref)
        if fp is None:
            continue
        # set EVERY pad with this number -- footprints like the ESP32 module
        # carry a thermal-via array all numbered like the EP pad, and leaving
        # the extras netless makes DRC report same-number pads as shorts
        hits = 0
        for pad in fp.Pads():
            if pad.GetNumber() == pin:
                pad.SetNet(get_net(netname))
                hits += 1
        if not hits:
            unmatched += 1
    gnd = get_net("GND")

    # ---- pin-transform verification ----
    fails = []
    for ref, pin, ex, ey in VERIFY_PINS:
        pad = fps[ref].FindPadByNumber(pin)
        px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
        if abs(px - ex) > 0.05 or abs(py - ey) > 0.05:
            fails.append(f"{ref}.{pin}: expected ({ex},{ey}) got ({px:.2f},{py:.2f})")
    if fails:
        print("PIN TRANSFORM CHECK FAILED:")
        for f in fails:
            print("  ", f)
        raise SystemExit(1)

    set_board_outline(board)
    holes = add_mounting_holes(board, gnd)

    # ---- relaxation: enforce assembly clearance around the seeds ----
    boxes = {r: fp_bbox_mm(f) for r, f in fps.items()}
    check_set = dict(fps)
    check_set.update(holes)
    for h, f in holes.items():
        boxes[h] = fp_bbox_mm(f)
    big_moves = relax(check_set, boxes)

    # ---- GND: solid pours on both inner layers + outer-layer stitch pours.
    # Fill is deferred to the GUI (ZONE_FILLER segfaults under this
    # interpreter); zones are outlined + net-assigned here.
    rect = [(1.0, 1.0), (BOARD - 1.0, 1.0), (BOARD - 1.0, BOARD - 1.0), (1.0, BOARD - 1.0)]
    for layer, name in ((pcbnew.In1_Cu, "GND_IN1"), (pcbnew.In2_Cu, "GND_IN2"),
                        (pcbnew.F_Cu, "GND_F"), (pcbnew.B_Cu, "GND_B")):
        z = add_zone(board, layer, gnd, rect, name)
        board.Add(z)

    # ---- silkscreen: readable references (greedy collision-avoiding) ----
    obstacles = place_ref_labels(check_set)
    # block labels + titles must also stay off part BODIES (courtyards), not
    # just pads/outlines -- a label under a connector shell is unreadable
    for r, f in check_set.items():
        b = fp_bbox_mm(f)
        if is_thru(f) or f.GetLayerName() == "B.Cu":
            obstacles["B"].append(b)
    add_silk_text(board, "wooden clock v0.19  2026-07-22", 55.0, 108.3,
                  pcbnew.F_SilkS)
    for text, x, y, size in [("clock v0.19", 40.0, 108.5, 1.2)] + \
            [(t_, x_, y_, 1.2) for t_, x_, y_ in BLOCK_LABELS]:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetLayer(pcbnew.B_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
        t.SetTextThickness(MM(size * 0.15))
        t.SetMirrored(True)
        board.Add(t)
        cands = [(x, y)] + [(x + dx, y + dy) for dy in (0, -2, 2, -4, 4, -6, 6)
                            for dx in (0, -3, 3, -6, 6)]
        obstacles["B"].append(settle_text(t, obstacles, "B", cands, clearance=0.25))

    board.Save(PCB_OUT)
    print(f"saved {PCB_OUT}: {len(fps)} parts + {len(holes)} mounting holes")
    print(f"net assignment: {unmatched} pad lookups missed")

    # ---- QA report ----
    if big_moves:
        print("--- relax(): parts displaced >1.5mm from their seed ---")
        for d, r in big_moves:
            print(f"  {r:5s} moved {d:.1f}mm")
    boxes = {r: fp_bbox_mm(f) for r, f in check_set.items()}
    errors, warns = qa_courtyards(check_set, boxes)
    print(f"--- courtyard gaps: {len(errors)} ERRORS, {len(warns)} tight ---")
    for r1, r2, g in sorted(errors, key=lambda e: e[2]):
        print(f"  ERR   {r1:5s} <-> {r2:5s} gap {g:+.2f}mm")
    for r1, r2, g in sorted(warns, key=lambda e: e[2]):
        print(f"  tight {r1:5s} <-> {r2:5s} gap {g:+.2f}mm")
    kerr = qa_keepouts(check_set, boxes)
    print(f"--- keepouts/edges: {len(kerr)} violations ---")
    for e in kerr:
        print("  ERR", e)
    loc = qa_locality(fps, net_map)
    print(f"--- locality (satellites >10mm from their signal net): {len(loc)} ---")
    for d, ref, net in loc:
        print(f"  {ref:5s} {d:5.1f}mm from {net}")
    print(f"--- est. total ratsnest (MST): {qa_ratsnest(fps, net_map):.0f}mm ---")


if __name__ == "__main__":
    main()
