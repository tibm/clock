"""Sheet: Rails — TPS61023 (+5V boost, always-on), TLV62569 (+3V3 buck from 5V),
TPS55340 (+12V boost from VBAT, plugged-only via BOOST12_EN).
Parts clustered at their pins so on-page nets are short wires. Values: §3/§4/§5.
"""
from sch import FP


def build(sch):
    sch.text("RAILS  —  5 V boost (always) / 3V3 buck / 12 V boost (plugged-only)",
             30, 20, size=2.0)

    # ============ TPS61023 -> +5V (top-left) ============
    U5 = sch.comp("U5", "clock:TPS61023", 90, 70, value="TPS61023",
                  footprint="Package_TO_SOT_SMD:SOT-563")
    sch.node(U5, "3", "VBAT"); sch.node(U5, "2", "VBAT")   # VIN, EN (left)
    sch.node(U5, "4", "GND")                                # GND (bottom)
    Cin5 = sch.C("C120", 72, 62, "10uF", fp="C0805"); sch.node(Cin5, "1", "VBAT"); sch.node(Cin5, "2", "GND")
    L2 = sch.L("L2", 90, 52, "1uH", fp="L4020"); L2.rot = 90   # VBAT -> SW (above)
    sch.node(L2, "1", "VBAT"); sch.node(L2, "2", "RAIL5_SW"); sch.node(U5, "5", "RAIL5_SW")
    sch.node(U5, "6", "+5V")                                # VOUT (right)
    Co5a = sch.C("C121", 108, 78, "22uF", fp="C0805"); sch.node(Co5a, "1", "+5V"); sch.node(Co5a, "2", "GND")
    Co5b = sch.C("C122", 120, 78, "22uF", fp="C0805"); sch.node(Co5b, "1", "+5V"); sch.node(Co5b, "2", "GND")
    # FB divider (left of FB): +5V -> R40 -> FB -> R41 -> GND, Cff5 || R40
    Rf5a = sch.R("R40", 66, 74, "732k"); sch.node(Rf5a, "1", "+5V"); sch.node(Rf5a, "2", "RAIL5_FB")
    Rf5b = sch.R("R41", 66, 88, "100k"); sch.node(Rf5b, "1", "RAIL5_FB"); sch.node(Rf5b, "2", "GND")
    Cff5 = sch.C("C123", 78, 74, "220pF"); sch.node(Cff5, "1", "+5V"); sch.node(Cff5, "2", "RAIL5_FB")
    sch.node(U5, "1", "RAIL5_FB")

    # ============ TLV62569 -> +3V3 (top-right) ============
    U6 = sch.comp("U6", "Regulator_Switching:TLV62569DBV", 240, 70,
                  value="TLV62569DBVR", footprint=FP["SOT23-5"])
    sch.node(U6, "4", "+5V"); sch.node(U6, "1", "+5V")     # VIN, EN (left)
    sch.node(U6, "2", "GND")                                # GND (bottom)
    Cin33 = sch.C("C124", 222, 62, "10uF", fp="C0805"); sch.node(Cin33, "1", "+5V"); sch.node(Cin33, "2", "GND")
    L3 = sch.L("L3", 262, 60, "2.2uH", fp="L4020"); L3.rot = 90   # SW -> +3V3
    sch.node(U6, "3", "RAIL33_SW"); sch.node(L3, "1", "RAIL33_SW"); sch.node(L3, "2", "+3V3")
    Co33 = sch.C("C125", 278, 78, "22uF", fp="C0805"); sch.node(Co33, "1", "+3V3"); sch.node(Co33, "2", "GND")
    Rf3a = sch.R("R42", 262, 78, "453k"); sch.node(Rf3a, "1", "+3V3"); sch.node(Rf3a, "2", "RAIL33_FB")
    Rf3b = sch.R("R43", 262, 92, "100k"); sch.node(Rf3b, "1", "RAIL33_FB"); sch.node(Rf3b, "2", "GND")
    Cff33 = sch.C("C126", 250, 84, "6.8pF"); sch.node(Cff33, "1", "+3V3"); sch.node(Cff33, "2", "RAIL33_FB")
    sch.node(U6, "5", "RAIL33_FB")

    # ============ TPS55340 -> +12V (bottom, plugged-only) ============
    U7 = sch.comp("U7", "clock:TPS55340", 150, 160, value="TPS55340PWPR",
                  footprint="Package_SO:Texas_HTSSOP-14-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask3.155x3.255mm")
    sch.node(U7, "3", "VBAT")                               # VIN (left)
    Cin12a = sch.C("C127", 108, 150, "10uF", fp="C0805"); sch.node(Cin12a, "1", "VBAT"); sch.node(Cin12a, "2", "GND")
    Cin12b = sch.C("C128", 118, 150, "100nF"); sch.node(Cin12b, "1", "VBAT"); sch.node(Cin12b, "2", "GND")
    Cin12c = sch.CP("C129", 96, 150, "100uF"); sch.node(Cin12c, "1", "VBAT"); sch.node(Cin12c, "2", "GND")
    sch.node(U7, "4", "BOOST12_EN")                         # EN (left)
    Ren = sch.R("R44", 122, 172, "100k"); sch.node(Ren, "1", "BOOST12_EN"); sch.node(Ren, "2", "GND")
    L4 = sch.L("L4", 150, 138, "4.7uH", fp="L4030"); L4.rot = 90   # VBAT -> SW (top)
    sch.node(U7, "1", "RAIL12_SW"); sch.node(U7, "2", "RAIL12_SW"); sch.node(L4, "1", "VBAT"); sch.node(L4, "2", "RAIL12_SW")
    D20 = sch.D_schottky("D20", 175, 138, "B340A", fp="SMA"); D20.rot = 90  # SW -> +12V (K top)
    sch.node(D20, "2", "RAIL12_SW"); sch.node(D20, "1", "+12V")
    Co12a = sch.C("C130", 185, 160, "22uF", fp="C1210"); sch.node(Co12a, "1", "+12V"); sch.node(Co12a, "2", "GND")
    Co12b = sch.C("C131", 197, 160, "22uF", fp="C1210"); sch.node(Co12b, "1", "+12V"); sch.node(Co12b, "2", "GND")
    Co12c = sch.C("C132", 209, 160, "22uF", fp="C1210"); sch.node(Co12c, "1", "+12V"); sch.node(Co12c, "2", "GND")
    # FB divider (left side pin 9)
    Rf12a = sch.R("R45", 120, 158, "86.6k"); sch.node(Rf12a, "1", "+12V"); sch.node(Rf12a, "2", "RAIL12_FB")
    Rf12b = sch.R("R46", 120, 172, "10k"); sch.node(Rf12b, "1", "RAIL12_FB"); sch.node(Rf12b, "2", "GND")
    sch.node(U7, "9", "RAIL12_FB")
    # FREQ, SS, COMP (left)
    Rfreq = sch.R("R47", 108, 186, "95.3k"); sch.node(Rfreq, "1", "RAIL12_FREQ"); sch.node(Rfreq, "2", "GND"); sch.node(U7, "10", "RAIL12_FREQ")
    Css = sch.C("C133", 120, 186, "47nF"); sch.node(Css, "1", "RAIL12_SS"); sch.node(Css, "2", "GND"); sch.node(U7, "5", "RAIL12_SS")
    Rc = sch.R("R48", 132, 186, "2.55k"); sch.node(Rc, "1", "RAIL12_COMP"); sch.node(Rc, "2", "RAIL12_CC"); sch.node(U7, "7", "RAIL12_COMP")
    Cc = sch.C("C134", 132, 200, "100nF"); sch.node(Cc, "1", "RAIL12_CC"); sch.node(Cc, "2", "GND")
    Cc2 = sch.C("C135", 144, 200, "100pF"); sch.node(Cc2, "1", "RAIL12_COMP"); sch.node(Cc2, "2", "GND")
    # grounds / NC (right + bottom)
    sch.node(U7, "6", "GND"); sch.node(U7, "8", "GND")
    sch.node(U7, "12", "GND"); sch.node(U7, "13", "GND"); sch.node(U7, "14", "GND"); sch.node(U7, "15", "GND")
    sch.nc(U7, "11")

    sch.text("12 V boost PLUGGED-ONLY: firmware asserts BOOST12_EN only when PD_PG live.",
             30, 220, size=1.3)
