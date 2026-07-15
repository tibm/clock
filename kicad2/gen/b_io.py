"""Blocks: SENSORS & HOMING (sensor-board connector + QRE1113) and
IO EXPANDER + UI (MCP23017, buttons, rotary encoder, I2C pull-ups).
Encoder A/B and the expander INT are WIRED to the MCU down the x~425 corridor;
slow expander fan-out lines use labels (per the block-diagram style).
"""
U = 2.54


def build(s):
    U8 = s.parts["U8"]

    # ================= SENSORS & HOMING =================
    s.frame(405, 225, 595, 305, "SENSORS (I2C off-board) + HAND-HOMING (QRE1113)")

    # sensor-board connector (STEMMA-QT chain: BME688 + TSL2591 + LIS3DH)
    J7 = s.comp("J7", "Connector_Generic:Conn_01x05", 533.40, 254.00,
                value="Sensor board (BME688+TSL2591+LIS3DH)",
                footprint="Connector_JST:JST_SH_SM05B-SRSS-TB_1x05-1MP_P1.00mm_Horizontal")
    s.pw(J7, "1", ("x", 505.46), ("dy", 2.54))
    s.power_at(505.46, 251.46, "GND")
    s.pw(J7, "2", ("x", 510.54), ("y", 246.38))
    s.power_at(510.54, 246.38, "+3V3")
    s.glabel(J7, "3", "I2C_SDA")
    s.glabel(J7, "4", "I2C_SCL")
    s.glabel(J7, "5", "SENSOR_INT")
    # SENSOR_INT pull-up (INT lines are open-drain)
    R97 = s.R("R97", 541.02, 254.00, "10k")
    s.rail(R97, "1", "+3V3", rise=0)
    s.pw(R97, "2", ("dy", 2.54))
    s.glabel_at("SENSOR_INT", 541.02, 260.35, 270)

    # QRE1113 reflective homing -> HOME_OPTO (MCU ADC IO2)
    U14 = s.comp("U14", "Sensor_Proximity:QRE1113", 443.23, 256.54,
                 value="QRE1113", footprint="")
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
    s.text("QRE1113 faces reflective tabs on the hand hubs (no magnets).", 410, 297, size=1.3)
    s.text("Sensor breakouts carry their own I2C pull-ups.", 410, 301.5, size=1.3)

    # ================= IO EXPANDER + UI =================
    s.frame(405, 415, 595, 585, "IO EXPANDER (MCP23017 @0x20) + ENCODER + BUTTONS")

    U13 = s.comp("U13", "clock:MCP23017x-x-SS", 497.84, 469.90, value="MCP23017",
                 footprint="Package_SO:SSOP-28_5.3x10.2mm_P0.65mm")
    s.parts["U13"] = U13
    s.rail(U13, "9", "+3V3", rise=2.54)               # VDD
    s.gnd(U13, "10", drop=2.54)                       # VSS
    C240 = s.C("C240", 505.46, 441.96, "100nF")
    s.rail(C240, "1", "+3V3", rise=0)
    s.gnd(C240, "2", drop=0)
    s.nc(U13, "11")
    s.nc(U13, "14")
    s.nc(U13, "28")                                    # GPA7 spare
    s.nc(U13, "8")                                     # GPB7 spare

    # I2C bus stubs + main-board pull-ups
    s.pw(U13, "12", ("x", 464.82))
    s.glabel_at("I2C_SCL", 464.82, 449.58, 180)
    s.pw(U13, "13", ("x", 462.28))
    s.glabel_at("I2C_SDA", 462.28, 452.12, 180)
    R96 = s.R("R96", 476.25, 443.23, "4.7k")
    s.rail(R96, "1", "+3V3", rise=0)
    s.pw(R96, "2", ("y", 449.58))
    R95 = s.R("R95", 467.36, 443.23, "4.7k")
    s.rail(R95, "1", "+3V3", rise=0)
    s.pw(R95, "2", ("y", 452.12))

    # INTA+INTB mirrored -> one line to the MCU (IO44), + pull-up
    s.pw(U13, "20", ("x", 473.71))                    # INTA row
    s.w((478.79, 467.36), (478.79, 147.32))           # corridor up to the MCU
    s.junction(478.79, 464.82)                        # INTA row -> corridor
    s.pw(U8, "36", ("x", 478.79))                     # IO44 EXP_INT
    s.pw(U13, "19", ("x", 478.79))                    # INTB joins the column
    R94 = s.R("R94", 473.71, 461.01, "10k")
    s.rail(R94, "1", "+3V3", rise=0)
    # RESET tied high
    R93 = s.R("R93", 469.90, 468.63, "10k")
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
    # buttons on GPA3..GPA5 (internal pull-ups + IOC in firmware),
    # fanned out to a 5.08 pitch
    gcol = 543.56
    for pin, ref, name, row, bend in [("24", "SW4", "BTN1", 454.66, 518.16),
                                      ("25", "SW5", "BTN2", 459.74, None),
                                      ("26", "SW6", "BTN3", 464.82, 520.70)]:
        sw = s.comp(ref, "Switch:SW_Push", 535.94, row, value=name,
                    footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
        if bend is None:
            s.route(U13, pin, sw, "1", "H")
        else:
            s.pw(U13, pin, ("x", bend), ("y", row), ("px", sw, "1"))
        s.pw(sw, "2", ("x", gcol))
    s.w((gcol, 454.66), (gcol, 464.82))
    s.power_at(gcol, 464.82, "GND")

    # GPB fan-out labels
    for pin, name in [("1", "PD_PG"), ("2", "CHRG"), ("3", "FAULT"),
                      ("4", "LCD_DISP"), ("5", "FULLCHG_EN"),
                      ("6", "VBAT_DIV_EN"), ("7", "SPK_FAULT")]:
        s.glabel(U13, pin, name)
    # SPK_FAULT open-drain pull-up (amp fault line)
    R62 = s.R("R62", 513.08, 491.49, "10k")
    s.power_at(513.08, 495.30, "+3V3", rot=180)

    # rotary encoder: A/B wired to the MCU (PCNT), switch on GPA6
    SW3 = s.comp("SW3", "Device:RotaryEncoder_Switch", 533.40, 508.00,
                 value="EC11 encoder",
                 footprint="Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm")
    s.pw(U8, "24", ("x", 424.18), ("y", 505.46), ("px", SW3, "A"))   # ENC_A
    s.pw(U8, "25", ("x", 426.72), ("y", 510.54), ("px", SW3, "B"))   # ENC_B
    R100 = s.R("R100", 452.12, 501.65, "10k")
    s.rail(R100, "1", "+3V3", rise=0)
    R101 = s.R("R101", 459.74, 499.11, "10k")
    s.rail(R101, "1", "+3V3", rise=0)
    s.pw(R101, "2", ("y", 510.54))
    s.gnd(SW3, "C", via=-2.54, drop=5.08)
    s.gnd(SW3, "S2", via=2.54)
    # ENC_SW -> GPA6
    s.pw(U13, "27", ("x", 516.89), ("y", 469.90), ("x", 548.64), ("y", 505.46),
         ("px", SW3, "S1"))

    s.text("INT: any GPA/GPB change -> IO44 (IOCON.MIRROR=1).  Buttons/encoder-switch", 410, 575, size=1.3)
    s.text("use MCP internal pull-ups.  Encoder A/B -> hardware PCNT (IO47/48).", 410, 579.5, size=1.3)
