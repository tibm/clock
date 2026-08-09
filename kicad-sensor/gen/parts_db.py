"""Manufacturer part data for the sensor board's BOM (MPN / Manufacturer /
Package / Description / Notes) — the main board's `parts_db.py`, scoped to this
project.

This board is **ordered fab-assembled** (every sensor is leadless), so its BOM
is not a shopping list for a human with an iron: it goes to an assembly house,
which needs each line to be unambiguous (`MPN` + `Package`) *and* needs to know
which parts must **not** be fitted.  `stamp_bom.py` writes the fields below into
`sensor.kicad_sch` and `sensor.kicad_pcb`, plus the `DNP` /
`EXCLUDE_FROM_BOM` flags.

The generic passive tables (0603 resistors, ceramic caps, case codes) are
**imported from the main board's db** so the two boards order the same physical
parts; only the per-reference table is local.  It must never fall through to
the main board's — `J1`/`U1`/`U2`/`U3`/`Y1` exist on both boards and mean
different parts there.

Sources: `../README.md`, `../../datasheet/README.md` §12/§13/§14b/§16.
DigiKey stock/price/lifecycle re-verified 2026-08-01; J1's tail-length
variants re-checked 2026-08-07 (see its entry).
"""
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN_DB = os.path.abspath(
    os.path.join(HERE, "..", "..", "kicad", "gen", "parts_db.py"))


def _main_parts_db():
    """Load ../../kicad/gen/parts_db.py by path — it shares this file's
    basename, so a plain `import parts_db` would find this one instead."""
    spec = importlib.util.spec_from_file_location("clock_parts_db", MAIN_DB)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_MAIN = _main_parts_db()
SUB_OK = _MAIN.SUB_OK
_R = _MAIN._R          # 0603 1 % Yageo RC series, keyed by value
_C = _MAIN._C          # ceramics, keyed by (value, case)
_CASE = _MAIN._CASE    # KiCad footprint name -> BOM package string

# --- everything else, by reference ------------------------------------------
_REF = {
    # (mpn, manufacturer, package, description, notes)
    # Tail length, checked 2026-08-07 (REVIEW.md #2): the ZH datasheet says
    # 2.7 mm suits 0.6-1.2 mm boards and 3.4 mm suits 1.6 mm, which is this
    # board -- but B6B-ZR-3.4(LF)(SN) (DigiKey 455-B6B-ZR-3.4-ND, $0.121,
    # Active) is NOT stocked: made to order, MOQ 2,000, 16-week lead.  The
    # 2.7 mm part is $0.24 with 9,478 on the shelf and MOQ 1.  Keeping it is
    # a deliberate deviation: a 2.7 mm post in a 1.6 mm board still leaves
    # 1.1 mm protruding, which is a normal, fully-wetted TH joint (IPC-A-610
    # wants the lead visible, not a specific length) -- the wafer seats on
    # the board either way.  Revisit only if a run ever justifies 2,000 pcs.
    "J1": ("B6B-ZR(LF)(SN)", "JST Sales America",
           "JST ZH 1.50mm 6-pos TH vertical, 2.7mm tail",
           "CONN HEADER VERT 6POS 1.50MM",
           "DigiKey 455-B6B-ZR-ND — $0.24, Active, 9,478 pcs (2026-08-07). "
           "Mates main-board J7 1:1 over the 6-way ZH harness (same part "
           "both ends); ZHR-6 housing + A06ZR pre-crimped cable. JST specs "
           "the 3.4 mm tail (B6B-ZR-3.4) for 1.6 mm board thickness, but "
           "that variant is made-to-order only (MOQ 2,000, 16 wk) — 2.7 mm "
           "leaves 1.1 mm protruding here, which solders fine; deviation "
           "accepted, see ../PCB_NOTES.md. The only through-hole part on "
           "this board — hand-solder it after reflow, or have it fitted as "
           "a third operation"),

    "U1": ("BNO085", "CEVA Technologies", "LGA-28 5.2x3.8x1.1mm, 0.5mm pitch",
           "IC IMU 9-AXIS SH-2 FUSION I2C LGA-28",
           "DigiKey 1888-1006-1-ND (cut tape) — $13.57, Active, 324 pcs, "
           "16 wk factory lead (2026-08-01). Land pattern is the vendor one in "
           "sensor.pretty (verified against BNO08X Fig. 7-2), NOT KiCad's "
           "generic LGA-28"),
    # The handling rules are in the BOM line on purpose (2026-08-08): this is
    # the one part on the board that a routine, correctly-executed assembly
    # process can permanently ruin, and it does not fail when it happens -- a
    # washed or siloxane-exposed hotplate just reads a wrong VOC baseline for
    # ever, which no electrical test at the assembler would catch.  Putting it
    # in Notes means it travels with the exported CSV to the assembly house
    # instead of living only in a README they will never open.  Bosch
    # publishes no BME688-specific HSMI (checked 2026-08-07); the BME680 one
    # governs -- same LGA-8 3x3 package, same 0.35 mm lid vent, same MOX
    # element -- and is filed as datasheet/sensor_env_bme68x_hsmi.pdf.
    "U2": ("BME688", "Bosch Sensortec", "LGA-8 3.0x3.0x0.93mm",
           "SENSOR TEMP/RH/PRESSURE/GAS I2C LGA-8",
           "DigiKey 828-BME688CT-ND — $8.99 single, Active, 9.3k pcs "
           "(2026-08-01; datasheet/README.md §12's ~$5 is the 3k reel price). "
           "*** HANDLING IS CONTRACTUAL — GAS SENSOR, SEE BOSCH HSMI "
           "(BST-BME680-HS000-06, supplied with this package). "
           "(1) MSL 1, no bake. "
           "(2) Peak reflow 260 C for 20-40 s, MAX 3 REFLOW CYCLES — this "
           "board is double-sided, so U2 already sees two. "
           "(3) MINIMUM 50 um SOLDER HEIGHT AFTER REFLOW — this is a stencil "
           "thickness decision and must be confirmed BEFORE the run; it is "
           "what mechanically decouples the die from the board. "
           "(4) NO AQUEOUS WASH and no flux over the vent hole; if any "
           "cleaning step is used the vent must first be covered with a "
           "silicone-free protective layer. No-clean process preferred. "
           "(5) NO SILICONE / SILOXANE ANYWHERE NEAR THIS PART — gloves, "
           "adhesives, coatings, packaging. Permanent VOC poisoning. "
           "(6) No conformal coat, no underfill, no ultrasonic welding, "
           "nothing sharp in the vent, no rear-side handling. "
           "A part that has been washed, fluxed over or siloxane-exposed "
           "does NOT fail electrically — it reads a wrong gas baseline for "
           "ever, so incoming test will not catch it. ***"),
    "U3": ("TSL25911FN", "ams-OSRAM", "WFDFN-6 2.0x2.0mm",
           "SENSOR OPTICAL AMBIENT LIGHT I2C WFDFN-6",
           "DigiKey TSL25911FNTR-ND (product 4162547) — $1.74, Active, 29k pcs "
           "(2026-08-01). Optical window: no coating, no label over U3"),

    "Y1": ("ABS07-32.768KHZ-T", "Abracon", "SMD 3.2x1.5mm 2-pin",
           "CRYSTAL 32.768KHZ 12.5PF SMD",
           "Same part as the main board's RTC crystal — one line covers both "
           "boards"),
}

