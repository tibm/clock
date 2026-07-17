"""Blocks: RAILS — TPS61023 (+5V, always-on), TLV62569 (+3V3 from 5V),
TPS55340 (+12V from VBAT, plugged-only). Values per power_values.md §3/§4/§5.

The VBAT feed column (x=175.26, in the gutter between the battery and rails
frames) comes down from the charger staircase.
The +12V output is WIRED: down the x=281.94 column, branching at
(281.94,393.70) into a long horizontal run to the audio PVDD mux
(picked up by b_audio at (800.10,393.70)) and continuing down to the
wake-LED connector (picked up by b_led at (281.94,447.04)).
"""
U = 2.54


def build(s):
    s.frame(180, 222, 395, 278, "RAILS — 5 V boost (always on) -> 3V3 buck")
    s.frame(180, 286, 395, 384, "RAIL — 12 V boost (plugged-only: audio PVDD + wake LEDs)")

    # ================= TPS61023 -> +5V =================
    U5 = s.comp("U5", "clock:TPS61023", 223.52, 246.38, value="TPS61023",
                footprint="Package_TO_SOT_SMD:SOT-563",
                refpos=(217.17, 231.75, "left"), valpos=(217.17, 234.29, "left"))
    # VBAT feed (column from the charger staircase)
    s.w((175.26, 212.09), (175.26, 317.50))
    s.w((175.26, 243.84), (213.36, 243.84))          # -> VIN
    s.power_at(185.42, 241.30, "VBAT")
    s.w((185.42, 241.30), (185.42, 243.84))
    s.pw(U5, "2", ("x", 205.74), ("y", 243.84))      # EN tied to VBAT
    C120 = s.C("C120", 200.66, 251.46, "10uF", fp="C0805")
    s.pw(C120, "1", ("y", 243.84))
    s.gnd(C120, "2", drop=0)
    # inductor VBAT -> SW
    L2 = s.L("L2", 222.25, 236.22, "1uH", fp="L4020", rot=90)
    s.pw(L2, "1", ("x", 195.58), ("y", 243.84))
    s.pw(L2, "2", ("x", 232.41), ("py", U5, "5"), ("pin", U5, "5"))
    s.gnd(U5, "4")
    # VOUT rail: caps, +5V tap, flag, feed to the 3V3 buck
    s.pw(U5, "6", ("x", 281.94), ("y", 241.30), ("x", 289.56))  # ...-> U6 VIN
    C121 = s.C("C121", 241.30, 251.46, "22uF", fp="C0805")
    s.pw(C121, "1", ("y", 243.84))
    s.gnd(C121, "2", drop=0)
    C122 = s.C("C122", 248.92, 251.46, "22uF", fp="C0805")
    s.pw(C122, "1", ("y", 243.84))
    s.gnd(C122, "2", drop=0)
    s.power_at(254.00, 241.30, "+5V")
    s.w((254.00, 241.30), (254.00, 243.84))
    s.pwr_flag(264.16, 243.84)
    # FB divider (VOUT -> R40 -> FB -> R41 -> GND, C123 || R40)
    R40 = s.R("R40", 259.08, 250.19, "732k")
    s.pw(R40, "1", ("y", 243.84))
    C123 = s.C("C123", 269.24, 250.19, "220pF")
    s.route(R40, "1", C123, "1", "H")
    s.route(R40, "2", C123, "2", "H")
    R41 = s.R("R41", 259.08, 259.08, "100k")
    s.route(R40, "2", R41, "1", "V")
    s.gnd(R41, "2", drop=0)
    s.pw(U5, "1", ("x", 198.12), ("y", 269.24), ("x", 254.00), ("y", 253.99),
         ("px", R40, "2"))

    # ================= TLV62569 -> +3V3 =================
    U6 = s.comp("U6", "Regulator_Switching:TLV62569DBV", 297.18, 243.84,
                value="TLV62569DBVR", footprint="Package_TO_SOT_SMD:SOT-23-5")
    s.pw(U6, "1", ("x", 287.02), ("y", 241.30))      # EN tied to VIN
    C124 = s.C("C124", 285.75, 247.65, "10uF", fp="C0805")
    s.pw(C124, "1", ("y", 241.30))
    s.gnd(C124, "2", drop=0)
    s.gnd(U6, "2")
    L3 = s.L("L3", 312.42, 241.30, "2.2uH", fp="L4020", rot=90)
    s.route(U6, "3", L3, "1", "H")
    s.pw(L3, "2", ("x", 332.74))                     # +3V3 node
    s.power_at(325.12, 238.76, "+3V3")
    s.w((325.12, 238.76), (325.12, 241.30))
    s.pwr_flag(332.74, 241.30)
    C125 = s.C("C125", 321.31, 247.65, "22uF", fp="C0805")
    s.pw(C125, "1", ("y", 241.30))
    s.gnd(C125, "2", drop=0)
    # FB divider: +3V3 -> R42 -> FB node -> R43 -> GND, C126 || R42
    R42 = s.R("R42", 330.20, 247.65, "453k")
    s.pw(R42, "1", ("y", 241.30))
    C126 = s.C("C126", 340.36, 247.65, "6.8pF")
    s.route(R42, "1", C126, "1", "H")
    s.route(R42, "2", C126, "2", "H")
    R43 = s.R("R43", 330.20, 256.54, "100k")
    s.route(R42, "2", R43, "1", "V")
    s.gnd(R43, "2", drop=0)
    s.pw(U6, "5", ("x", 306.07), ("y", 259.08), ("x", 327.66), ("y", 251.46),
         ("px", R42, "2"))
    s.text("+5V: display panel, stepper VM, panel LEDs, PVDD-mux aux.  +3V3: MCU + all logic.",
           185, 274, size=1.3)

    # ================= TPS55340 -> +12V (plugged-only) =================
    U7 = s.comp("U7", "clock:TPS55340", 250.19, 325.12, value="TPS55340PWPR",
                footprint="Package_SO:Texas_HTSSOP-14-1EP_4.4x5mm_P0.65mm_EP3.4x5mm_Mask3.155x3.255mm")
    # VIN row from the VBAT column + input caps
    s.w((175.26, 317.50), (237.49, 317.50))
    for ref, x, v, fp in [("C129", 194.31, "100uF", None),
                          ("C127", 203.20, "10uF", "C0805"),
                          ("C128", 212.09, "100nF", "C0603")]:
        c = s.CP(ref, x, 323.85, v) if v == "100uF" else s.C(ref, x, 323.85, v, fp=fp)
        s.pw(c, "1", ("y", 317.50))
        s.gnd(c, "2", drop=0, show_value=False)
    # inductor VBAT -> SW (both SW pins tied at x=269.24)
    L4 = s.L("L4", 220.98, 309.88, "4.7uH", rot=90)
    s.pw(L4, "1", ("x", 186.69), ("y", 317.50))
    s.pw(L4, "2", ("x", 269.24), ("py", U7, "1"), ("pin", U7, "1"))
    s.pw(U7, "2", ("x", 269.24))
    s.w((269.24, 317.50), (269.24, 320.04))          # tie both SW pins
    # rectifier SW -> +12V (anode on SW)
    D20 = s.D_schottky("D20", 275.59, 309.88, "B340A", mirror="y")
    s.pw(D20, "1", ("x", 281.94))                    # cathode -> +12V node
    s.w((271.78, 309.88), (269.24, 309.88))          # anode <- SW node
    s.power_at(281.94, 307.34, "+12V")
    s.w((281.94, 307.34), (281.94, 309.88))
    s.w((281.94, 309.88), (287.02, 309.88))
    s.pwr_flag(287.02, 309.88)
    # output caps
    s.w((281.94, 309.88), (281.94, 393.70))          # +12V column (branch below)
    s.w((281.94, 316.23), (304.80, 316.23))
    for ref, x in [("C130", 289.56), ("C131", 297.18), ("C132", 304.80)]:
        c = s.C(ref, x, 320.04, "22uF", fp="C1210")
        s.gnd(c, "2", drop=0)
    # EN: BOOST12_EN (expander GPA2) + 100k pulldown (off at boot)
    s.pw(U7, "4", ("x", 224.79), ("y", 346.71), ("x", 198.12))
    s.glabel_at("BOOST12_EN", 198.12, 346.71, 180)
    R44 = s.R("R44", 205.74, 350.52, "100k")
    s.gnd(R44, "2", drop=0)
    # SS cap
    C133 = s.C("C133", 231.14, 335.28, "47nF",
               refpos=(230.10, 333.88, "right"), valpos=(230.10, 336.68, "right"))
    s.pw(U7, "5", ("x", 231.14), ("pin", C133, "1"))
    s.gnd(C133, "2", drop=0)
    # SYNC -> GND (not used)
    s.pw(U7, "6", ("x", 190.50), ("dy", 2.54))
    s.power_at(190.50, 327.66, "GND")
    # FREQ: R47 -> GND (500 kHz)
    R47 = s.R("R47", 222.25, 334.01, "95.3k",
              refpos=(221.21, 332.61, "right"), valpos=(221.21, 335.41, "right"))
    s.pw(U7, "10", ("px", R47, "1"))
    s.gnd(R47, "2", drop=0)
    # COMP: R48 + C134 series to GND, C135 parallel
    R48 = s.R("R48", 234.95, 349.25, "2.55k")
    s.pw(U7, "7", ("x", 234.95), ("pin", R48, "1"))
    C134 = s.C("C134", 234.95, 356.87, "100nF")
    s.gnd(C134, "2", drop=0)
    C135 = s.C("C135", 243.84, 349.25, "100pF")
    s.route(R48, "1", C135, "1", "H")
    s.gnd(C135, "2", drop=0)
    # FB divider: +12V -> R45 -> FB node -> R46 -> GND
    s.pw(U7, "9", ("x", 227.33), ("y", 368.30), ("x", 276.86))
    R46 = s.R("R46", 256.54, 372.11, "10k")
    s.gnd(R46, "2", drop=0)
    R45 = s.R("R45", 276.86, 364.49, "86.6k")
    s.pw(R45, "1", ("y", 341.63), ("x", 281.94))
    # grounds (right side tie + bottom pad)
    s.pw(U7, "8", ("x", 271.78))
    s.pw(U7, "12", ("x", 271.78))
    s.pw(U7, "13", ("x", 271.78))
    s.w((271.78, 327.66), (271.78, 335.28))
    s.power_at(271.78, 335.28, "GND")
    s.gnd(U7, "14", via=0)
    s.pw(U7, "15", ("dy", 2.54), ("px", U7, "14"))
    s.nc(U7, "11")
    s.text("PLUGGED-ONLY: firmware asserts BOOST12_EN only while PD_PG is live.",
           185, 380, size=1.3)
    s.text("~12 W ceiling from 1S input: wake LEDs + audio share it.", 185, 375.5, size=1.3)

    # long +12V run to the audio PVDD mux (b_audio picks up at 800.10)
    s.w((281.94, 393.70), (800.10, 393.70))
