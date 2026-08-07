"""Manufacturer part data for the BOM (MPN / Manufacturer / Package / Description).

The KiCad symbols + footprints only carry Value + Footprint, which is not enough
for an assembly house: PCBWay's BOM template wants **Package** and **Mfg Part #**
so the part is uniquely identifiable.  `stamp_bom.py` writes the fields below into
`clock.kicad_sch` and `clock.kicad_pcb`; the PCBWay KiCad plug-in then picks them
up (it reads footprint fields named `MPN` / `Package`, and emits any other
footprint field as an extra BOM column).

Sources: README.md §16b/§16c for the locked parts; power_values.md for the
voltage/dielectric each passive has to survive.  Verified on DigiKey 2026-07-27.

Every entry is (mpn, manufacturer, package, description, notes).
"""

SUB_OK = "Substitution OK - same value/tolerance/voltage/dielectric/size"

# --- 0603 thick-film 1 % 1/10 W chip resistors (Yageo RC series) -------------
_R = {
    "0R":     ("RC0603JR-070RL",    "RES 0 OHM JUMPER 1/10W 0603"),
    "100R":   ("RC0603FR-07100RL",  "RES 100 OHM 1% 1/10W 0603"),
    "200R":   ("RC0603FR-07200RL",  "RES 200 OHM 1% 1/10W 0603"),
    "150R":   ("RC0603FR-07150RL",  "RES 150 OHM 1% 1/10W 0603"),
    "330R":   ("RC0603FR-07330RL",  "RES 330 OHM 1% 1/10W 0603"),
    "909R":   ("RC0603FR-07909RL",  "RES 909 OHM 1% 1/10W 0603"),
    "1k":     ("RC0603FR-071KL",    "RES 1K OHM 1% 1/10W 0603"),
    "2k":     ("RC0603FR-072KL",    "RES 2K OHM 1% 1/10W 0603"),
    "2.55k":  ("RC0603FR-072K55L",  "RES 2.55K OHM 1% 1/10W 0603"),
    "4.7k":   ("RC0603FR-074K7L",   "RES 4.7K OHM 1% 1/10W 0603"),
    "10k":    ("RC0603FR-0710KL",   "RES 10K OHM 1% 1/10W 0603"),
    "20k":    ("RC0603FR-0720KL",   "RES 20K OHM 1% 1/10W 0603"),
    "45.3k":  ("RC0603FR-0745K3L",  "RES 45.3K OHM 1% 1/10W 0603"),
    "56k":    ("RC0603FR-0756KL",   "RES 56K OHM 1% 1/10W 0603"),
    "86.6k":  ("RC0603FR-0786K6L",  "RES 86.6K OHM 1% 1/10W 0603"),
    "95.3k":  ("RC0603FR-0795K3L",  "RES 95.3K OHM 1% 1/10W 0603"),
    "100k":   ("RC0603FR-07100KL",  "RES 100K OHM 1% 1/10W 0603"),
    "200k":   ("RC0603FR-07200KL",  "RES 200K OHM 1% 1/10W 0603"),
    "316k":   ("RC0603FR-07316KL",  "RES 316K OHM 1% 1/10W 0603"),
    "453k":   ("RC0603FR-07453KL",  "RES 453K OHM 1% 1/10W 0603"),
    "732k":   ("RC0603FR-07732KL",  "RES 732K OHM 1% 1/10W 0603"),
    "976k":   ("RC0603FR-07976KL",  "RES 976K OHM 1% 1/10W 0603"),
}

