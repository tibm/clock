"""Block: MCU — ESP32-S3-WROOM-1-N16R8 (custom functionally-grouped symbol).
Straps (EN/IO0 + buttons + PROG header), XTAL32K, decoupling, and labels for
bus/far nets. SPI/I2S/motor/encoder/INT wires are drawn by their peripheral
blocks via s.parts["U8"].
"""
U = 2.54


def build(s):
    s.frame(405, 40, 595, 215, "MCU — ESP32-S3-WROOM-1-N16R8 (Wi-Fi + BLE, native USB-JTAG)")

    U8 = s.comp("U8", "clock:ESP32S3_CLOCK", 525.78, 139.70,
                value="ESP32-S3-WROOM-1-N16R8",
                footprint="RF_Module:ESP32-S3-WROOM-1",
                refpos=(505.46, 97.79, "left"), valpos=(505.46, 190.50, "left"))
    s.parts["U8"] = U8

    # ---- 3V3 + decoupling (10u + 22u + 100n x2) ----
    s.w((445.77, 92.71), (525.78, 92.71), (525.78, 100.33))   # to the 3V3 pin
    s.power_at(455.93, 87.63, "+3V3")
    s.w((455.93, 87.63), (455.93, 92.71))
    for ref, x, v, fp in [("C140", 445.77, "10uF", "C0805"),
                          ("C141", 453.39, "22uF", "C0805"),
                          ("C142", 461.01, "100nF", "C0603"),
                          ("C143", 468.63, "100nF", "C0603")]:
        c = s.C(ref, x, 96.52, v, fp=fp)
        s.gnd(c, "2", drop=0)
    # ---- GND pins ----
    s.w((523.24, 179.07), (528.32, 179.07))
    s.w((525.78, 179.07), (525.78, 181.61))
    s.power_at(525.78, 181.61, "GND")

    # ---- EN strap: 10k PU + 1uF + RESET button + PROG header ----
    s.pw(U8, "3", ("x", 438.15))                     # EN row wire
    R50 = s.R("R50", 467.36, 102.87, "10k")
    s.rail(R50, "1", "+3V3", rise=0)
    s.jpin(R50, "2")
    C144 = s.C("C144", 478.79, 110.49, "1uF")
    s.pw(C144, "1", ("y", 106.68))
    s.gnd(C144, "2", drop=0)
    SW1 = s.comp("SW1", "Switch:SW_Push", 461.01, 114.30, value="RESET",
                 footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
    s.pw(SW1, "2", ("y", 106.68))
    s.gnd(SW1, "1", via=-2.54)
    # ---- IO0 strap: 10k PU + BOOT button (PU left of the EN row) ----
    s.pw(U8, "27", ("x", 435.61))                    # IO0 row wire
    R51 = s.R("R51", 435.61, 105.41, "10k")
    s.rail(R51, "1", "+3V3", rise=0)
    SW2 = s.comp("SW2", "Switch:SW_Push", 463.55, 119.38, value="BOOT",
                 footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
    s.pw(SW2, "2", ("x", 468.63), ("y", 109.22))
    s.gnd(SW2, "1", via=0)
    # PROG header: 3V3 / GND / EN / IO0
    J2 = s.comp("J2", "Connector_Generic:Conn_01x04", 429.26, 116.84,
                mirror="y", value="PROG",
                footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical")
    s.rail(J2, "1", "+3V3", rise=2.54)
    s.gnd(J2, "2", via=2.54, drop=7.62)
    s.pw(J2, "3", ("x", 441.96), ("y", 106.68))      # EN
    s.pw(J2, "4", ("x", 444.50), ("y", 109.22))      # IO0

    # ---- 32.768 kHz crystal ----
    Y1 = s.comp("Y1", "Device:Crystal", 490.22, 123.19, rot=90, value="32.768kHz",
                footprint="Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm",
                refpos=(490.22, 114.30, None), valpos=(490.22, 116.84, None))
    s.pw(U8, "8", ("x", 494.03), ("y", 119.38), ("px", Y1, "2"))
    s.pw(U8, "9", ("x", 494.03), ("y", 127.00), ("px", Y1, "1"))
    C145 = s.C("C145", 481.33, 119.38, "18pF", rot=90)
    s.route(C145, "2", Y1, "2", "H")
    s.gnd(C145, "1")
    C146 = s.C("C146", 481.33, 127.00, "18pF", rot=90)
    s.route(C146, "2", Y1, "1", "H")
    s.gnd(C146, "1")

    # ---- left-side nets (labels) ----
    s.glabel(U8, "13", "USB_DM")
    s.glabel(U8, "14", "USB_DP")
    s.glabel(U8, "39", "VBAT_SENSE")
    s.glabel(U8, "38", "HOME_OPTO")
    s.glabel(U8, "35", "SENSOR_INT")
    for p in ("28", "29", "30"):                     # PSRAM-reserved pads
        s.nc(U8, p)

    # ---- right-side nets (labels; buses are wired by peripheral blocks) ----
    s.glabel(U8, "12", "I2C_SDA")
    s.glabel(U8, "17", "I2C_SCL")
    s.glabel(U8, "7", "PANEL_PWM")
    s.glabel(U8, "26", "WAKE_WARM_PWM")
    s.glabel(U8, "16", "WAKE_COOL_PWM")

    s.text("IO35-37 reserved by the octal PSRAM (N16R8).  Straps: IO0 high, IO46 low (R52,", 410, 205, size=1.3)
    s.text("at the LED block), IO3/45 internal.  Flash + console + JTAG = USB-CDC (IO19/20).", 410, 210, size=1.3)
