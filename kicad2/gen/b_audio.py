"""Block: AUDIO — TAS5760M (PBTL mono) + output LC + speaker + LTC4412 PVDD
mux (12 V plugged / 5 V battery). I2S is WIRED from the MCU; the +12V wire
arrives from the rails block at (800.10, 393.70).
Left-pin support parts hang on "combs" (staggered verticals) below the amp.
"""
U = 2.54


def build(s):
    U8 = s.parts["U8"]
    s.frame(610, 190, 835, 385, "AUDIO — TAS5760M PBTL + LC filter + PVDD mux (12V/5V)")

    U9 = s.comp("U9", "clock:TAS5760M", 695.96, 246.38, value="TAS5760MDAPR",
                footprint="Package_SO:HTSSOP-32-1EP_6.1x11mm_P0.65mm_EP5.2x11mm_Mask4.11x4.36mm",
                refpos=(675.64, 217.81, "left"), valpos=(675.64, 220.35, "left"))

    # ---- I2S from the MCU (order reverses between MCU and amp) ----
    s.pw(U8, "20", ("x", 635.00), ("y", 224.79), ("px", U9, "16"))   # DOUT -> SDIN
    s.pw(U8, "19", ("x", 640.08), ("y", 229.87), ("px", U9, "17"))   # LRCLK -> LRCK
    s.pw(U8, "18", ("x", 645.16), ("y", 227.33), ("px", U9, "15"))   # BCLK -> SCLK
    s.pw(U8, "37", ("x", 650.24), ("y", 232.41), ("px", U9, "14"))   # MCLK

    # ---- I2C (shared bus) ----
    s.glabel(U9, "8", "I2C_SDA")
    s.glabel(U9, "9", "I2C_SCL")

    # ---- control straps ----
    # GAIN0+GAIN1 tied high -> software/I2C control mode
    R60 = s.R("R60", 652.78, 215.90, "10k")
    s.rail(R60, "1", "+3V3", rise=0)
    s.pw(U9, "11", ("x", 652.78), ("py", R60, "2"))
    s.pw(U9, "12", ("x", 652.78), ("y", 240.03))
    # SLEEP/ADR -> 0R to GND (address 0x6C)
    R61 = s.R("R61", 631.19, 252.73, "0R")
    s.pw(U9, "13", ("x", 631.19), ("pin", R61, "1"))
    s.gnd(R61, "2", drop=0)
    # SPK_SD (expander GPA0) / SPK_FAULT (open-drain, pull-up, GPB6)
    s.pw(U9, "7", ("x", 626.11), ("y", 257.81))
    s.glabel_at("SPK_SD", 626.11, 257.81, 180)
    s.pw(U9, "6", ("x", 623.57), ("y", 262.89))
    s.glabel_at("SPK_FAULT", 623.57, 262.89, 180)   # pull-up R62 at the expander

    # ---- supplies + reg bypass (combs below-left) ----
    combs = [("10", 638.81, None),          # DVDD
             ("1", 643.89, None),           # AVDD
             ("32", 648.97, None),          # GVDD_REG
             ("3", 656.59, None),           # ANA_REG
             ("5", 661.67, None),           # ANA_REF
             ("4", 666.75, None)]           # VCOM
    for pin, x, _ in combs:
        s.pw(U9, pin, ("x", x), ("y", 284.48))
    # row A caps (DVDD, GVDD, ANA_REF)
    for ref, x, v in [("C161", 638.81, "100nF"), ("C160", 648.97, "1uF"),
                      ("C166", 661.67, "100nF")]:
        c = s.C(ref, x, 288.29, v)
        s.gnd(c, "2", drop=0, show_value=(ref != "C160"))
    # row B caps (AVDD, ANA_REG, VCOM) — combs continue through row A
    for x in (643.89, 656.59, 666.75):
        s.w((x, 284.48), (x, 293.37))
    for ref, x, v, fp in [("C162", 643.89, "10uF", "C0805"),
                          ("C164", 656.59, "100nF", "C0603"),
                          ("C167", 666.75, "1uF", "C0603")]:
        c = s.C(ref, x, 297.18, v, fp=fp)
        s.gnd(c, "2", drop=0)
    # ANA_REG second cap (1uF)
    C165 = s.C("C165", 651.51, 297.18, "1uF")
    s.pw(C165, "1", ("x", 656.59))
    s.gnd(C165, "2", drop=0)
    # DVDD/AVDD are +3V3
    s.w((638.81, 280.67), (643.89, 280.67))
    s.w((638.81, 280.67), (633.73, 280.67))
    s.power_at(633.73, 280.67, "+3V3")
    # SFT_CLIP tied to GVDD_REG (soft-clipper off)
    s.pw(U9, "2", ("x", 648.97))
    # grounds (bottom pins)
    s.w((690.88, 274.32), (701.04, 274.32))
    s.w((695.96, 274.32), (695.96, 276.86))
    s.power_at(695.96, 276.86, "GND")

    # ---- outputs: PBTL ties + bootstraps + LC filter + speaker ----
    s.pw(U9, "29", ("x", 730.25))                     # OUTA+
    s.pw(U9, "20", ("x", 730.25))                     # OUTB+
    s.w((730.25, 234.95), (730.25, 240.03))
    s.pw(U9, "26", ("x", 725.17))                     # OUTA-
    s.pw(U9, "23", ("x", 725.17))                     # OUTB-
    s.w((725.17, 237.49), (725.17, 242.57))
    L5 = s.L("L5", 749.30, 234.95, "10uH", rot=90)
    s.w((730.25, 234.95), (745.49, 234.95))
    L6 = s.L("L6", 751.84, 251.46, "10uH", rot=90)
    s.pw(L6, "1", ("x", 725.17), ("y", 242.57))
    # bootstrap caps (BSTx -> switching node)
    for pin, x, ref in [("30", 727.71, "C180"), ("25", 734.06, "C181"),
                        ("19", 740.41, "C182"), ("24", 746.76, "C183")]:
        s.pw(U9, pin, ("x", x), ("y", 260.35))
        s.C(ref, x, 264.16, "33nF",
            refpos=(x + 1.4, 262.76, "left"), valpos=(x + 1.4, 265.56, "left"))
    s.w((727.71, 267.97), (727.71, 270.51), (740.41, 270.51), (740.41, 267.97))
    s.w((737.87, 270.51), (737.87, 234.95))           # -> OUT+ node
    s.w((734.06, 267.97), (734.06, 273.05), (746.76, 273.05), (746.76, 267.97))
    s.w((744.22, 273.05), (744.22, 251.46))           # -> OUT- node
    # filter caps + speaker connector
    C184 = s.C("C184", 768.35, 240.03, "0.68uF", fp="C0805")
    s.pw(L5, "2", ("x", 793.75))
    s.pw(C184, "1", ("y", 234.95))
    s.gnd(C184, "2", drop=0)
    C185 = s.C("C185", 773.43, 256.54, "0.68uF", fp="C0805")
    s.pw(L6, "2", ("x", 773.43))
    s.pw(C185, "1", ("y", 251.46))
    s.gnd(C185, "2", drop=0)
    J3 = s.comp("J3", "Connector_Generic:Conn_01x02", 798.83, 234.95,
                value="Speaker DMA58-4 (4R)",
                footprint="Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical")
    s.pw(J3, "2", ("x", 788.67), ("y", 251.46), ("x", 773.43))

    # ---- PVDD node + bulk + mux ----
    s.pw(U9, "21", ("x", 723.90))
    s.pw(U9, "28", ("x", 723.90))
    s.w((723.90, 255.27), (723.90, 312.42))           # PVDD column
    s.w((723.90, 283.21), (718.82, 283.21))
    s.power_at(718.82, 283.21, "PVDD")
    s.pwr_flag(721.36, 283.21)
    s.w((723.90, 289.56), (746.76, 289.56))
    for ref, x, v, fp in [("C170", 731.52, "100nF", "C0603"),
                          ("C171", 739.14, "1uF", "C0603"),
                          ("C172", 746.76, "220uF", None)]:
        c = s.CP(ref, x, 293.37, v) if v == "220uF" else s.C(ref, x, 293.37, v, fp=fp)
        s.pw(c, "1", ("y", 289.56))
        s.gnd(c, "2", drop=0)

    # LTC4412: 5 V leg via Q4 (ideal diode), 12 V leg via D30
    U10 = s.comp("U10", "Power_Management:LTC4412xS6", 695.96, 314.96,
                 value="LTC4412ES6",
                 footprint="Package_TO_SOT_SMD:SOT-23-6")
    s.pw(U10, "1", ("x", 680.72), ("y", 309.88))
    s.power_at(680.72, 309.88, "+5V")
    C186 = s.C("C186", 680.72, 317.50, "100nF")
    s.pw(C186, "1", ("y", 312.42))
    s.gnd(C186, "2", drop=0)
    s.gnd(U10, "3", via=0)                            # CTL = GND (automatic)
    s.gnd(U10, "2")
    s.nc(U10, "4")                                    # STAT unused
    Q4 = s.comp("Q4", "clock:AO3401A", 711.20, 302.26, rot=270, value="AO3401A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(716.28, 295.91, None), valpos=(711.20, 309.88, None))
    s.pw(U10, "5", ("y", 298.45), ("px", Q4, "1"), ("pin", Q4, "1"))
    s.pw(Q4, "2", ("x", 701.04), ("y", 295.91))
    s.power_at(701.04, 295.91, "+5V")
    s.pw(Q4, "3", ("x", 723.90))                      # drain -> PVDD
    s.pw(U10, "6", ("x", 723.90))                     # SENSE -> PVDD
    s.w((723.90, 312.42), (723.90, 335.28))
    D30 = s.D_schottky("D30", 800.10, 345.44, "SS34", rot=270)
    s.w((800.10, 393.70), (800.10, 349.25))           # +12V from the rails
    s.pw(D30, "1", ("y", 335.28), ("x", 723.90))      # cathode -> PVDD
    s.text("PVDD auto-mux: 12 V wins when plugged; on battery Q4 ideal-diodes", 640, 355, size=1.3)
    s.text("the 5 V rail -> ~3 W quieter alarm.  Firmware: LEDs+audio <= ~12 W.", 640, 360, size=1.3)
    s.text("PBTL mono (reg 0x06[7]=1): OUTA||OUTB.  MCLK 256fs ~12.288 MHz.", 640, 365, size=1.3)
