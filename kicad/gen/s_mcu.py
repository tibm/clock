"""Sheet: MCU — ESP32-S3-WROOM-1-N16R8 host.
Pin/GPIO allocation per esp32.md.  Native USB (IO19/20) -> USB_DP/USB_DM,
XTAL32K on IO15/16, boot/reset straps + programming header.
Symbol pin NUMBERS (module pads) carry GPIO names; nets attached by number.
"""
from sch import FP

# module-pad pin number -> (net name, kind)  kind: 'g'=global label, 'p'=power, 'nc'
PINMAP = {
    # power / ground
    "2":  ("+3V3", "p"), "1": ("GND", "p"), "40": ("GND", "p"), "41": ("GND", "p"),
    "3":  ("MCU_EN", "special"),        # EN handled below
    "27": ("MCU_IO0", "special"),       # IO0 boot strap
    # native USB
    "13": ("USB_DM", "g"),              # USB_D- = IO19
    "14": ("USB_DP", "g"),              # USB_D+ = IO20
    # steppers (minute = unit0)
    "4":  ("STEP_M_AIN1", "g"), "5": ("STEP_M_AIN2", "g"),
    "6":  ("STEP_M_BIN1", "g"), "15": ("STEP_M_BIN2", "g"),
    # steppers (hour = unit1)
    "31": ("STEP_H_AIN1", "g"), "32": ("STEP_H_AIN2", "g"),
    "33": ("STEP_H_BIN1", "g"), "34": ("STEP_H_BIN2", "g"),
    # LED PWM
    "7":  ("PANEL_PWM", "g"), "26": ("WAKE_WARM_PWM", "g"), "16": ("WAKE_COOL_PWM", "g"),
    # I2C shared
    "12": ("I2C_SDA", "g"), "17": ("I2C_SCL", "g"),
    # I2S audio
    "18": ("I2S_BCLK", "g"), "19": ("I2S_LRCLK", "g"), "20": ("I2S_DOUT", "g"),
    "37": ("I2S_MCLK", "g"),            # TXD0 = IO43
    # SPI (display + microSD)
    "21": ("SPI_SCLK", "g"), "22": ("SPI_MOSI", "g"), "23": ("SPI_MISO", "g"),
    "10": ("LCD_CS", "g"), "11": ("SD_CS", "g"),
    # analog
    "39": ("VBAT_SENSE", "g"), "38": ("HOME_OPTO", "g"),
    # interrupts / encoder
    "35": ("SENSOR_INT", "g"), "36": ("EXPANDER_INT", "g"),
    "24": ("ENC_A", "g"), "25": ("ENC_B", "g"),
    # XTAL32K
    "8":  ("XTAL32K_P", "g"), "9": ("XTAL32K_N", "g"),
    # octal-PSRAM reserved pads -> not available
    "28": (None, "nc"), "29": (None, "nc"), "30": (None, "nc"),
}


def build(sch):
    sch.text("MCU — ESP32-S3-WROOM-1-N16R8 (host, 3.3 V logic, native USB-JTAG)",
             30, 20, size=2.0)

    U8 = sch.comp("U8", "RF_Module:ESP32-S3-WROOM-1", 210, 130,
                  value="ESP32-S3-WROOM-1-N16R8",
                  footprint="RF_Module:ESP32-S3-WROOM-1")

    for num, (net, kind) in PINMAP.items():
        if kind == "g":
            sch.net(U8, num, net)
        elif kind == "p":
            sch.power(U8, num, net)
        elif kind == "nc":
            sch.nc(U8, num)

    # --- 3V3 decoupling (bulk + per-pad) ---
    for i, v in enumerate(["10uF", "22uF", "100nF", "100nF"]):
        c = sch.C(f"C14{i}", 150 + i * 9, 60, v,
                  fp="C0805" if "uF" in v else "C0603")
        sch.power(c, "1", "+3V3"); sch.power(c, "2", "GND")

    # --- EN reset: 10k PU + 1uF + reset button ---
    Ren = sch.R("R50", 150, 100, "10k"); sch.power(Ren, "1", "+3V3"); sch.net(Ren, "2", "MCU_EN")
    sch.net(U8, "3", "MCU_EN")
    Cen = sch.C("C144", 165, 100, "1uF"); sch.net(Cen, "1", "MCU_EN"); sch.power(Cen, "2", "GND")
    SWr = sch.comp("SW1", "Switch:SW_Push", 135, 115, value="RESET", footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
    sch.net(SWr, "1", "MCU_EN"); sch.power(SWr, "2", "GND")

    # --- IO0 boot strap: 10k PU + boot button ---
    Rb = sch.R("R51", 150, 175, "10k"); sch.power(Rb, "1", "+3V3"); sch.net(Rb, "2", "MCU_IO0")
    sch.net(U8, "27", "MCU_IO0")
    SWb = sch.comp("SW2", "Switch:SW_Push", 135, 190, value="BOOT", footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
    sch.net(SWb, "1", "MCU_IO0"); sch.power(SWb, "2", "GND")

    # --- IO46 strap: 10k pulldown (ensure low at boot) ---
    Rpd = sch.R("R52", 270, 175, "10k"); sch.net(Rpd, "1", "WAKE_COOL_PWM"); sch.power(Rpd, "2", "GND")

    # --- 32.768 kHz crystal (ABS07) on IO15/IO16 ---
    Y1 = sch.comp("Y1", "Device:Crystal", 300, 90, value="ABS07-32.768kHz",
                  footprint="Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm")
    sch.net(Y1, "1", "XTAL32K_P"); sch.net(Y1, "2", "XTAL32K_N")
    Cx1 = sch.C("C145", 285, 110, "18pF"); sch.net(Cx1, "1", "XTAL32K_P"); sch.power(Cx1, "2", "GND")
    Cx2 = sch.C("C146", 315, 110, "18pF"); sch.net(Cx2, "1", "XTAL32K_N"); sch.power(Cx2, "2", "GND")

    # --- Programming / debug header (3V3, GND, EN, IO0); USB tap is on J1 ---
    JP = sch.comp("J2", "Connector_Generic:Conn_01x04", 300, 175, value="PROG",
                  footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical")
    sch.power(JP, "1", "+3V3")
    sch.power(JP, "2", "GND")
    sch.net(JP, "3", "MCU_EN")
    sch.net(JP, "4", "MCU_IO0")

    sch.text("Logging + flash + JTAG over USB-CDC (IO19/20). IO43=I2S_MCLK, IO44=EXPANDER_INT.",
             30, 235, size=1.2)
    sch.text("IO35/36/37 reserved by octal PSRAM (N16R8) -> not connected.", 30, 240, size=1.2)
