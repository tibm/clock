"""Blocks: LED — wake COB drivers (2x AO3400A low-side, 12 V plugged-only)
and the STATUS + DIAL NeoPixel chain (7x SK6812 RGBW on 5 V, one RMT data
line through a 74AHCT1G125 3V3->5V buffer). The v0.18 panel string (Cree
CLM3C x12 + Q5) was dropped 2026-07-19 with the display.
The +12V wire arrives on the x=281.94 column from the rails block.
PWM/data lines come from the MCU as labels (long runs). Per led.md / pv §8.
"""
U = 2.54

PIXELS = [  # (ref, x, caption)  pitch 20.32, row y = 546.10
    ("D40", 248.92, "BELL"),
    ("D41", 269.24, "ALARM"),
    ("D42", 289.56, "CLOCK"),
    ("D43", 309.88, "VOL"),
    ("D44", 330.20, "BATT"),
    ("D45", 350.52, "DIAL1"),
    ("D46", 370.84, "DIAL2"),
]


def build(s):
    # ================= wake COB drivers (12 V) =================
    s.frame(180, 436, 395, 506, "LED - wake COB drivers (12 V, plugged-only)")

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

    s.text("Wake COB strips are self-ballasted (12 V, plugged-only): firmware", 185, 491, size=1.3)
    s.text("gates their PWM off on battery.  ~1 kHz, gamma; LED + audio <= ~12 W.", 185, 495.5, size=1.3)

    # ================= STATUS + DIAL NEOPIXELS (5 V) =================
    s.frame(180, 512, 395, 585, "STATUS + DIAL NEOPIXELS - 7x SK6812 RGBW (5 V)")
    s.text("Pixels 1-5 = status row behind the face holes (red/white semantics),", 185, 521, size=1.3)
    s.text("6-7 = dial wash.  One RMT data line; AHCT buffer lifts 3V3 -> 5 V", 185, 525.5, size=1.3)
    s.text("(SK6812 VIH = 0.7*VDD = 3.5 V).  <=0.6 A worst-case all-white.", 185, 530, size=1.3)

    # ---- 3V3 -> 5V data buffer (SN74AHCT1G125, SOT-23-5) ----
    U15 = s.comp("U15", "clock:74AHCT1G125", 218.44, 546.10,
                 value="SN74AHCT1G125",
                 footprint="Package_TO_SOT_SMD:SOT-23-5",
                 refpos=(210.82, 537.97, "left"), valpos=(196.85, 552.45, "left"))
    s.pw(U15, "2", ("x", 195.58))                    # A <- MCU (3V3)
    s.glabel_at("NEOPIX_DATA", 195.58, 546.10, 180)
    s.rail(U15, "5", "+5V", rise=2.54)               # VCC = 5 V
    s.gnd(U15, "3", drop=2.54)
    # /OE tied low (always enabled): inverted GND flag right at the pin
    s.pw(U15, "1", ("dy", -2.54))
    s.power_at(218.44, 533.40, "GND", rot=180)
    C238 = s.C("C238", 196.85, 533.40, "100nF")
    s.rail(C238, "1", "+5V", rise=0)
    s.gnd(C238, "2", drop=0, show_value=False)
    # series R into the first DIN (edge damping)
    R110 = s.R("R110", 237.49, 546.10, "330R", rot=90,
               refpos=(234.95, 541.02, None), valpos=(237.49, 550.55, None))
    s.route(U15, "4", R110, "1", "H")                # Y -> R110

    # ---- the chain: DIN <- prev DOUT, VDD/VSS on shared bus rows ----
    leds = []
    for ref, x, cap in PIXELS:
        d = s.comp(ref, "LED:SK6812", x, 546.10, value="SK6812RGBW",
                   footprint="LED_SMD:LED_SK6812_PLCC4_5.0x5.0mm_P3.2mm",
                   refpos=(x - 4.57, 539.75, "left"), valpos=(x - 4.57, 542.29, "left"),
                   show_value=False)
        s.pw(d, "3", ("y", 535.94))                  # VDD stub -> +5V bus
        s.pw(d, "1", ("y", 556.26))                  # VSS stub -> GND bus
        s.text(cap, x - 2.54, 560.07, size=1.3)
        leds.append(d)
    s.route(R110, "2", leds[0], "2", "H")            # into DIN of pixel 1
    for a, b in zip(leds, leds[1:]):
        s.route(a, "4", b, "2", "H")                 # DOUT -> next DIN
    s.nc(leds[-1], "4")                              # last DOUT open

    # +5V bus (top) + GND bus (bottom)
    s.w((246.38, 535.94), (370.84, 535.94))
    s.power_at(246.38, 533.40, "+5V")
    s.w((246.38, 533.40), (246.38, 535.94))
    s.w((246.38, 556.26), (370.84, 556.26))
    s.w((246.38, 556.26), (246.38, 558.80))
    s.power_at(246.38, 558.80, "GND")

    # per-pixel decoupling (layout: one 100nF at every pixel VDD) + bulk
    for i, x in enumerate([254.00, 264.16, 274.32, 284.48, 294.64, 304.80, 314.96]):
        c = s.C(f"C23{i}", x, 571.50, "100nF",
                refpos=(x - 1.02, 566.42, "right"), valpos=(x + 1.4, 566.42, "left"))
        s.rail(c, "1", "+5V", rise=0)
        s.gnd(c, "2", drop=0, show_value=False)
    C237 = s.CP("C237", 327.66, 571.50, "100uF",
                refpos=(326.64, 566.42, "right"), valpos=(329.06, 566.42, "left"))
    s.rail(C237, "1", "+5V", rise=0)
    s.gnd(C237, "2", drop=0, show_value=False)
    s.text("Layout: status row pitch = face-plate hole pitch; C230-236 one at each", 185, 578.5, size=1.3)
    s.text("pixel VDD; C237 bulk + R110 + U15 at the chain head.", 185, 583, size=1.3)
