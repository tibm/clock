"""Block: STORAGE — microSD (DM3AT) alone on SPI2. The Sharp display + FH34
FPC (J5) were dropped with the v0.19 cube redesign, so SPI2 is dedicated to
the card. SCLK/MOSI/MISO/CS are WIRED from the MCU through the x=663..683
corridor (same geometry as before — only the display branches are gone).
"""
U = 2.54


def build(s):
    U8 = s.parts["U8"]
    s.frame(610, 110, 835, 180, "STORAGE — microSD (DM3AT), SPI2 dedicated")

    J6 = s.comp("J6", "clock:Micro_SD_Card_Det_Hirose_DM3AT", 762.00, 152.40,
                value="DM3AT-SF-PEJM5",
                footprint="Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5")
    s.pw(U8, "21", ("x", 668.02), ("y", 152.40), ("px", J6, "5"))   # SCLK -> CLK
    s.pw(U8, "22", ("x", 673.10), ("y", 147.32), ("px", J6, "3"))   # MOSI -> CMD
    s.pw(U8, "23", ("x", 678.18), ("y", 157.48), ("px", J6, "7"))   # MISO = DAT0
    s.pw(U8, "11", ("x", 683.26), ("y", 144.78), ("px", J6, "2"))   # SD_CS = DAT3
    # pull-ups: SD_CS (boot-idle), DAT1/DAT2 (avoid SD-mode glitches)
    R70 = s.R("R70", 690.88, 138.43, "10k")
    s.rail(R70, "1", "+3V3", rise=0)
    s.pw(R70, "2", ("y", 144.78))
    R72 = s.R("R72", 709.93, 138.43, "10k")
    s.rail(R72, "1", "+3V3", rise=0)
    s.pw(J6, "1", ("px", R72, "2"))                                 # DAT2
    R71 = s.R("R71", 661.67, 134.62, "10k")
    s.rail(R71, "1", "+3V3", rise=1.27)
    s.pw(J6, "8", ("pin", R71, "2"))                                # DAT1
    # VDD -> +3V3 (through the row gap, up left of the corridors)
    s.pw(J6, "4", ("x", 656.59), ("y", 137.16))
    s.power_at(656.59, 137.16, "+3V3")
    # VSS + shield -> GND
    s.pw(J6, "6", ("x", 654.05), ("y", 167.64))
    s.power_at(654.05, 167.64, "GND")
    s.gnd(J6, "SH", via=0)
    s.nc(J6, "9")
    s.nc(J6, "10")
    # 3V3 decoupling at the card
    C222 = s.C("C222", 695.96, 120.65, "10uF", fp="C0805")
    s.rail(C222, "1", "+3V3", rise=0)
    s.gnd(C222, "2", drop=1.27, show_value=False)
    C223 = s.C("C223", 703.58, 120.65, "100nF")
    s.rail(C223, "1", "+3V3", rise=0)
    s.gnd(C223, "2", drop=1.27, show_value=False)
    s.text("Layout: C222/C223 at the J6 VDD pin.", 688.34, 130, size=1.3)
    s.text("SPI mode: MSB-first, CS active-low, ~25 MHz. No card-detect (no spare GPIO).",
           688.34, 174, size=1.3)
