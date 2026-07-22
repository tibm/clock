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
    # Whole sub-block nudged up 20.32mm from the original auto-layout
    # (hand-tuned in eeschema 2026-07-21 for readability; re-harvested via
    # cosmetics.py -- positions below match, wires updated to follow).
    s.frame(180, 415.68, 395, 485.68, "LED - wake COB drivers (12 V, plugged-only)")

    # ---- wake COB connector (+12V, WARM_K, COOL_K) ----
    J9 = s.comp("J9", "Connector_Generic:Conn_01x03", 340.36, 429.26,
                value="Wake COB 12V (warm+cool)",
                footprint="Connector_JST:JST_PH_B3B-PH-K_1x03_P2.00mm_Vertical")
    s.w((281.94, 393.70), (281.94, 426.72), (335.28, 426.72))   # +12V run
    Q6 = s.comp("Q6", "clock:AO3400A", 313.69, 443.23, value="AO3400A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(318.52, 441.83, "left"), valpos=(326.39, 447.04, "left"))
    s.pw(Q6, "3", ("y", 429.26), ("px", J9, "2"))    # drain -> WARM_K
    s.gnd(Q6, "2", drop=0)
    R104 = s.R("R104", 304.80, 443.23, "100R", rot=90)
    s.route(R104, "2", Q6, "1", "H")
    s.pw(R104, "1", ("x", 298.45))
    s.glabel_at("WAKE_WARM_PWM", 298.45, 443.23, 180)
    R105 = s.R("R105", 308.61, 450.85, "10k")
    s.pw(R105, "1", ("y", 443.23))
    s.gnd(R105, "2", drop=0)

    Q7 = s.comp("Q7", "clock:AO3400A", 321.31, 466.09, value="AO3400A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(326.14, 464.69, "left"), valpos=(329.18, 469.9, "left"))
    s.pw(Q7, "3", ("y", 452.12), ("x", 331.47), ("y", 431.8),
         ("px", J9, "3"))                            # drain -> COOL_K
    s.gnd(Q7, "2", drop=0)
    R106 = s.R("R106", 312.42, 466.09, "100R", rot=90)
    s.route(R106, "2", Q7, "1", "H")
    s.pw(R106, "1", ("x", 303.53))
    s.glabel_at("WAKE_COOL_PWM", 303.53, 466.09, 180)
    R107 = s.R("R107", 316.23, 473.71, "10k")
    s.pw(R107, "1", ("y", 466.09))
    s.gnd(R107, "2", drop=0)
    # IO46 boot-strap pulldown (strap must be low at boot) on the PWM line
    R52 = s.R("R52", 306.07, 469.9, "10k",
              refpos=(305.03, 468.5, "right"), valpos=(305.03, 471.3, "right"))
    s.pw(R52, "1", ("y", 466.09))
    s.gnd(R52, "2", drop=0)

    s.text("Wake COB strips are self-ballasted (12 V, plugged-only): firmware", 185, 470.68, size=1.3)
    s.text("gates their PWM off on battery.  ~1 kHz, gamma; LED + audio <= ~12 W.", 185, 475.18, size=1.3)

    # ================= DIAL NEOPIXEL HEAD (5 V) =================
    # Whole sub-block nudged (+57.15, -21.59) from the original auto-layout
    # (hand-tuned in eeschema 2026-07-21 for readability; re-harvested via
    # cosmetics.py). The decoupling-cap row (C230/C231/C237) moved
    # independently of that uniform shift, so its wiring is derived from
    # its own harvested position rather than the same delta.
    s.frame(180, 490.41, 395, 578.41, "DIAL NEOPIXEL HEAD - 2x on-PCB + 5x off-board via J12 (5 V)")
    s.text("D40/D41 = on-PCB dial wash (chain pos 1-2). J12 breaks the chain out", 185, 499.41, size=1.3)
    s.text("for 5 more pixels wired off-board (status row, chain pos 3-7). One RMT", 185, 503.91, size=1.3)
    s.text("data line; AHCT buffer lifts 3V3 -> 5V (SK6812 VIH = 0.7*VDD = 3.5V).", 185, 508.41, size=1.3)

    # ---- 3V3 -> 5V data buffer (SN74AHCT1G125, SOT-23-5) ----
    U15 = s.comp("U15", "clock:74AHCT1G125", 275.59, 524.51,
                 value="SN74AHCT1G125",
                 footprint="Package_TO_SOT_SMD:SOT-23-5",
                 refpos=(267.97, 516.38, "left"), valpos=(254.00, 530.86, "left"))
    s.pw(U15, "2", ("x", 195.58))                    # A <- MCU (3V3)
    s.glabel_at("NEOPIX_DATA", 195.58, 524.51, 180)
    s.rail(U15, "5", "+5V", rise=2.54)               # VCC = 5 V
    s.gnd(U15, "3", drop=2.54)
    # /OE tied low (always enabled): inverted GND flag right at the pin
    s.pw(U15, "1", ("dy", -2.54))
    s.power_at(275.59, 511.81, "GND", rot=180)
    C238 = s.C("C238", 219.71, 529.59, "100nF")
    s.rail(C238, "1", "+5V", rise=0)
    s.gnd(C238, "2", drop=0, show_value=False)
    # series R into the first DIN (edge damping)
    R110 = s.R("R110", 294.64, 524.51, "330R", rot=90,
               refpos=(292.10, 519.43, None), valpos=(294.64, 528.96, None))
    s.route(U15, "4", R110, "1", "H")                # Y -> R110

    # ---- the chain: DIN <- prev DOUT, VDD/VSS on shared bus rows ----
    leds = []
    for ref, x, cap in PIXELS:
        d = s.comp(ref, "LED:SK6812", x, 524.51, value="SK6812RGBW",
                   footprint="LED_SMD:LED_SK6812_PLCC4_5.0x5.0mm_P3.2mm",
                   refpos=(x - 4.57, 518.16, "left"), valpos=(x - 4.57, 520.70, "left"),
                   show_value=False)
        s.pw(d, "3", ("y", 514.35))                  # VDD stub -> +5V bus
        s.pw(d, "1", ("y", 534.67))                  # VSS stub -> GND bus
        s.text(cap, x - 2.54, 538.48, size=1.3)
        leds.append(d)
    s.route(R110, "2", leds[0], "2", "H")            # into DIN of pixel 1
    for a, b in zip(leds, leds[1:]):
        s.route(a, "4", b, "2", "H")                 # DOUT -> next DIN

    # ---- J12: 3-pin breakout for the 5 off-board pixels (chain pos 3-7) ----
    J12 = s.comp("J12", "Connector_Generic:Conn_01x03", 358.14, 524.51,
                value="NeoPixel ext (5V/DATA/GND, pos 3-7)",
                footprint="Connector_JST:JST_PH_B3B-PH-K_1x03_P2.00mm_Vertical",
                refpos=(362.94, 518.16, "left"), valpos=(362.94, 570.61, "left"))
    s.route(leds[-1], "4", J12, "2", "H")             # last on-board DOUT -> DATA
    s.rail(J12, "1", "+5V", rise=2.54)
    s.gnd(J12, "3", drop=2.54)

    # +5V bus (top) + GND bus (bottom) -- spans exactly D40..D41's VDD/VSS
    # stubs (326.39 = D41's x); C237/decoupling tap the rail via s.rail()
    # power-symbol flags, not a wire stub, so the bus doesn't need to reach them.
    s.w((303.53, 514.35), (326.39, 514.35))
    s.power_at(303.53, 511.81, "+5V")
    s.w((303.53, 511.81), (303.53, 514.35))
    s.w((303.53, 534.67), (326.39, 534.67))
    s.w((303.53, 534.67), (303.53, 537.21))
    s.power_at(303.53, 537.21, "GND")

    # per-pixel decoupling (100nF at each on-board pixel's VDD) + bulk --
    # positions hand-tuned in eeschema, no longer a clean offset from the
    # pixel row (see note above); hardcoded to match the harvested layout.
    for i, x in enumerate([303.53, 316.23]):
        c = s.C(f"C23{i}", x, 557.53, "100nF",
                refpos=(x - 1.02, 552.45, "right"), valpos=(x + 1.4, 552.45, "left"))
        s.rail(c, "1", "+5V", rise=0)
        s.gnd(c, "2", drop=0, show_value=False)
    C237 = s.CP("C237", 330.2, 557.53, "100uF",
                refpos=(329.18, 552.45, "right"), valpos=(331.6, 552.45, "left"))
    s.rail(C237, "1", "+5V", rise=0)
    s.gnd(C237, "2", drop=0, show_value=False)
    s.text("Layout: D40/D41 = dial wash either side of the shaft. C230/231 one at", 185, 565.01, size=1.3)
    s.text("each pixel VDD; C237 bulk + R110 + U15 at the chain head. J12 feeds", 185, 569.51, size=1.3)
    s.text("5 more SK6812 wired off-board in series (chain pos 3-7), same rail.", 185, 574.01, size=1.3)
