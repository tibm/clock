"""Block: MOTOR — 2x TB6612FNG (PWM-on-IN microstep) -> Juken X40.879
dual-shaft stepper (M1, soldered directly on the PCB; shafts pass through
the board). All 8 MCPWM lines are WIRED from the MCU down the x=600..621
corridor (order-preserving, no crossings within each group).
Driver #1 = minute (external shaft), #2 = hour (internal shaft).
"""
U = 2.54


def _driver(s, ref, y0, cbase, stby_up=False, rail_rise=17.78):
    u = s.comp(ref, "Driver_Motor:TB6612FNG", 693.42, y0, value="TB6612FNG",
               footprint="Package_SO:SSOP-24_5.3x8.2mm_P0.65mm",
               refpos=(664.21, y0 - 25.40, "right"),
               valpos=(664.21, y0 - 22.86, "right"))
    # STBY (shared expander line, idle-low at boot); U12's label jogs up a
    # row so it clears the +3V3 strap glyph
    if stby_up:
        s.pw(u, "19", ("x", 676.91), ("y", y0 - 17.78), ("x", 668.02))
        s.glabel_at("STEP_STBY", 668.02, y0 - 17.78, 180)
    else:
        s.pw(u, "19", ("x", 668.02))
        s.glabel_at("STEP_STBY", 668.02, y0 - 10.16, 180)
    # PWMA/PWMB tied high to VCC (3V3): PWM rides the IN pins
    s.pw(u, "23", ("x", 673.10))
    s.pw(u, "15", ("x", 673.10), ("y", y0 - 5.08))
    s.w((673.10, y0 - 5.08), (673.10, y0 - rail_rise))
    s.power_at(673.10, y0 - rail_rise, "+3V3")
    # supplies
    s.rail(u, "20", "+3V3", rise=2.54)               # VCC
    for p in ("13", "14"):                            # VM1..VM3 tie -> +5V
        s.pw(u, p, ("dy", -2.54))
    s.pw(u, "24", ("dy", -2.54))
    s.w((695.96, y0 - 27.94), (701.04, y0 - 27.94))
    s.w((698.50, y0 - 27.94), (698.50, y0 - 30.48))
    s.power_at(698.50, y0 - 30.48, "+5V")
    # grounds
    for p in ("18", "3", "9"):
        s.pw(u, p, ("dy", 2.54))
    s.w((685.80, y0 + 27.94), (701.04, y0 + 27.94))
    s.w((695.96, y0 + 27.94), (695.96, y0 + 30.48))
    s.power_at(695.96, y0 + 30.48, "GND")
    # decoupling (VM + VCC)
    for cref, x, v, fp, net in [(cbase + "0", 711.20, "10uF", "C0805", "+5V"),
                                (cbase + "1", 718.82, "100nF", "C0603", "+5V"),
                                (cbase + "2", 726.44, "100nF", "C0603", "+3V3")]:
        c = s.C(cref, x, y0 - 30.48, v, fp=fp)
        s.rail(c, "1", net, rise=0)
        s.gnd(c, "2", drop=0)
    return u


def build(s):
    U8 = s.parts["U8"]
    s.frame(610, 405, 835, 585, "MOTOR — 2x TB6612FNG -> X40.879 dual-shaft stepper")

    U11 = _driver(s, "U11", 467.36, "C20")            # minute / external shaft
    U12 = _driver(s, "U12", 543.56, "C21",            # hour / internal shaft
                  stby_up=True, rail_rise=7.62)

    # 8 MCPWM lines, MCU -> drivers (order-preserving corridor)
    s.pw(U8, "4", ("x", 600.71), ("y", 469.90), ("px", U11, "21"))   # M_AIN1
    s.pw(U8, "5", ("x", 603.25), ("y", 472.44), ("px", U11, "22"))   # M_AIN2
    s.pw(U8, "6", ("x", 605.79), ("y", 474.98), ("px", U11, "17"))   # M_BIN1
    s.pw(U8, "15", ("x", 608.33), ("y", 477.52), ("px", U11, "16"))  # M_BIN2
    s.pw(U8, "31", ("x", 613.41), ("y", 546.10), ("px", U12, "21"))  # H_AIN1
    s.pw(U8, "32", ("x", 615.95), ("y", 548.64), ("px", U12, "22"))  # H_AIN2
    s.pw(U8, "33", ("x", 618.49), ("y", 551.18), ("px", U12, "17"))  # H_BIN1
    s.pw(U8, "34", ("x", 621.03), ("y", 553.72), ("px", U12, "16"))  # H_BIN2

    # stepper, soldered on-board (J4 connector dropped 2026-07-17): pins
    # 1-4 = 1e..4e (external coil pair), 5-8 = 1i..4i (internal); custom
    # footprint = through-board shafts + 3 snap-peg NPTH
    M1 = s.comp("M1", "clock:X40_879", 777.24, 502.92,
                value="X40.879",
                footprint="clock:Juken_X40-879_DualShaft")
    s.pw(U11, "1", ("x", 736.60), ("py", M1, "1"), ("pin", M1, "1"))    # AO1 -> 1e
    s.pw(U11, "5", ("x", 739.14), ("py", M1, "2"), ("pin", M1, "2"))    # AO2 -> 2e
    s.pw(U11, "7", ("x", 741.68), ("py", M1, "3"), ("pin", M1, "3"))    # BO2 -> 3e
    s.pw(U11, "11", ("x", 744.22), ("py", M1, "4"), ("pin", M1, "4"))   # BO1 -> 4e
    s.pw(U12, "1", ("x", 746.76), ("py", M1, "5"), ("pin", M1, "5"))    # AO1 -> 1i
    s.pw(U12, "5", ("x", 749.30), ("py", M1, "6"), ("pin", M1, "6"))    # AO2 -> 2i
    s.pw(U12, "7", ("x", 751.84), ("py", M1, "7"), ("pin", M1, "7"))    # BO2 -> 3i
    s.pw(U12, "11", ("x", 754.38), ("py", M1, "8"), ("pin", M1, "8"))   # BO1 -> 4i

    s.text("Layout: each driver's 10uF+100nF go at its VM pins, the 100nF at its VCC pin.", 615, 415, size=1.3)
    s.text("PWM-on-IN microstepping: MCPWM waveforms on AIN/BIN, PWMA/B tied high.", 615, 575, size=1.3)
    s.text("STEP_STBY (expander GPA1) low at boot -> drivers off. Coil map: esp32.md.", 615, 579.5, size=1.3)
