"""Sheet: Audio — TAS5760M DAP-32 Class-D amp (PBTL mono into DMA58-4) +
LTC4412 PVDD rail-mux (12 V plugged / 5 V battery) + output LC filter.
The amp is a dense 32-pin part: its pin nets use labels (standard); discrete
support (mux, LC filter, bulk) is wired.  Values: power_values.md §8/§10.
"""
from collections import defaultdict
from sch import FP


def build(sch):
    sch.text("AUDIO  —  TAS5760M (DAP-32) PBTL mono + LTC4412 PVDD mux (12V<->5V) + LC filter",
             30, 18, size=2.0)

    U9 = sch.comp("U9", "clock:TAS5760M", 180, 150, value="TAS5760MDAPR",
                  footprint="Package_SO:HTSSOP-32-1EP_6.1x11mm_P0.65mm_EP5.2x11mm_Mask4.11x4.36mm")
    nm = defaultdict(list)
    for n, d in sch.cache.pins("clock:TAS5760M").items():
        nm[d["name"]].append(n)

    def pin(name):
        return nm[name][0]

    # left digital pins -> cross-sheet buses (labels)
    sch.node(U9, pin("MCLK"), "I2S_MCLK"); sch.node(U9, pin("SCLK"), "I2S_BCLK")
    sch.node(U9, pin("SDIN"), "I2S_DOUT"); sch.node(U9, pin("LRCK"), "I2S_LRCLK")
    sch.node(U9, pin("FREQ/SDA"), "I2C_SDA"); sch.node(U9, pin("PBTL/SCL"), "I2C_SCL")
    sch.node(U9, pin("SPK_SD"), "SPK_SD"); sch.node(U9, pin("SPK_FAULT"), "SPK_FAULT")
    sch.node(U9, pin("SPK_GAIN0"), "AMP_GAIN"); sch.node(U9, pin("SPK_GAIN1"), "AMP_GAIN")
    sch.node(U9, pin("SPK_SLEEP/ADR"), "AMP_ADR")

    # control strap resistors (spaced column, left) -> connect by AMP_* labels
    Rf = sch.R("R62", 110, 120, "10k"); sch.node(Rf, "1", "+3V3"); sch.node(Rf, "2", "SPK_FAULT")
    Rg = sch.R("R60", 110, 138, "10k"); sch.node(Rg, "1", "+3V3"); sch.node(Rg, "2", "AMP_GAIN")
    Radr = sch.R("R61", 110, 156, "0R"); sch.node(Radr, "1", "AMP_ADR"); sch.node(Radr, "2", "GND")

    # analog/reg bypass caps (spaced column, far left) -> AMP_* labels
    sch.node(U9, pin("DVDD"), "+3V3"); sch.node(U9, pin("AVDD"), "+3V3")
    sch.node(U9, pin("GVDD_REG"), "AMP_GVDD"); sch.node(U9, pin("SFT_CLIP"), "AMP_GVDD")
    sch.node(U9, pin("ANA_REG"), "AMP_ANAREG"); sch.node(U9, pin("ANA_REF"), "AMP_ANAREF")
    sch.node(U9, pin("VCOM"), "AMP_VCOM")
    for i, v in enumerate(["100nF", "10uF"]):     # DVDD/AVDD decoupling
        c = sch.C(f"C16{i+1}", 60 + i * 12, 118, v, fp="C0805" if "uF" in v else "C0603")
        sch.node(c, "1", "+3V3"); sch.node(c, "2", "GND")
    Cgv = sch.C("C160", 60, 138, "1uF"); sch.node(Cgv, "1", "AMP_GVDD"); sch.node(Cgv, "2", "GND")
    Car = sch.C("C164", 78, 138, "100nF"); sch.node(Car, "1", "AMP_ANAREG"); sch.node(Car, "2", "GND")
    Car2 = sch.C("C165", 60, 156, "1uF"); sch.node(Car2, "1", "AMP_ANAREG"); sch.node(Car2, "2", "GND")
    Cref = sch.C("C166", 78, 156, "100nF"); sch.node(Cref, "1", "AMP_ANAREF"); sch.node(Cref, "2", "GND")
    Cvcom = sch.C("C167", 96, 156, "1uF"); sch.node(Cvcom, "1", "AMP_VCOM"); sch.node(Cvcom, "2", "GND")

    # grounds (bottom of amp)
    for gnm in ("DGND", "PGND", "GGND", "PAD"):
        for p in nm[gnm]:
            sch.node(U9, p, "GND")

    # right: outputs / bootstrap / PVDD (labels off the dense pins)
    sch.node(U9, pin("SPK_OUTA+"), "AMP_OUTP"); sch.node(U9, pin("SPK_OUTB+"), "AMP_OUTP")
    sch.node(U9, pin("SPK_OUTA-"), "AMP_OUTN"); sch.node(U9, pin("SPK_OUTB-"), "AMP_OUTN")
    for p in nm["PVDD"]:
        sch.node(U9, p, "PVDD")
    boots = [("BSTRPA+", "AMP_OUTP"), ("BSTRPA-", "AMP_OUTN"),
             ("BSTRPB+", "AMP_OUTP"), ("BSTRPB-", "AMP_OUTN")]
    for i, (b, onet) in enumerate(boots):
        sch.node(U9, pin(b), f"AMP_{b}")
        cb = sch.C(f"C18{i}", 236 + (i % 2) * 14, 118 + (i // 2) * 16, "33nF")
        sch.node(cb, "1", f"AMP_{b}"); sch.node(cb, "2", onet)

    # PVDD bulk (spaced)
    for i, (v, fp) in enumerate([("100nF", "C0603"), ("1uF", "C0603"), ("220uF", "CP_bulk")]):
        c = (sch.CP(f"C17{i}", 236 + i * 14, 200, v) if fp == "CP_bulk"
             else sch.C(f"C17{i}", 236 + i * 14, 200, v, fp=fp))
        sch.node(c, "1", "PVDD"); sch.node(c, "2", "GND")

    # output LC filter + speaker (wired cluster, right)
    La = sch.L("L5", 260, 150, "10uH", fp="L4030"); La.rot = 90
    sch.node(La, "1", "AMP_OUTP"); sch.node(La, "2", "SPK_P")
    Lb = sch.L("L6", 260, 165, "10uH", fp="L4030"); Lb.rot = 90
    sch.node(Lb, "1", "AMP_OUTN"); sch.node(Lb, "2", "SPK_N")
    Cfp = sch.C("C184", 278, 150, "0.68uF", fp="C0805"); sch.node(Cfp, "1", "SPK_P"); sch.node(Cfp, "2", "GND")
    Cfn = sch.C("C185", 292, 165, "0.68uF", fp="C0805"); sch.node(Cfn, "1", "SPK_N"); sch.node(Cfn, "2", "GND")
    JS = sch.comp("J3", "Connector_Generic:Conn_01x02", 300, 156, value="DMA58-4 (4 ohm)",
                  footprint="Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical")
    sch.node(JS, "1", "SPK_P"); sch.node(JS, "2", "SPK_N")

    # LTC4412 PVDD mux (wired cluster, bottom-right)
    U10 = sch.comp("U10", "Power_Management:LTC4412xS6", 210, 205, value="LTC4412ES6", footprint=FP["SOT23-6"])
    sch.node(U10, "1", "+5V"); sch.node(U10, "6", "PVDD")
    sch.node(U10, "5", "MUX_GATE"); sch.node(U10, "3", "GND"); sch.node(U10, "2", "GND")
    sch.nc(U10, "4")
    Cmux = sch.C("C186", 192, 213, "100nF"); sch.node(Cmux, "1", "+5V"); sch.node(Cmux, "2", "GND")
    Q5 = sch.comp("Q4", "Transistor_FET:AO3401A", 234, 205, value="AO3401A", footprint=FP["SOT23-3"])
    sch.node(Q5, "2", "+5V"); sch.node(Q5, "3", "PVDD"); sch.node(Q5, "1", "MUX_GATE")
    Dm = sch.D_schottky("D30", 252, 205, "SS34", fp="SMA"); Dm.rot = 90
    sch.node(Dm, "2", "+12V"); sch.node(Dm, "1", "PVDD")

    sch.text("Bootstrap caps 33nF: VERIFY vs TAS5760M datasheet.  PVDD = 12V(plugged)/5V(batt).",
             30, 232, size=1.3)
