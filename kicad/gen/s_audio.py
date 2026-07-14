"""Sheet: Audio — TAS5760M Class-D amp (PBTL mono into DMA58-4) + LTC4412 PVDD
rail-mux (12 V plugged / 5 V battery) + output LC filter.  Values: power_values.md §8/§10.

DISCREPANCY FLAG: BOM says TAS5760MDAPR (DAP 32-pin); /datasheet has the DCA
48-pin (SLOS772). Symbol/footprint built to the datasheet in hand; connections
are by pin NAME and identical either way. Reconcile the package before fab.
"""
from collections import defaultdict
from sch import FP


def build(sch):
    sch.text("AUDIO — TAS5760M PBTL mono + LTC4412 PVDD mux (12V<->5V) + LC filter",
             30, 20, size=2.0)

    U9 = sch.comp("U9", "clock:TAS5760M", 200, 150, value="TAS5760M (DCA-48)",
                  footprint="Package_SO:TSSOP-48_6.1x12.5mm_P0.5mm")
    # name -> [pin numbers]
    nm = defaultdict(list)
    for n, d in sch.cache.pins("clock:TAS5760M").items():
        nm[d["name"]].append(n)

    def pin(name):  # single-pin by name
        return nm[name][0]

    # --- I2S ---
    sch.net(U9, pin("MCLK"), "I2S_MCLK")
    sch.net(U9, pin("SCLK"), "I2S_BCLK")
    sch.net(U9, pin("SDIN"), "I2S_DOUT")
    sch.net(U9, pin("LRCK"), "I2S_LRCLK")
    # --- I2C (shared) ---
    sch.net(U9, pin("FREQ/SDA"), "I2C_SDA")
    sch.net(U9, pin("PBTL/SCL"), "I2C_SCL")
    # --- control-mode straps: GAIN0+GAIN1 HIGH to DVDD via one 10k ---
    sch.net(U9, pin("SPK_GAIN0"), "AMP_GAIN")
    sch.net(U9, pin("SPK_GAIN1"), "AMP_GAIN")
    Rg = sch.R("R60", 130, 95, "10k"); sch.power(Rg, "1", "+3V3"); sch.net(Rg, "2", "AMP_GAIN")
    # --- I2C address 0x6C: SPK_SLEEP/ADR -> GND via 0R ---
    Radr = sch.R("R61", 130, 120, "0R"); sch.net(Radr, "1", "AMP_ADR"); sch.power(Radr, "2", "GND")
    sch.net(U9, pin("SPK_SLEEP/ADR"), "AMP_ADR")
    # --- SPK_SD (from expander), SPK_FAULT (10k PU) ---
    sch.net(U9, pin("SPK_SD"), "SPK_SD")
    sch.net(U9, pin("SPK_FAULT"), "SPK_FAULT")
    Rf = sch.R("R62", 130, 135, "10k"); sch.power(Rf, "1", "+3V3"); sch.net(Rf, "2", "SPK_FAULT")
    # --- SFT_CLIP tied to GVDD_REG (soft-clip off) ---
    sch.net(U9, pin("SFT_CLIP"), "AMP_GVDD")
    sch.net(U9, pin("GVDD_REG"), "AMP_GVDD")
    Cgv = sch.C("C160", 150, 210, "1uF"); sch.net(Cgv, "1", "AMP_GVDD"); sch.power(Cgv, "2", "GND")
    # --- DVDD / AVDD = 3V3 + decoupling ---
    sch.power(U9, pin("DVDD"), "+3V3")
    sch.power(U9, pin("AVDD"), "+3V3")
    for i, (nname, v) in enumerate([("DVDD", "100nF"), ("DVDD", "10uF"),
                                    ("AVDD", "100nF")]):
        c = sch.C(f"C16{i+1}", 120 + i * 8, 200, v, fp="C0805" if "uF" in v else "C0603")
        sch.power(c, "1", "+3V3"); sch.power(c, "2", "GND")
    # --- reg-pin bypass caps (do not load) ---
    Car = sch.C("C164", 120, 165, "100nF"); sch.net(Car, "1", "AMP_ANAREG"); sch.power(Car, "2", "GND")
    Car2 = sch.C("C165", 128, 165, "1uF"); sch.net(Car2, "1", "AMP_ANAREG"); sch.power(Car2, "2", "GND")
    sch.net(U9, pin("ANA_REG"), "AMP_ANAREG")
    Cref = sch.C("C166", 138, 165, "100nF"); sch.net(Cref, "1", "AMP_ANAREF"); sch.power(Cref, "2", "GND")
    sch.net(U9, pin("ANA_REF"), "AMP_ANAREF")
    Cvcom = sch.C("C167", 148, 165, "1uF"); sch.net(Cvcom, "1", "AMP_VCOM"); sch.power(Cvcom, "2", "GND")
    sch.net(U9, pin("VCOM"), "AMP_VCOM")
    # --- PVDD (4 pins) + bulk ---
    for p in nm["PVDD"]:
        sch.net(U9, p, "PVDD")
    for i, (v, fp) in enumerate([("100nF", "C0603"), ("1uF", "C0603"),
                                 ("220uF", "CP_bulk")]):
        c = (sch.CP(f"C17{i}", 250 + i * 8, 210, v) if fp == "CP_bulk"
             else sch.C(f"C17{i}", 250 + i * 8, 210, v, fp=fp))
        sch.net(c, "1", "PVDD"); sch.power(c, "2", "GND")
    # --- grounds ---
    for gname in ("DGND", "PGND", "GGND", "PAD"):
        for p in nm[gname]:
            sch.power(U9, p, "GND")
    for p in nm["NC"]:               # datasheet: tie NC to GND for thermal
        sch.power(U9, p, "GND")
    # --- PBTL: OUTA+ || OUTB+ = OUTP, OUTA- || OUTB- = OUTN, then LC ---
    sch.net(U9, pin("SPK_OUTA+"), "AMP_OUTP"); sch.net(U9, pin("SPK_OUTB+"), "AMP_OUTP")
    sch.net(U9, pin("SPK_OUTA-"), "AMP_OUTN"); sch.net(U9, pin("SPK_OUTB-"), "AMP_OUTN")

    # --- bootstrap caps: BSTRPx <-> its (paralleled) output node ---
    boots = [("BSTRPA+", "AMP_OUTP"), ("BSTRPA-", "AMP_OUTN"),
             ("BSTRPB+", "AMP_OUTP"), ("BSTRPB-", "AMP_OUTN")]
    for i, (b, onet) in enumerate(boots):
        cb = sch.C(f"C18{i}", 250 + i * 16, 110, "33nF")   # spread horizontally
        sch.net(cb, "1", f"AMP_{b}"); sch.net(cb, "2", onet)
        sch.net(U9, pin(b), f"AMP_{b}")
    sch.text("Bootstrap caps 33nF: VERIFY value vs TAS5760M datasheet.", 245, 100, size=1.1)
    La = sch.L("L5", 285, 150, "10uH", fp="L4030"); sch.net(La, "1", "AMP_OUTP"); sch.net(La, "2", "SPK_P")
    Lb = sch.L("L6", 285, 170, "10uH", fp="L4030"); sch.net(Lb, "1", "AMP_OUTN"); sch.net(Lb, "2", "SPK_N")
    Cfp = sch.C("C184", 305, 155, "0.68uF", fp="C0805"); sch.net(Cfp, "1", "SPK_P"); sch.power(Cfp, "2", "GND")
    Cfn = sch.C("C185", 305, 175, "0.68uF", fp="C0805"); sch.net(Cfn, "1", "SPK_N"); sch.power(Cfn, "2", "GND")
    JS = sch.comp("J3", "Connector_Generic:Conn_01x02", 330, 165,
                  value="DMA58-4 (4 ohm)",
                  footprint="Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical")
    sch.net(JS, "1", "SPK_P"); sch.net(JS, "2", "SPK_N")

    # ================= LTC4412 PVDD mux (12V priority / 5V backup) =================
    U10 = sch.comp("U10", "Power_Management:LTC4412xS6", 90, 165,
                   value="LTC4412ES6", footprint=FP["SOT23-6"])
    sch.net(U10, "1", "+5V")                 # VIN
    Cmux = sch.C("C186", 65, 185, "100nF"); sch.power(Cmux, "1", "+5V"); sch.power(Cmux, "2", "GND")
    sch.net(U10, "6", "PVDD")                # SENSE
    Csn = sch.C("C187", 115, 150, "100nF"); sch.net(Csn, "1", "PVDD"); sch.power(Csn, "2", "GND")
    sch.net(U10, "5", "MUX_GATE")            # GATE
    sch.power(U10, "3", "GND")               # CTL = GND (auto)
    sch.nc(U10, "4")                         # STAT (open-drain) unused
    sch.power(U10, "2", "GND")
    # Q_5V ideal-diode P-FET: S=+5V, D=PVDD, G=GATE
    Q5 = sch.comp("Q4", "Transistor_FET:AO3401A", 90, 200, value="AO3401A",
                  footprint=FP["SOT23-3"])
    sch.power(Q5, "2", "+5V"); sch.net(Q5, "3", "PVDD"); sch.net(Q5, "1", "MUX_GATE")
    # 12V leg Schottky: +12V -> PVDD
    Dm = sch.D_schottky("D30", 120, 200, "SS34", fp="SMA")
    sch.net(Dm, "2", "+12V"); sch.net(Dm, "1", "PVDD")  # A=12V, K=PVDD
    sch.text("PVDD = 12V (plugged, via Schottky) OR 5V (battery, via LTC4412 FET) -> quieter alarm.",
             30, 240, size=1.2)