# --- ceramic capacitors, keyed (value, case) --------------------------------
# Voltage ratings follow the rail each part sits on (power_values.md):
# VBUS = 15 V -> 50 V parts; +12 V/PVDD -> 25-50 V; VBAT/5 V/3V3 -> 16-25 V.
_C = {
    ("100nF", "0603"): ("CL10B104KB8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 0.1UF 50V X7R 0603"),
    ("1uF",   "0603"): ("CL10B105KA8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 1UF 25V X7R 0603"),
    ("220nF", "0603"): ("CL10B224KA8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 0.22UF 25V X7R 0603"),
    ("47nF",  "0603"): ("CL10B473KB8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 0.047UF 50V X7R 0603"),
    ("6.8pF", "0603"): ("CL10C6R8BB8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 6.8PF 50V C0G/NP0 0603"),
    ("18pF",  "0603"): ("CL10C180JB8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 18PF 50V C0G/NP0 0603"),
    ("22pF",  "0603"): ("CL10C220JB8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 22PF 50V C0G/NP0 0603"),
    ("100pF", "0603"): ("CL10C101JB8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 100PF 50V C0G/NP0 0603"),
    ("220pF", "0603"): ("CL10C221JB8NNNC", "Samsung Electro-Mechanics",
                        "CAP CER 220PF 50V C0G/NP0 0603"),
    ("10uF",  "0805"): ("CL21A106KAYNNNE", "Samsung Electro-Mechanics",
                        "CAP CER 10UF 25V X5R 0805"),
    ("22uF",  "0805"): ("CL21A226MOQNNNE", "Samsung Electro-Mechanics",
                        "CAP CER 22UF 16V X5R 0805"),
    ("0.68uF", "0805"): ("C0805C684K5RACTU", "KEMET",
                         "CAP CER 0.68UF 50V X7R 0805"),
    ("22uF",  "1210"): ("GRM32ER71E226KE15L", "Murata Electronics",
                        "CAP CER 22UF 25V X7R 1210"),
}

_CASE = {  # KiCad footprint name -> BOM package string
    "C_0603_1608Metric": "0603",
    "C_0805_2012Metric": "0805",
    "C_1210_3225Metric": "1210",
    "R_0603_1608Metric": "0603",
    "R_1206_3216Metric": "1206",
    "R_2010_5025Metric": "2010",
}