# Mounting holes (H*), power symbols (#*) and the bare-copper test pads (TP*)
# carry no orderable part, so stamp_bom.py never looks one up for them.
SKIP_PREFIXES = ("H", "#", "TP")

# Test pads are board features, not parts. The PCB footprints already say so
# (`exclude_from_bom exclude_from_pos_files`); stamp_bom.py makes the SCHEMATIC
# agree, so an eeschema/kicad-cli BOM matches the PCBWay-plug-in one and a
# future "Update PCB from Schematic" cannot put them back.
EXCLUDE_FROM_BOM = {
    "TP1": "bare copper pad on BNO_NRST (DFU entry)",
    "TP2": "bare copper pad on BNO_BOOTN (DFU entry)",
}

# **Do not populate.** Both are the *alternate* I2C-address strap, drawn next
# to the fitted one; the value string says "(DNP)" but nothing in the files did
# until stamp_bom.py set the flags, so an assembly house would have fitted
# them.  Since v0.2 the straps are 10k rather than 0R, so that slip costs
# 330 uA instead of shorting the main board's +3V3 rail to GND — the flags
# below are still the primary guard, the value is the fail-safe under them.
DNP = {
    "R4":  "alternate BNO085 address strap — fit INSTEAD OF R3 for 0x4B "
           "(both fitted = +3V3 to GND through 20k, 330 uA, wrong address)",
    "R11": "alternate BME688 address strap — fit INSTEAD OF R10 for 0x76 "
           "(both fitted = +3V3 to GND through 20k, 330 uA, wrong address)",
}

# No Value-field corrections / Value-text moves are needed on this board; the
# keys exist because the shared stamper reads them.
VALUES = {}
VALUE_POS = {}

_DNP_SUFFIX = " (DNP)"


def part_for(ref, value, footprint):
    """-> dict(MPN, Manufacturer, Package, Description, Notes) or None."""
    if ref in _REF:
        mpn, mfr, pkg, desc, notes = _REF[ref]
        return {"MPN": mpn, "Manufacturer": mfr, "Package": pkg,
                "Description": desc, "Notes": notes}

    # "0R (DNP)" is one physical part with a fitting instruction attached: look
    # the MPN up on the value alone, and put the instruction in Notes.
    if value.endswith(_DNP_SUFFIX):
        value = value[:-len(_DNP_SUFFIX)]
    dnp = DNP.get(ref)
    notes = ("DO NOT POPULATE — " + dnp) if dnp else SUB_OK

    case = _CASE.get(footprint.split(":")[-1], "")

    if ref.startswith("R") and value in _R:
        mpn, desc = _R[value]
        return {"MPN": mpn, "Manufacturer": "YAGEO", "Package": case or "0603",
                "Description": desc, "Notes": notes}

    if ref.startswith("C") and (value, case) in _C:
        mpn, mfr, desc = _C[(value, case)]
        return {"MPN": mpn, "Manufacturer": mfr, "Package": case,
                "Description": desc, "Notes": notes}

    return None
