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
    s.pw(U8, "37", ("x", 648.97), ("y", 232.41), ("px", U9, "14"))   # MCLK

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
    # SPK_SD (expander GPA0) / SPK_FAULT (open-drain, pull-up, GPB6);
    # SPK_SD's boot-state pulldown R63 lives in the IO block next to R62
    s.pw(U9, "7", ("x", 666.75))
    s.glabel_at("SPK_SD", 666.75, 247.65, 180)
    s.pw(U9, "6", ("x", 652.78))
    s.glabel_at("SPK_FAULT", 652.78, 250.19, 180)   # pull-up R62 at the expander

    # ---- supplies + reg bypass (staggered caps below-left) ----
    C161 = s.C("C161", 638.81, 288.29, "100nF")          # DVDD
    s.gnd(C161, "2", drop=0)
    # AVDD gets its own 100nF (datasheet: 0.1uF close to EACH supply pin;
    # C162 10uF alone left AVDD without a local HF bypass). x=629.92 +
    # right-justified texts keep it clear of C161's (cosmetics-pinned) labels
    C163 = s.C("C163", 629.92, 288.29, "100nF",
               refpos=(628.65, 286.89, "right"), valpos=(628.65, 289.69, "right"))
    s.pw(C163, "1", ("y", 280.67), ("x", 633.73))
    s.gnd(C163, "2", drop=0)
    C160 = s.C("C160", 648.97, 288.29, "1uF")            # GVDD_REG
    s.gnd(C160, "2", drop=0, show_value=False)
    C162 = s.C("C162", 643.89, 306.07, "10uF", fp="C0805")   # AVDD
    s.gnd(C162, "2", drop=1.27)
    C164 = s.C("C164", 656.59, 313.69, "100nF")          # ANA_REG
    s.gnd(C164, "2", drop=8.89)
    C165 = s.C("C165", 651.51, 300.99, "1uF")            # ANA_REG 2nd cap
    s.pw(C165, "1", ("x", 656.59))
    s.gnd(C165, "2", drop=13.97)
    C166 = s.C("C166", 662.94, 276.86, "100nF")          # ANA_REF
    s.gnd(C166, "2", drop=3.81)
    C167 = s.C("C167", 666.75, 297.18, "1uF")            # VCOM
    s.gnd(C167, "2", drop=0)
    # combs from the pins down into their caps
    s.pw(U9, "10", ("x", 638.81), ("py", C161, "1"))
    s.pw(U9, "1", ("x", 643.89), ("py", C162, "1"))
    s.pw(U9, "32", ("x", 648.97), ("py", C160, "1"))
    s.pw(U9, "3", ("x", 656.59), ("py", C164, "1"))
    s.pw(U9, "5", ("x", 662.94), ("py", C166, "1"))
    s.pw(U9, "4", ("x", 666.75), ("py", C167, "1"))
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
    s.pw(U9, "29", ("x", 734.06))                     # OUTA+  (224.79)
    s.pw(U9, "20", ("x", 734.06))                     # OUTB+  (229.87)
    s.w((734.06, 224.79), (734.06, 229.87))
    s.pw(U9, "26", ("x", 750.57))                     # OUTA-  (227.33)
    s.pw(U9, "23", ("x", 750.57))                     # OUTB-  (232.41)
    s.w((750.57, 227.33), (750.57, 232.41))
    L5 = s.L("L5", 758.19, 224.79, "10uH", rot=90)
    s.w((734.06, 224.79), (754.38, 224.79))
    L6 = s.L("L6", 758.19, 241.30, "10uH", rot=90)
    s.pw(L6, "1", ("x", 750.57), ("y", 232.41))
    # bootstrap caps (BSTx -> switching node); 220nF per datasheet fig. 62
    for pin, x, ref in [("30", 727.71, "C180"), ("25", 734.06, "C181"),
                        ("19", 741.68, "C182"), ("24", 754.38, "C183")]:
        s.pw(U9, pin, ("x", x), ("y", 250.19))
        s.C(ref, x, 254.00, "220nF",
            refpos=(x + 1.4, 252.59, "left"), valpos=(x + 1.4, 255.39, "left"))
    s.w((727.71, 257.81), (727.71, 260.35), (748.03, 260.35), (748.03, 224.79))
    s.w((741.68, 257.81), (741.68, 260.35))           # C182 -> BST-A row
    s.w((734.06, 257.81), (734.06, 262.89), (754.38, 262.89), (754.38, 257.81))
    s.w((750.57, 241.30), (750.57, 262.89))           # -> OUT- node
    # filter caps + speaker connector
    C184 = s.C("C184", 768.35, 228.60, "0.68uF", fp="C0805")
    s.pw(L5, "2", ("x", 793.75))
    s.pw(C184, "1", ("y", 224.79))
    s.gnd(C184, "2", drop=0)
    C185 = s.C("C185", 773.43, 245.11, "0.68uF", fp="C0805")
    s.pw(L6, "2", ("x", 773.43))
    s.pw(C185, "1", ("y", 241.30))
    s.gnd(C185, "2", drop=0)
    J3 = s.comp("J3", "Connector_Generic:Conn_01x02", 798.83, 224.79,
                value="Speaker DMA58-4 (4R)",
                footprint="Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical")
    s.pw(J3, "2", ("x", 788.67), ("y", 241.30), ("x", 773.43))

    # ---- PVDD node + bulk + mux ----
    s.pw(U9, "21", ("x", 723.90))                     # PVDD (247.65)
    s.pw(U9, "28", ("x", 723.90))                     # PVDD (250.19)
    s.w((723.90, 247.65), (723.90, 312.42))           # PVDD column
    s.w((723.90, 283.21), (713.74, 283.21))
    s.power_at(713.74, 283.21, "PVDD")
    s.pwr_flag(721.36, 283.21)
    s.w((723.90, 289.56), (751.84, 289.56))
    for ref, x, v, fp in [("C170", 731.52, "100nF", "C0603"),
                          ("C171", 739.14, "1uF", "C0603"),
                          ("C172", 751.84, "220uF", None)]:
        c = s.CP(ref, x, 293.37, v) if v == "220uF" else s.C(ref, x, 293.37, v, fp=fp)
        s.pw(c, "1", ("y", 289.56))
        s.gnd(c, "2", drop=0)

    # LTC4412: 5 V leg via Q4 (ideal diode), 12 V leg via D30
    U10 = s.comp("U10", "Power_Management:LTC4412xS6", 695.96, 314.96,
                 value="LTC4412ES6",
                 footprint="Package_TO_SOT_SMD:SOT-23-6")
    s.pw(U10, "1", ("x", 676.91), ("y", 308.61))
    s.power_at(676.91, 308.61, "+5V")
    C186 = s.C("C186", 676.91, 317.50, "100nF")
    s.pw(C186, "1", ("y", 312.42))
    s.gnd(C186, "2", drop=1.27)
    s.gnd(U10, "3", via=0)                            # CTL = GND (automatic)
    s.gnd(U10, "2")
    s.nc(U10, "4")                                    # STAT unused
    Q4 = s.comp("Q4", "clock:AO3401A", 711.20, 302.26, rot=270, value="AO3401A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(716.28, 295.91, None), valpos=(711.20, 309.88, None))
    s.pw(U10, "5", ("y", 295.91), ("px", Q4, "1"), ("pin", Q4, "1"))
    s.pw(Q4, "2", ("x", 701.04), ("y", 302.26))
    s.power_at(701.04, 302.26, "+5V")
    s.pw(Q4, "3", ("x", 723.90))                      # drain -> PVDD
    s.pw(U10, "6", ("x", 723.90))                     # SENSE -> PVDD
    s.w((723.90, 312.42), (723.90, 335.28))
    D30 = s.D_schottky("D30", 800.10, 345.44, "SS34", rot=270)
    s.w((800.10, 393.70), (800.10, 349.25))           # +12V from the rails
    s.pw(D30, "1", ("y", 335.28), ("x", 723.90))      # cathode -> PVDD
    s.text("PVDD auto-mux: 12 V wins when plugged; on battery Q4 ideal-diodes", 640, 355, size=1.3)
    s.text("the 5 V rail -> ~3 W quieter alarm.  Firmware: LEDs+audio <= ~12 W.", 640, 360, size=1.3)
    s.text("PBTL mono (reg 0x06[7]=1): OUTA||OUTB.  MCLK 256fs ~12.288 MHz.", 640, 365, size=1.3)
