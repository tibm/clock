"""Blocks: LED — wake COB drivers (2x AO3400A low-side, 12 V plugged-only)
and the DIAL NeoPixel head (2x SK6812 RGBW on-PCB + a 3-pin breakout for
5 more off-board, one RMT data line through a 74AHCT1G125 3V3->5V buffer).
The v0.18 panel string (Cree CLM3C x12 + Q5) was dropped 2026-07-19 with
the display. 2026-07-21: the 5 status pixels (ex-D40..D44) moved off-board
(J12) so they can be placed around the dial by hand-wiring instead of
fighting for PCB-edge space; D40/D41 (ex-D45/D46, renumbered to close the
gap) stay on-board as chain positions 1-2, feeding J12 pin 2 (DATA) for
positions 3-7 in series off-board.
The +12V wire arrives on the x=281.94 column from the rails block.
PWM/data lines come from the MCU as labels (long runs). Per led.md / pv §8.
"""
U = 2.54

PIXELS = [  # (ref, x, caption)  pitch 20.32, row y = 546.10 -- chain pos 1-2
    ("D40", 248.92, "DIAL1"),
    ("D41", 269.24, "DIAL2"),
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

    # ================= DIAL NEOPIXEL HEAD (5 V) =================
    s.frame(180, 512, 395, 600, "DIAL NEOPIXEL HEAD - 2x on-PCB + 5x off-board via J12 (5 V)")
    s.text("D40/D41 = on-PCB dial wash (chain pos 1-2). J12 breaks the chain out", 185, 521, size=1.3)
    s.text("for 5 more pixels wired off-board (status row, chain pos 3-7). One RMT", 185, 525.5, size=1.3)
    s.text("data line; AHCT buffer lifts 3V3 -> 5V (SK6812 VIH = 0.7*VDD = 3.5V).", 185, 530, size=1.3)

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

    # ---- J12: 3-pin breakout for the 5 off-board pixels (chain pos 3-7) ----
    J12 = s.comp("J12", "Connector_Generic:Conn_01x03", 300.99, 546.10,
                value="NeoPixel ext (5V/DATA/GND, pos 3-7)",
                footprint="Connector_JST:JST_PH_B3B-PH-K_1x03_P2.00mm_Vertical",
                refpos=(305.79, 539.75, "left"), valpos=(305.79, 592.0, "left"))
    s.route(leds[-1], "4", J12, "2", "H")             # last on-board DOUT -> DATA
    s.rail(J12, "1", "+5V", rise=2.54)
    s.gnd(J12, "3", drop=2.54)

    # +5V bus (top) + GND bus (bottom) -- spans exactly D40..D41's VDD/VSS
    # stubs (269.24 = D41's x); C237/decoupling tap the rail via s.rail()
    # power-symbol flags, not a wire stub, so the bus doesn't need to reach them.
    s.w((246.38, 535.94), (269.24, 535.94))
    s.power_at(246.38, 533.40, "+5V")
    s.w((246.38, 533.40), (246.38, 535.94))
    s.w((246.38, 556.26), (269.24, 556.26))
    s.w((246.38, 556.26), (246.38, 558.80))
    s.power_at(246.38, 558.80, "GND")

    # per-pixel decoupling (100nF at each on-board pixel's VDD) + bulk
    for i, x in enumerate([254.00, 264.16]):
        c = s.C(f"C23{i}", x, 571.50, "100nF",
                refpos=(x - 1.02, 566.42, "right"), valpos=(x + 1.4, 566.42, "left"))
        s.rail(c, "1", "+5V", rise=0)
        s.gnd(c, "2", drop=0, show_value=False)
    C237 = s.CP("C237", 280.67, 571.50, "100uF",
                refpos=(279.65, 566.42, "right"), valpos=(282.07, 566.42, "left"))
    s.rail(C237, "1", "+5V", rise=0)
    s.gnd(C237, "2", drop=0, show_value=False)
    s.text("Layout: D40/D41 = dial wash either side of the shaft. C230/231 one at", 185, 578.5, size=1.3)
    s.text("each pixel VDD; C237 bulk + R110 + U15 at the chain head. J12 feeds", 185, 583, size=1.3)
    s.text("5 more SK6812 wired off-board in series (chain pos 3-7), same rail.", 185, 587.5, size=1.3)
