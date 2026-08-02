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
DigiKey stock/price/lifecycle re-verified 2026-08-01.
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
    "J1": ("B6B-ZR(LF)(SN)", "JST Sales America",
           "JST ZH 1.50mm 6-pos TH vertical",
           "CONN HEADER VERT 6POS 1.50MM",
           "Mates main-board J7 1:1 over the 6-way ZH harness (same part both "
           "ends). The only through-hole part on this board — hand-solder it "
           "after reflow, or have it fitted as a second operation"),

    "U1": ("BNO085", "CEVA Technologies", "LGA-28 5.2x3.8x1.1mm, 0.5mm pitch",
           "IC IMU 9-AXIS SH-2 FUSION I2C LGA-28",
           "DigiKey 1888-1006-1-ND (cut tape) — $13.57, Active, 324 pcs, "
           "16 wk factory lead (2026-08-01). Land pattern is the vendor one in "
           "sensor.pretty (verified against BNO08X Fig. 7-2), NOT KiCad's "
           "generic LGA-28"),
    "U2": ("BME688", "Bosch Sensortec", "LGA-8 3.0x3.0x0.93mm",
           "SENSOR TEMP/RH/PRESSURE/GAS I2C LGA-8",
           "DigiKey 828-BME688CT-ND — $8.99 single, Active, 9.3k pcs "
           "(2026-08-01; datasheet/README.md §12's ~$5 is the 3k reel price). "
           "Gas sensor: do not conformal-coat, keep the vent clear"),
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
# until stamp_bom.py set the flags, so an assembly house would have fitted them
# — and each pair fitted together ties +3V3 to GND through 0 ohm.
DNP = {
    "R4":  "alternate BNO085 address strap — fit INSTEAD OF R3 for 0x4B "
           "(both fitted = +3V3 shorted to GND through 0 ohm)",
    "R11": "alternate BME688 address strap — fit INSTEAD OF R10 for 0x76 "
           "(both fitted = +3V3 shorted to GND through 0 ohm)",
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
