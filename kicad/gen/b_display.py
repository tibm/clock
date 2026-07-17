"""Block: DISPLAY (Sharp LS032B7DD02 via FH34 FPC) + microSD (DM3AT), shared
SPI2. All five SPI lines are WIRED from the MCU through the x=663..683
corridor; SCLK/MOSI branch to both devices.
"""
U = 2.54


def build(s):
    U8 = s.parts["U8"]
    s.frame(610, 40, 835, 180, "DISPLAY (LS032B7DD02, FPC) + microSD — shared SPI2")

    # ---- display FPC connector ----
    J5 = s.comp("J5", "Connector_Generic:Conn_01x10", 740.41, 66.04,
                value="LS032B7DD02 (FH34SRJ-10S)",
                footprint="Connector_FFC-FPC:Hirose_FH12-10S-0.5SH_1x10-1MP_P0.50mm_Horizontal")
    # SPI wires from the MCU (branching to the microSD below)
    s.pw(U8, "21", ("x", 668.02), ("y", 55.88), ("px", J5, "1"))    # SCLK
    s.w((668.02, 106.68), (668.02, 152.40))                         # branch down
    s.pw(U8, "22", ("x", 673.10), ("y", 58.42), ("px", J5, "2"))    # MOSI/SI
    s.w((673.10, 109.22), (673.10, 147.32))
    s.pw(U8, "10", ("x", 662.94), ("y", 60.96), ("px", J5, "3"))    # LCD_CS/SCS
    # EXTCOMIN unused (software VCOM) + EXTMODE/VSS/VSSA -> GND
    s.pw(J5, "8", ("x", 723.90))
    s.pw(J5, "9", ("x", 723.90))
    s.pw(J5, "10", ("x", 723.90))
    s.w((723.90, 73.66), (723.90, 78.74), (723.90, 81.28))
    s.power_at(723.90, 81.28, "GND")
    s.pw(J5, "4", ("x", 730.25), ("y", 73.66))                      # EXTCOMIN -> GND
    # DISP on/off (expander GPB3)
    s.pw(J5, "5", ("x", 725.17))
    s.glabel_at("LCD_DISP", 725.17, 66.04, 180)
    # panel power: VDDA + VDD -> +5V
    s.pw(J5, "6", ("x", 707.39))
    s.pw(J5, "7", ("x", 707.39))
    s.w((707.39, 71.12), (707.39, 52.07))
    s.power_at(707.39, 52.07, "+5V")
    C220 = s.C("C220", 698.50, 80.01, "1uF")
    s.rail(C220, "1", "+5V", rise=0)
    s.gnd(C220, "2", drop=0)
    C221 = s.C("C221", 688.34, 80.01, "1uF")
    s.rail(C221, "1", "+5V", rise=0)
    s.gnd(C221, "2", drop=0)
    s.text("EXTMODE=L -> software VCOM (frame-inversion bit, >=1 Hz).", 688.34, 92, size=1.3)
    s.text("SCS active-HIGH, LSB-first, <=2 MHz; write-only (no MISO).", 688.34, 96.5, size=1.3)
    s.text("Layout: C220/C221 at the J5 VDDA/VDD pins; C222/C223 at the J6 VDD pin.", 688.34, 101, size=1.3)

    # ---- microSD ----
    J6 = s.comp("J6", "clock:Micro_SD_Card_Det_Hirose_DM3AT", 762.00, 152.40,
                value="DM3AT-SF-PEJM5",
                footprint="Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5")
    s.w((668.02, 152.40), (739.14, 152.40))                         # CLK
    s.w((673.10, 147.32), (739.14, 147.32))                         # CMD = MOSI
    s.pw(U8, "23", ("x", 678.18), ("y", 157.48), ("px", J6, "7"))   # MISO = DAT0
    s.pw(U8, "11", ("x", 683.26), ("y", 144.78), ("px", J6, "2"))   # SD_CS = DAT3
    # pull-ups: SD_CS (boot-idle), DAT1/DAT2 (avoid SD-mode glitches)
    R70 = s.R("R70", 690.88, 138.43, "10k")
    s.rail(R70, "1", "+3V3", rise=0)
    s.pw(R70, "2", ("y", 144.78))
    R72 = s.R("R72", 709.93, 138.43, "10k")
    s.rail(R72, "1", "+3V3", rise=0)
    s.pw(J6, "1", ("px", R72, "2"))                                 # DAT2
    R71 = s.R("R71", 664.21, 134.62, "10k")
    s.rail(R71, "1", "+3V3", rise=0)
    s.pw(J6, "8", ("x", 664.21), ("pin", R71, "2"))                 # DAT1
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
    C222 = s.C("C222", 695.96, 124.46, "10uF", fp="C0805")
    s.rail(C222, "1", "+3V3", rise=0)
    s.gnd(C222, "2", drop=0, show_value=False)
    C223 = s.C("C223", 703.58, 124.46, "100nF")
    s.rail(C223, "1", "+3V3", rise=0)
    s.gnd(C223, "2", drop=0, show_value=False)
    s.text("SPI mode: MSB-first, CS active-low, ~25 MHz. No card-detect (no spare GPIO).",
           688.34, 174, size=1.3)
