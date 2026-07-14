"""Sheet: Rails — TPS61023 (+5V boost, always-on), TLV62569 (+3V3 buck from 5V),
TPS55340 (+12V boost from VBAT, plugged-only via BOOST12_EN).
Values: power_values.md §3/§4/§5.
"""
from sch import FP


def build(sch):
    sch.text("RAILS — 5 V boost (always) / 3V3 buck / 12 V boost (plugged-only)",
             30, 25, size=2.0)

    # ================= TPS61023 -> +5V (from VBAT) =================
    U5 = sch.comp("U5", "clock:TPS61023", 90, 90, value="TPS61023",
                  footprint="Package_TO_SOT_SMD:SOT-563")
    sch.net(U5, "3", "VBAT")             # VIN
    sch.net(U5, "2", "VBAT")             # EN tie-high (always-on)
    Cin5 = sch.C("C120", 60, 110, "10uF", fp="C0805"); sch.net(Cin5, "1", "VBAT"); sch.power(Cin5, "2", "GND")
    L2 = sch.L("L2", 70, 70, "1uH", fp="L4020"); sch.net(L2, "1", "VBAT"); sch.net(L2, "2", "RAIL5_SW")
    sch.net(U5, "5", "RAIL5_SW")         # SW
    sch.net(U5, "6", "+5V")              # VOUT
    Co5a = sch.C("C121", 120, 110, "22uF", fp="C0805"); sch.net(Co5a, "1", "+5V"); sch.power(Co5a, "2", "GND")
    Co5b = sch.C("C122", 132, 110, "22uF", fp="C0805"); sch.net(Co5b, "1", "+5V"); sch.power(Co5b, "2", "GND")
    # FB divider 732k/100k + 220pF ff
    sch.net(U5, "1", "RAIL5_FB")
    Rf5a = sch.R("R40", 120, 80, "732k"); sch.net(Rf5a, "1", "+5V"); sch.net(Rf5a, "2", "RAIL5_FB")
    Rf5b = sch.R("R41", 120, 95, "100k"); sch.net(Rf5b, "1", "RAIL5_FB"); sch.power(Rf5b, "2", "GND")
    Cff5 = sch.C("C123", 135, 80, "220pF"); sch.net(Cff5, "1", "+5V"); sch.net(Cff5, "2", "RAIL5_FB")
    sch.power(U5, "4", "GND")

    # ================= TLV62569 -> +3V3 (from +5V) =================
    U6 = sch.comp("U6", "Regulator_Switching:TLV62569DBV", 250, 90,
                  value="TLV62569DBVR", footprint=FP["SOT23-5"])
    sch.net(U6, "4", "+5V")              # VIN
    sch.net(U6, "1", "+5V")              # EN tie-high
    Cin33 = sch.C("C124", 220, 110, "10uF", fp="C0805"); sch.net(Cin33, "1", "+5V"); sch.power(Cin33, "2", "GND")
    sch.net(U6, "3", "RAIL33_SW")        # SW
    L3 = sch.L("L3", 285, 78, "2.2uH", fp="L4020"); sch.net(L3, "1", "RAIL33_SW"); sch.net(L3, "2", "+3V3")
    Co33 = sch.C("C125", 305, 110, "22uF", fp="C0805"); sch.net(Co33, "1", "+3V3"); sch.power(Co33, "2", "GND")
    # FB divider 453k/100k + 6.8pF ff
    sch.net(U6, "5", "RAIL33_FB")
    Rf3a = sch.R("R42", 300, 80, "453k"); sch.net(Rf3a, "1", "+3V3"); sch.net(Rf3a, "2", "RAIL33_FB")
    Rf3b = sch.R("R43", 300, 95, "100k"); sch.net(Rf3b, "1", "RAIL33_FB"); sch.power(Rf3b, "2", "GND")
    Cff33 = sch.C("C126", 315, 80, "6.8pF"); sch.net(Cff33, "1", "+3V3"); sch.net(Cff33, "2", "RAIL33_FB")
    sch.power(U6, "2", "GND")

    # ================= TPS55340 -> +12V (from VBAT, plugged-only) =================
    U7 = sch.comp("U7", "clock:TPS55340", 130, 190, value="TPS55340PWPR",
                  footprint="Package_SO:Texas_HTSSOP-14-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask3.155x3.255mm")
    sch.net(U7, "3", "VBAT")             # VIN
    Cin12a = sch.C("C127", 70, 210, "10uF", fp="C0805"); sch.net(Cin12a, "1", "VBAT"); sch.power(Cin12a, "2", "GND")
    Cin12b = sch.C("C128", 82, 210, "100nF"); sch.net(Cin12b, "1", "VBAT"); sch.power(Cin12b, "2", "GND")
    Cin12c = sch.CP("C129", 94, 210, "100uF"); sch.net(Cin12c, "1", "VBAT"); sch.power(Cin12c, "2", "GND")
    # EN <- BOOST12_EN + 100k pulldown
    sch.net(U7, "4", "BOOST12_EN")
    Ren = sch.R("R44", 95, 170, "100k"); sch.net(Ren, "1", "BOOST12_EN"); sch.power(Ren, "2", "GND")
    # power inductor VBAT -> SW(1,2), diode SW -> +12V
    L4 = sch.L("L4", 95, 150, "4.7uH", fp="L4030"); sch.net(L4, "1", "VBAT"); sch.net(L4, "2", "RAIL12_SW")
    sch.net(U7, "1", "RAIL12_SW"); sch.net(U7, "2", "RAIL12_SW")
    D20 = sch.D_schottky("D20", 175, 165, "B340A", fp="SMA"); sch.net(D20, "2", "RAIL12_SW"); sch.net(D20, "1", "+12V")  # A=SW,K=12V
    Co12a = sch.C("C130", 195, 210, "22uF", fp="C1210"); sch.net(Co12a, "1", "+12V"); sch.power(Co12a, "2", "GND")
    Co12b = sch.C("C131", 207, 210, "22uF", fp="C1210"); sch.net(Co12b, "1", "+12V"); sch.power(Co12b, "2", "GND")
    Co12c = sch.C("C132", 219, 210, "22uF", fp="C1210"); sch.net(Co12c, "1", "+12V"); sch.power(Co12c, "2", "GND")
    # FB divider 86.6k/10k
    sch.net(U7, "9", "RAIL12_FB")
    Rf12a = sch.R("R45", 195, 175, "86.6k"); sch.net(Rf12a, "1", "+12V"); sch.net(Rf12a, "2", "RAIL12_FB")
    Rf12b = sch.R("R46", 195, 190, "10k"); sch.net(Rf12b, "1", "RAIL12_FB"); sch.power(Rf12b, "2", "GND")
    # FREQ 95.3k -> GND ; SS 0.047uF -> GND
    sch.net(U7, "10", "RAIL12_FREQ")
    Rfreq = sch.R("R47", 165, 205, "95.3k"); sch.net(Rfreq, "1", "RAIL12_FREQ"); sch.power(Rfreq, "2", "GND")
    sch.net(U7, "5", "RAIL12_SS")
    Css = sch.C("C133", 150, 205, "47nF"); sch.net(Css, "1", "RAIL12_SS"); sch.power(Css, "2", "GND")
    # COMP network: Rc 2.55k + Cc 0.1uF (series) to GND, + Cc2 100pF to GND
    sch.net(U7, "7", "RAIL12_COMP")
    Rc = sch.R("R48", 100, 195, "2.55k"); sch.net(Rc, "1", "RAIL12_COMP"); sch.net(Rc, "2", "RAIL12_CC")
    Cc = sch.C("C134", 100, 210, "100nF"); sch.net(Cc, "1", "RAIL12_CC"); sch.power(Cc, "2", "GND")
    Cc2 = sch.C("C135", 115, 210, "100pF"); sch.net(Cc2, "1", "RAIL12_COMP"); sch.power(Cc2, "2", "GND")
    # SYNC -> GND ; AGND/PGND/PAD -> GND ; NC
    sch.power(U7, "6", "GND")
    sch.power(U7, "8", "GND")
    sch.power(U7, "12", "GND"); sch.power(U7, "13", "GND"); sch.power(U7, "14", "GND")
    sch.power(U7, "15", "GND")
    sch.nc(U7, "11")

    sch.text("12 V boost is PLUGGED-ONLY: firmware asserts BOOST12_EN only when PD_PG live.",
             30, 235, size=1.2)
