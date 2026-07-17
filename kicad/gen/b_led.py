"""Block: LED DRIVERS — 3x AO3400A low-side switches.
Panel string (5 V, always) + wake COB pair (12 V, plugged-only).
The +12V wire arrives on the x=281.94 column from the rails block.
PWM lines come from the MCU as labels (long runs). Per led.md / pv §8.
"""
U = 2.54


def build(s):
    s.frame(180, 406, 395, 506, "LED DRIVERS — panel (5 V always) + wake COB (12 V plugged)")

    # ---- panel LED string connector (+5V, PANEL_K) ----
    J8 = s.comp("J8", "Connector_Generic:Conn_01x02", 340.36, 415.29,
                value="Panel LEDs 5V (CLM3C x12)",
                footprint="Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical")
    s.power_at(327.66, 412.75, "+5V")
    s.w((327.66, 412.75), (327.66, 415.29), (335.28, 415.29))
    Q5 = s.comp("Q5", "clock:AO3400A", 320.04, 427.99, value="AO3400A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(318.52, 426.59, "left"), valpos=(326.39, 431.80, "left"))
    s.pw(Q5, "3", ("y", 417.83), ("px", J8, "2"))    # drain -> PANEL_K
    s.gnd(Q5, "2", drop=0)
    R102 = s.R("R102", 304.80, 427.99, "100R", rot=90)
    s.route(R102, "2", Q5, "1", "H")
    s.pw(R102, "1", ("x", 298.45))
    s.glabel_at("PANEL_PWM", 298.45, 427.99, 180)
    R103 = s.R("R103", 308.61, 435.61, "10k")
    s.pw(R103, "1", ("y", 427.99))
    s.gnd(R103, "2", drop=0)

    # ---- wake COB connector (+12V, WARM_K, COOL_K) ----
    J9 = s.comp("J9", "Connector_Generic:Conn_01x03", 340.36, 449.58,
                value="Wake COB 12V (warm+cool)",
                footprint="Connector_JST:JST_PH_B3B-PH-K_1x03_P2.00mm_Vertical")
    s.w((281.94, 393.70), (281.94, 447.04), (335.28, 447.04))   # +12V run
    Q6 = s.comp("Q6", "clock:AO3400A", 313.69, 463.55, value="AO3400A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(318.52, 462.15, "left"), valpos=(326.39, 467.36, "left"))
    s.pw(Q6, "3", ("y", 449.58), ("px", J9, "2"))    # drain -> WARM_K
    s.gnd(Q6, "2", drop=0)
    R104 = s.R("R104", 304.80, 463.55, "100R", rot=90)
    s.route(R104, "2", Q6, "1", "H")
    s.pw(R104, "1", ("x", 298.45))
    s.glabel_at("WAKE_WARM_PWM", 298.45, 463.55, 180)
    R105 = s.R("R105", 308.61, 471.17, "10k")
    s.pw(R105, "1", ("y", 463.55))
    s.gnd(R105, "2", drop=0)

    Q7 = s.comp("Q7", "clock:AO3400A", 321.31, 486.41, value="AO3400A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(326.14, 485.01, "left"), valpos=(329.18, 490.22, "left"))
    s.pw(Q7, "3", ("y", 472.44), ("x", 331.47), ("y", 452.12),
         ("px", J9, "3"))                            # drain -> COOL_K
    s.gnd(Q7, "2", drop=0)
    R106 = s.R("R106", 312.42, 486.41, "100R", rot=90)
    s.route(R106, "2", Q7, "1", "H")
    s.pw(R106, "1", ("x", 303.53))
    s.glabel_at("WAKE_COOL_PWM", 303.53, 486.41, 180)
    R107 = s.R("R107", 316.23, 494.03, "10k")
    s.pw(R107, "1", ("y", 486.41))
    s.gnd(R107, "2", drop=0)
    # IO46 boot-strap pulldown (strap must be low at boot) on the PWM line
    R52 = s.R("R52", 306.07, 490.22, "10k",
              refpos=(305.03, 488.82, "right"), valpos=(305.03, 491.62, "right"))
    s.pw(R52, "1", ("y", 486.41))
    s.gnd(R52, "2", drop=0)

    s.text("Wake COB strips are self-ballasted (12 V). Panel string: one series R per LED,", 185, 491, size=1.3)
    s.text("on the LED board. ~1 kHz PWM, gamma-corrected; wake channels plugged-only.", 185, 495.5, size=1.3)
