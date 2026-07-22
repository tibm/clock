"""Blocks: SENSORS & HOMING (sensor-board connector + QRE1113) and
IO EXPANDER + KNOB (MCP23017, EM14 optical encoder connector, I2C pull-ups).
Knob A/B (5 V outputs -> 100k/200k dividers) and the expander INT are WIRED
to the MCU down the x~425 corridor; ENC_SW + slow expander fan-out lines use
labels (per the block-diagram style). BTN1-3 + the EC11 dropped 2026-07-19.
"""
U = 2.54


def build(s):
    U8 = s.parts["U8"]

    # ================= SENSORS & HOMING =================
    s.frame(405, 225, 595, 305, "SENSORS (I2C off-board) + HAND-HOMING (QRE1113)")

    # sensor-board connector (STEMMA-QT chain: BME688 + TSL2591 + LIS3DH).
    # JST ZH 1x06 (B6B-ZR, TH top-entry) so the cheap pre-crimped
    # A06ZR06ZR28H102B ZH<->ZH cable plugs straight in; pin 6 spare.
    J7 = s.comp("J7", "Connector_Generic:Conn_01x06", 533.40, 254.00,
                value="Sensor board (BME688+TSL2591+LIS3DH)",
                footprint="Connector_JST:JST_ZH_B6B-ZR_1x06_P1.50mm_Vertical",
                refpos=(533.40, 265.43, None), valpos=(535.94, 241.30, None))
    s.pw(J7, "1", ("x", 505.46), ("dy", 2.54))
    s.power_at(505.46, 251.46, "GND")
    s.pw(J7, "2", ("x", 510.54), ("y", 246.38))
    s.power_at(510.54, 246.38, "+3V3")
    s.glabel(J7, "3", "I2C_SDA")
    s.glabel(J7, "4", "I2C_SCL")
    s.glabel(J7, "5", "SENSOR_INT")
    s.nc(J7, "6")                                     # spare wire in the cable
    # SENSOR_INT pull-up (INT lines are open-drain)
    R97 = s.R("R97", 541.02, 254.00, "10k")
    s.rail(R97, "1", "+3V3", rise=0)
    s.pw(R97, "2", ("dy", 2.54))
    s.glabel_at("SENSOR_INT", 541.02, 260.35, 270)

    # QRE1113GR (SMD) reflective homing -> HOME_OPTO (MCU ADC IO2)
    U14 = s.comp("U14", "Sensor_Proximity:QRE1113", 443.23, 256.54,
                 value="QRE1113GR", footprint="clock:OnSemi_QRE1113GR")
    R98 = s.R("R98", 430.53, 247.65, "150R")
    s.rail(R98, "1", "+3V3", rise=0)
    s.pw(R98, "2", ("y", 254.00), ("px", U14, "1"))   # LED anode
    s.gnd(U14, "2", via=-2.54)                        # LED cathode
    R99 = s.R("R99", 455.93, 247.65, "10k")
    s.rail(R99, "1", "+3V3", rise=0)
    s.pw(R99, "2", ("y", 254.00))
    s.pw(U14, "3", ("x", 466.09))                     # collector node
    C241 = s.C("C241", 461.01, 257.81, "100nF")
    s.gnd(C241, "2", drop=0)
    s.glabel_at("HOME_OPTO", 466.09, 254.00, 0)
    s.gnd(U14, "4", via=2.54)                         # emitter
    s.text("QRE1113GR faces reflective tabs on the hand hubs (no magnets).", 486.41, 297, size=1.3)
    s.text("Sensor breakouts carry their own I2C pull-ups.", 486.41, 301.5, size=1.3)

    # ================= IO EXPANDER + KNOB =================
    s.frame(405, 415, 595, 585, "IO EXPANDER (MCP23017 @0x20) + KNOB (EM14)")

    U13 = s.comp("U13", "clock:MCP23017x-x-SS", 497.84, 469.90, value="MCP23017",
                 footprint="Package_SO:SSOP-28_5.3x10.2mm_P0.65mm")
    s.parts["U13"] = U13
    s.rail(U13, "9", "+3V3", rise=2.54)               # VDD
    s.gnd(U13, "10", drop=2.54)                       # VSS
    C240 = s.C("C240", 505.46, 434.34, "100nF")
    s.rail(C240, "1", "+3V3", rise=0)
    s.gnd(C240, "2", drop=1.27)
    s.nc(U13, "11")
    s.nc(U13, "14")
    s.nc(U13, "28")                                    # GPA7 spare
    s.nc(U13, "8")                                     # GPB7 spare

    # I2C bus stubs + main-board pull-ups
    s.pw(U13, "12", ("x", 450.85))
    s.glabel_at("I2C_SCL", 450.85, 449.58, 180)
    s.pw(U13, "13", ("x", 440.69))
    s.glabel_at("I2C_SDA", 440.69, 452.12, 180)
    R96 = s.R("R96", 469.90, 443.23, "4.7k")
    s.rail(R96, "1", "+3V3", rise=0)
    s.pw(R96, "2", ("y", 449.58))
    R95 = s.R("R95", 458.47, 443.23, "4.7k")
    s.rail(R95, "1", "+3V3", rise=0)
    s.pw(R95, "2", ("y", 452.12))

    # INTA+INTB mirrored -> one line to the MCU (IO44), + pull-up
    R94 = s.R("R94", 468.63, 461.01, "10k")
    s.rail(R94, "1", "+3V3", rise=0)
    s.pw(U13, "20", ("px", R94, "2"))                 # INTA row
    s.w((478.79, 467.36), (478.79, 147.32))           # corridor up to the MCU
    s.junction(478.79, 464.82)                        # INTA row -> corridor
    s.pw(U8, "36", ("x", 478.79))                     # IO44 EXP_INT
    s.pw(U13, "19", ("x", 478.79))                    # INTB joins the column
    # RESET tied high
    R93 = s.R("R93", 462.28, 468.63, "10k")
    s.pw(U13, "18", ("px", R93, "2"))
    s.rail(R93, "1", "+3V3", rise=0)
    # address straps 0x20: A0/A1/A2 -> 0R -> GND
    for pin, ref, tx in [("15", "R90", 481.33), ("16", "R91", 473.71),
                         ("17", "R92", 466.09)]:
        r = s.R(ref, tx, 495.30, "0R")
        s.pw(U13, pin, ("x", tx), ("pin", r, "1"))
        s.gnd(r, "2", drop=0)

    # GPA fan-out: labels for far nets
    s.glabel(U13, "21", "SPK_SD")
    s.glabel(U13, "22", "STEP_STBY")
    s.glabel(U13, "23", "BOOST12_EN")

    # GPA3..GPA6 freed 2026-07-19 (BTN1-3 dropped, ENC_SW moved to the host
    # IO17). GPA3 -> J11: rear radio-disable toggle (MCP internal PU + IOC;
    # slow/static, so the expander is fine). GPA4..6 spare.
    J11 = s.comp("J11", "Connector_Generic:Conn_01x02", 546.10, 457.20,
                 value="Radio-off toggle (rear)",
                 footprint="Connector_JST:JST_ZH_B2B-ZR_1x02_P1.50mm_Vertical",
                 refpos=(546.10, 450.85, None), valpos=(551.18, 464.82, None))
    s.pw(U13, "24", ("px", J11, "1"))                  # GPA3 = RADIO_OFF
    s.pw(J11, "2", ("x", 535.94), ("dy", 2.54))
    s.power_at(535.94, 462.28, "GND")
    for p in ("25", "26", "27"):
        s.nc(U13, p)

    # GPB fan-out labels (GPB3 freed 2026-07-19: LCD_DISP gone with the display)
    for pin, name in [("1", "PD_PG"), ("2", "CHRG"), ("3", "FAULT"),
                      ("5", "FULLCHG_EN"), ("6", "VBAT_DIV_EN")]:
        s.glabel(U13, pin, name)
    s.nc(U13, "4")                                     # GPB3 spare
    # SPK_FAULT (GPB6): run right, open-drain pull-up R62 hangs on it
    s.pw(U13, "7", ("x", 535.94), ("dx", 2.54))
    s.glabel_at("SPK_FAULT", 538.48, 487.68, 0)
    R62 = s.R("R62", 535.94, 480.06, "10k")
    s.pw(R62, "2", ("y", 487.68))
    s.pw(R62, "1", ("dy", -1.27))
    s.power_at(535.94, 474.98, "+3V3", show_value=False)
    s.text("+3V3", 536.702, 472.694, size=1.27)
    # SPK_SD boot-state pulldown (TAS5760M 9.2.1.2.1: SD must be LOW while
    # supplies ramp; GPA0 is hi-Z at POR). Lives here with R62, joined by
    # name like the rest of the slow amp lines (added 2026-07-21).
    R63 = s.R("R63", 556.26, 481.33, "100k")
    s.glabel_at("SPK_SD", 556.26, 477.52, 180)   # directly on pin 1
    s.gnd(R63, "2", drop=0)

    # ---- knob connector: Bourns EM14A0D-C24-L064S optical encoder (5 V,
    # 64 CPR, no detent) + push switch, off-board on the cube's top face.
    # JST ZH 1x06 (B6B-ZR) -> same pre-crimped A06ZR06ZR28H102B cable as
    # J7. Pinout: 1 GND, 2 +5V, 3 A, 4 B, 5 SW, 6 GND.
    J10 = s.comp("J10", "Connector_Generic:Conn_01x06", 560.07, 530.86,
                 value="Knob: EM14 enc+SW (top face)",
                 footprint="Connector_JST:JST_ZH_B6B-ZR_1x06_P1.50mm_Vertical",
                 refpos=(560.07, 520.70, None), valpos=(560.07, 544.83, None))
    # GND pins 1+6 tied on a left lane (rows 525.78 / 538.48)
    s.pw(J10, "1", ("x", 537.21))
    s.pw(J10, "6", ("x", 537.21))
    s.w((537.21, 525.78), (537.21, 542.29))
    s.power_at(537.21, 542.29, "GND")
    # +5V (the EM14 opto-ASIC runs on 5 V, ~30 mA)
    s.pw(J10, "2", ("x", 546.10), ("dy", -5.08))
    s.power_at(546.10, 523.24, "+5V")
    # A/B: 5 V outputs -> 100k/200k dividers (~3.2 V) -> PCNT (IO47/48),
    # wired down the x=424/427 corridor to the MCU
    R111 = s.R("R111", 528.32, 530.86, "100k", rot=90,
               refpos=(526.03, 527.05, None), valpos=(532.13, 527.05, None))
    s.pw(J10, "3", ("px", R111, "2"))
    s.pw(R111, "1", ("x", 424.18))
    s.pw(U8, "24", ("x", 424.18), ("y", 530.86))                   # ENC_A
    R112 = s.R("R112", 518.16, 533.40, "100k", rot=90,
               refpos=(514.35, 529.72, None), valpos=(520.70, 529.72, None))
    s.pw(J10, "4", ("px", R112, "2"))
    s.pw(R112, "1", ("x", 426.72))
    s.pw(U8, "25", ("x", 426.72), ("y", 533.40))                   # ENC_B
    R114 = s.R("R114", 523.24, 535.94, "200k")
    s.pw(R114, "1", ("y", 530.86))
    s.gnd(R114, "2", drop=0)
    R115 = s.R("R115", 444.50, 538.48, "200k")
    s.pw(R115, "1", ("y", 533.40))
    s.gnd(R115, "2", drop=0)
    # SW: dry contact to GND -> 10k pull-up + 100n debounce -> IO17 (IRQ)
    # R113 hand-moved 2026-07-21 (was (533.40,541.02)); route/tap points
    # below are the real post-move pin coordinates (via Comp.pin_xy), not
    # re-derived offsets -- R113 and C239 no longer share a Y, so they tap
    # the J10-R113 wire at two different points instead of one shared row.
    R113 = s.R("R113", 509.27, 546.10, "10k")
    s.rail(R113, "1", "+3V3", rise=0)
    # Manual HVH via a neutral x=548 -- not route()'s VH/HV, and not the
    # obvious x=537.21 (C239's own column): J10's pins are a single column
    # at x=554.99, R113's pin 1 (+3V3) sits directly above pin 2 in
    # R113's own column (x=509.27), and x=537.21 is J10's GND bus for
    # y<=542.29 -- a vertical leg down any of those three x's sweeps
    # straight through another net (found via ERC, twice: shorted ENC_SW
    # onto GND via J10 pin 6, then onto +3V3 via R113's own pin 1). x=548
    # clears all three; the final horizontal leg still crosses x=537.21,
    # but at y=549.91, below the GND bus's y<=542.29 span, so it just
    # T-taps C239 without touching the bus.
    j10_5 = s.pxy(J10, "5")
    r113_2 = s.pxy(R113, "2")
    s.w(j10_5, (548.64, j10_5[1]), (548.64, r113_2[1]), r113_2)
    # label on a short stub left of R113 pin 2 so it clears the pin-2
    # junction (hand-moved in eeschema 2026-07-21)
    s.w(r113_2, (501.65, r113_2[1]))
    s.glabel_at("ENC_SW", 501.65, r113_2[1], 180)
    C239 = s.C("C239", 537.21, 556.26, "100nF")   # pin 1 taps the row
    s.pw(C239, "1", ("y", r113_2[1]))
    s.gnd(C239, "2", drop=0)

    s.text("INT: any GPA/GPB change -> IO44 (IOCON.MIRROR=1).  Knob = EM14A0D-C24-L064S", 410, 571, size=1.3)
    s.text("(optical, 64 CPR, no detent, push) on the top face.  J7/J10 = JST ZH 1x06, J11 = ZH", 410, 575.5, size=1.3)
    s.text("1x02 (pre-crimped AxxZR cables).  A/B 5 V -> 100k/200k div -> PCNT; SW -> IO17 IRQ.", 410, 580, size=1.3)