# --- everything else, by reference ------------------------------------------
_REF = {
    # (mpn, manufacturer, package, description, notes)
    "BT1": ("1043", "Keystone Electronics", "TH holder, 18650",
            "BATTERY HOLDER 18650 PC PIN", ""),

    # R1 feeds the CH224K's VDD shunt regulator straight off VBUS.  On the
    # 15 V contract the shunt holds VDD at 3.3 V, so R1 carries
    # (15-3.3)/1k = 11.7 mA and burns (15-3.3)^2/1k = 137 mW -- 1.4x a 0603's
    # 100 mW and 1.1x an 0805's 125 mW.  1206 (250 mW) is the first standard
    # land with real margin, and R1 has 4.6 mm of clear space around it.
    # Same Yageo RC series as every other resistor here.  [REVIEW.md #13]
    "R1": ("RC1206FR-071KL", "YAGEO", "1206",
           "RES 1K OHM 1% 1/4W 1206",
           "1206 for the power rating, NOT 0603 -- see REVIEW.md #13"),

    # electrolytics (both on the same D6.3 SMD land)
    "C107": ("EEE-FK1C101P", "Panasonic", "SMD radial can D6.3xL6.1mm",
             "CAP ALUM 100UF 16V 20% SMD", SUB_OK),
    "C129": ("EEE-FK1C101P", "Panasonic", "SMD radial can D6.3xL6.1mm",
             "CAP ALUM 100UF 16V 20% SMD", SUB_OK),
    "C237": ("EEE-FK1C101P", "Panasonic", "SMD radial can D6.3xL6.1mm",
             "CAP ALUM 100UF 16V 20% SMD", SUB_OK),
    "C172": ("EEH-ZA1E101P", "Panasonic", "SMD radial can D6.3xL7.7mm",
             "CAP ALUM POLY HYBRID 100UF 25V SMD",
             "PVDD bulk. **Hybrid polymer, NOT a plain electrolytic** -- the "
             "ripple rating is the spec that matters here, not the value. A "
             "class-D BTL draws i_supply = P/PVDD x (1-cos 2wt), so the bulk "
             "sees (P/PVDD)/sqrt(2) rms at twice the audio frequency: 0.49 A "
             "at the 8 W firmware limit, 0.68 A at clip. The previous part "
             "(Rubycon 25TZV100M6.3X8, 300 mA @100 kHz) was under-rated ~1.6x; "
             "the ZA hybrid is ~1.3 A with ~40 mOhm ESR, and 7.7 mm actually "
             "fits the CP_Elec_6.3x7.7 land better than the old 8.0 mm can. "
             "SUBSTITUTION RULE: any D6.3 part with >=600 mA @100 kHz ripple "
             "and >=25 V. Value may drop to 68 uF if that buys the rating. "
             "VERIFY DigiKey stock before ordering (REVIEW.md #19)"),

    # inductors
    "L1": ("XAL4040-103MEC", "Coilcraft", "SMD 4.0x4.0x4.0mm",
           "FIXED IND 10UH 3.0A ISAT 84MOHM", ""),
    "L5": ("XAL4040-103MEC", "Coilcraft", "SMD 4.0x4.0x4.0mm",
           "FIXED IND 10UH 3.0A ISAT 84MOHM", ""),
    "L6": ("XAL4040-103MEC", "Coilcraft", "SMD 4.0x4.0x4.0mm",
           "FIXED IND 10UH 3.0A ISAT 84MOHM", ""),
    "L2": ("XAL4020-102MEC", "Coilcraft", "SMD 4.0x4.0x2.1mm",
           "FIXED IND 1UH 8.7A ISAT", ""),
    "L3": ("XAL4020-222MEC", "Coilcraft", "SMD 4.0x4.0x2.1mm",
           "FIXED IND 2.2UH 5.6A ISAT", ""),
    "L4": ("XGL5050-472MEC", "Coilcraft", "SMD 5.3x5.5x5.0mm",
           "FIXED IND 4.7UH 9.7A ISAT 16MOHM",
           "On the XAL5050 land - verified 2026-07-27: identical to the "
           "XGL5050 recommended land (1.18x4.7mm pads, 3.31mm pitch)"),

    # diodes / LEDs
    "D1":  ("SMAJ22CA", "Littelfuse", "DO-214AC (SMA)",
            "TVS DIODE 22V BIDIR 400W SMA", ""),
    "D12": ("SMAJ5.0CA", "Littelfuse", "DO-214AC (SMA)",
            "TVS DIODE 5V BIDIR 400W SMA", ""),
    "D10": ("BAT46W-E3-08", "Vishay", "SOD-123",
            "DIODE SCHOTTKY 100V 250MA SOD-123", ""),
    "D13": ("BAT42W-E3-08", "Vishay", "SOD-123",
            "DIODE SCHOTTKY 30V 200MA SOD-123", ""),
    # D14 = the +3V3 half of the VBAT_SENSE clamp pair (D13 clamps negative,
    # D14 positive). Same part and land as D13, so no new BOM line -- only qty.
    "D14": ("BAT42W-E3-08", "Vishay", "SOD-123",
            "DIODE SCHOTTKY 30V 200MA SOD-123", ""),
    "D11": ("B340A-13-F", "Diodes Incorporated", "DO-214AC (SMA)",
            "DIODE SCHOTTKY 40V 3A SMA", ""),
    "D20": ("B340A-13-F", "Diodes Incorporated", "DO-214AC (SMA)",
            "DIODE SCHOTTKY 40V 3A SMA", ""),
    "D30": ("B340A-13-F", "Diodes Incorporated", "DO-214AC (SMA)",
            "DIODE SCHOTTKY 40V 3A SMA", ""),
    "D40": ("SK6812RGBW", "Opsco Optoelectronics", "PLCC-4 5.0x5.0mm",
            "LED RGBW ADDRESSABLE 5050 SK6812",
            "= Adafruit 2758 10-pack (DigiKey 6134706); any SK6812 RGBW 5050"),
    "D41": ("SK6812RGBW", "Opsco Optoelectronics", "PLCC-4 5.0x5.0mm",
            "LED RGBW ADDRESSABLE 5050 SK6812",
            "= Adafruit 2758 10-pack (DigiKey 6134706); any SK6812 RGBW 5050"),

    # protection / thermal
    "F1": ("SDF-DF077S", "Cantherm", "Radial D4.0mm, axial AWG18 leads",
           "THERMAL CUTOFF 77C 10A NON-RESET",
           "Solder >=3 mm from the body and heatsink the lead"),
    "RT1": ("NCP18XH103F03RB", "Murata Electronics", "0603",
            "NTC THERMISTOR 10K OHM 1% B3380 0603",
            "NTC, not a resistor - do not merge with the 10k line"),

    # connectors
    "J1":  ("USB4160-03-0230-C", "GCT", "SMT vertical, 24-pin + 4 stakes",
            "CONN RCPT USB TYPE-C 24P VERTICAL SMT",
            "USB 2.0 subset wired (VBUS/GND/CC/D+/D-); SS pads unconnected"),
    "J2":  ("PRPC004SAAN-RC", "Sullins Connector Solutions",
            "1x04 2.54mm TH header",
            "CONN HEADER VERT 4POS 2.54MM", ""),
    "J3":  ("B2B-PH-K-S(LF)(SN)", "JST Sales America",
            "JST PH 2.00mm 2-pos TH vertical",
            "CONN HEADER VERT 2POS 2.00MM", ""),
    "J6":  ("DM3AT-SF-PEJM5", "Hirose Electric", "SMT push-push, 8-pos",
            "CONN MICRO SD CARD PUSH-PULL SMD", ""),
    # Tail length, checked 2026-08-07 (kicad-sensor/REVIEW.md #2): the ZH
    # datasheet specifies the 2.7 mm soldering length for 0.6-1.2 mm boards
    # and 3.4 mm for 1.6 mm, which is this board.  The -3.4 variants are
    # Active but NOT stocked at DigiKey (455-B6B-ZR-3.4-ND / 455-B2B-ZR-3.4-ND:
    # made to order, MOQ 2,000, 16-week lead) while the 2.7 mm parts are
    # $0.24/$0.20 off the shelf.  Deviation accepted: 2.7 mm still leaves
    # 1.1 mm protruding through a 1.6 mm board, which is a normal fully-wetted
    # TH joint, and the wafer seats on the board either way.  Same call as the
    # sensor board's J1 -- these are the two ends of one harness.
    "J7":  ("B6B-ZR(LF)(SN)", "JST Sales America",
            "JST ZH 1.50mm 6-pos TH vertical, 2.7mm tail",
            "CONN HEADER VERT 6POS 1.50MM",
            "2.7 mm tail on a 1.6 mm board — JST specs 3.4 mm (B6B-ZR-3.4) "
            "for this thickness, but that variant is made-to-order only "
            "(MOQ 2,000, 16 wk). Accepted deviation, see PCB_NOTES.md. Same "
            "part as the sensor board's J1: one harness, two identical ends"),
    "J9":  ("B3B-PH-K-S(LF)(SN)", "JST Sales America",
            "JST PH 2.00mm 3-pos TH vertical",
            "CONN HEADER VERT 3POS 2.00MM", ""),
    "J10": ("B6B-ZR(LF)(SN)", "JST Sales America",
            "JST ZH 1.50mm 6-pos TH vertical, 2.7mm tail",
            "CONN HEADER VERT 6POS 1.50MM",
            "Same part and same accepted 2.7 mm-tail deviation as J7 (above). "
            "MECHANICALLY IDENTICAL TO J7 AND 13.5 mm FROM IT — a ZHR-6 "
            "swapped between the two puts +5 V on the sensor board's +3V3 "
            "and destroys all three sensors. Label both harnesses"),
    "J11": ("B2B-ZR(LF)(SN)", "JST Sales America",
            "JST ZH 1.50mm 2-pos TH vertical, 2.7mm tail",
            "CONN HEADER VERT 2POS 1.50MM",
            "Same accepted 2.7 mm-tail deviation as J7 (above); B2B-ZR-3.4 "
            "is not stocked either. 2 positions, so it cannot mis-mate with "
            "J7/J10"),
    "J12": ("B3B-PH-K-S(LF)(SN)", "JST Sales America",
            "JST PH 2.00mm 3-pos TH vertical",
            "CONN HEADER VERT 3POS 2.00MM", ""),

    # electromechanical
    "M1":  ("X40.879", "Juken Swiss Technology",
            "TH, 8 pins, dual coaxial shaft",
            "STEPPER MOTOR ANALOG CLOCK MOVEMENT DUAL-SHAFT",
            # The "DO NOT MOUNT" half of this note used to live ONLY in
            # clock.kicad_pcb as a hand edit, so every stamp_bom.py run wiped
            # it. Keep it here -- parts_db is what stamp_bom writes from.
            "DO NOT MOUNT. Customer will mount part after PCB reception. "
            "Custom footprint; shafts pass through the board. Long lead time "
            "(MiniTools / DigiKey X40-879) - customer-supplied option"),
    "SW1": ("RS-282G05A3-SM RT", "C&K", "SMD tactile 4.2x3.2mm",
            "SWITCH TACTILE SPST-NO 50MA 12V SMD", ""),
    "SW2": ("RS-282G05A3-SM RT", "C&K", "SMD tactile 4.2x3.2mm",
            "SWITCH TACTILE SPST-NO 50MA 12V SMD", ""),

    # discretes
    "Q1": ("2N7002LT1G", "onsemi", "SOT-23", "MOSFET N-CH 60V 115MA SOT-23", ""),
    "Q3": ("2N7002LT1G", "onsemi", "SOT-23", "MOSFET N-CH 60V 115MA SOT-23", ""),
    "Q8": ("2N7002LT1G", "onsemi", "SOT-23", "MOSFET N-CH 60V 115MA SOT-23", ""),
    "Q2": ("AO3401A", "Alpha & Omega Semiconductor", "SOT-23",
           "MOSFET P-CH 30V 4A SOT-23", ""),
    "Q4": ("AO3401A", "Alpha & Omega Semiconductor", "SOT-23",
           "MOSFET P-CH 30V 4A SOT-23", ""),
    "Q9": ("AO3401A", "Alpha & Omega Semiconductor", "SOT-23",
           "MOSFET P-CH 30V 4A SOT-23", ""),
    "Q6": ("AO3400A", "Alpha & Omega Semiconductor", "SOT-23",
           "MOSFET N-CH 30V 5.7A SOT-23", ""),
    "Q7": ("AO3400A", "Alpha & Omega Semiconductor", "SOT-23",
           "MOSFET N-CH 30V 5.7A SOT-23", ""),
    "R18": ("WSL2010R1000FEA18", "Vishay Dale", "2010",
            "RES 0.1 OHM 1% 1W 2010 CURRENT SENSE",
            "LT3652 sense; dissipates 0.1 W at I_CHG 1 A. The ...FEA18 suffix "
            "is Vishay's 1 W variant of the same 2010 land (the plain "
            "...FEA is 1/2 W)"),

    # ICs
    "U1":  ("CH224K", "WCH (Nanjing Qinheng)", "ESSOP-10 (SSOP-10, EP)",
            "IC USB PD SINK CONTROLLER ESSOP-10",
            "Not stocked at DigiKey - LCSC C970725"),
    "U2":  ("LT3652EMSE#PBF", "Analog Devices", "12-MSOP-EP",
            "IC BATT CHARGER LI-ION 2A 12MSOP-EP", ""),
    "U3":  ("HY2111-GB", "HYCON Technology", "SOT-23-6",
            "IC BATT PROTECTION 1S 4.28V SOT-23-6",
            "Not stocked at DigiKey - LCSC C82747"),
    "U4":  ("AOSD32334C", "Alpha & Omega Semiconductor", "8-SOIC",
            "MOSFET 2 N-CH 30V 8A SO-8", ""),
    "U5":  ("TPS61023DRLR", "Texas Instruments", "SOT-563",
            "IC REG BOOST ADJ 3.7A SOT-563", ""),
    "U6":  ("TLV62569DBVR", "Texas Instruments", "SOT-23-5",
            "IC REG BUCK ADJ 2A SOT-23-5",
            "DBV = SOT-23-5 (README §16b says SOT-23-6 - README is wrong)"),
    "U7":  ("TPS55340PWPR", "Texas Instruments", "14-HTSSOP-EP (PWP)",
            "IC REG BOOST ADJ 5A 14HTSSOP-EP", ""),
    "U8":  ("ESP32-S3-WROOM-1-N8R8", "Espressif Systems",
            "SMD module 18x25.5mm, castellated",
            "MODULE ESP32-S3 WIFI/BLE 8MB FLASH 8MB PSRAM", ""),
    "U9":  ("TAS5760MDAPR", "Texas Instruments", "32-HTSSOP-EP (DAP)",
            "IC AUDIO AMP CLASS-D I2S 32HTSSOP-EP", ""),
    "U10": ("LTC4412ES6#TRPBF", "Analog Devices", "TSOT-23-6",
            "IC IDEAL DIODE POWERPATH CTRLR SOT-23-6", ""),
    "U11": ("TB6612FNG,C,8,EL", "Toshiba", "24-SSOP",
            "IC MOTOR DRIVER DUAL H-BRIDGE 24SSOP", ""),
    "U12": ("TB6612FNG,C,8,EL", "Toshiba", "24-SSOP",
            "IC MOTOR DRIVER DUAL H-BRIDGE 24SSOP", ""),
    "U13": ("MCP23017-E/SS", "Microchip Technology", "28-SSOP",
            "IC I/O EXPANDER I2C 16BIT 28SSOP",
            "SSOP-28 land (README §16b quotes the -E/SO SOIC-28W order code)"),
    "U14": ("QRE1113GR", "onsemi", "SMD gull-wing 4-lead",
            "SENSOR OPTICAL REFLECTIVE ANALOG OUT", ""),
    "U15": ("SN74AHCT1G125DBVR", "Texas Instruments", "SOT-23-5",
            "IC BUFFER 1CH 3-STATE SOT-23-5", ""),
    "U16": ("USBLC6-2SC6", "STMicroelectronics", "SOT-23-6",
            "TVS DIODE ESD USB 2-CHANNEL SOT-23-6", ""),
    "Y1":  ("ABS07-32.768KHZ-T", "Abracon", "SMD 3.2x1.5mm 2-pin",
            "CRYSTAL 32.768KHZ 12.5PF SMD", ""),
}

SKIP_PREFIXES = ("H", "#")   # mounting holes, power symbols

# Value-field corrections applied in place by stamp_bom.py. The b_*.py blocks
# emit these same strings, so after a regeneration this dict is a no-op — it
# exists so the current hand-fixed sch/pcb don't have to be rebuilt.
VALUES = {
    "RT1": "10k NTC",   # was "10k": read as a plain resistor on the sheet, and
                        # it silently merged into the 10 kOhm BOM line
    "C172": "100uF",    # was "220uF": no >=16 V 220 uF part fits the D6.3 land
}

# Schematic Value-text placement (x, y, justify[, rotation]), applied to
# clock.kicad_sch only.
#
# EMPTY SINCE 2026-08-04, and it should stay that way.  This predates
# harvest.py capturing GUI-placed Reference/Value text, and the two fought:
# stamp_bom.py runs AFTER build.py, so an entry here silently overwrote the
# position the user had placed by hand (RT1's "10k NTC" was the one case).
# cosmetics.py is now the single source of truth for where cosmetic text
# sits -- move it in eeschema and re-run harvest.py instead of adding an
# entry here.
VALUE_POS = {}


def part_for(ref, value, footprint):
    """-> dict(MPN, Manufacturer, Package, Description, Notes) or None."""
    if ref in _REF:
        mpn, mfr, pkg, desc, notes = _REF[ref]
        return {"MPN": mpn, "Manufacturer": mfr, "Package": pkg,
                "Description": desc, "Notes": notes}

    case = _CASE.get(footprint.split(":")[-1], "")

    if ref.startswith("R") and value in _R:
        mpn, desc = _R[value]
        return {"MPN": mpn, "Manufacturer": "YAGEO", "Package": case or "0603",
                "Description": desc, "Notes": SUB_OK}

    if ref.startswith("C") and (value, case) in _C:
        mpn, mfr, desc = _C[(value, case)]
        return {"MPN": mpn, "Manufacturer": mfr, "Package": case,
                "Description": desc, "Notes": SUB_OK}

    return None
