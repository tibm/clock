"""Sheet: MCU — ESP32-S3-WROOM-1-N16R8 host.
Pin/GPIO allocation per esp32.md.  Almost every GPIO is a cross-sheet signal
(global label). On-page circuitry (EN/IO0 straps, XTAL32K, decoupling) is
clustered at its pins so those nets are short wires.
"""
from sch import FP

PINMAP = {
    "2":  ("+3V3", "p"), "1": ("GND", "p"), "40": ("GND", "p"), "41": ("GND", "p"),
    "3":  ("MCU_EN", "l"), "27": ("MCU_IO0", "l"),
    "13": ("USB_DM", "g"), "14": ("USB_DP", "g"),
    "4":  ("STEP_M_AIN1", "g"), "5": ("STEP_M_AIN2", "g"),
    "6":  ("STEP_M_BIN1", "g"), "15": ("STEP_M_BIN2", "g"),
    "31": ("STEP_H_AIN1", "g"), "32": ("STEP_H_AIN2", "g"),
    "33": ("STEP_H_BIN1", "g"), "34": ("STEP_H_BIN2", "g"),
    "7":  ("PANEL_PWM", "g"), "26": ("WAKE_WARM_PWM", "g"), "16": ("WAKE_COOL_PWM", "g"),
    "12": ("I2C_SDA", "g"), "17": ("I2C_SCL", "g"),
    "18": ("I2S_BCLK", "g"), "19": ("I2S_LRCLK", "g"), "20": ("I2S_DOUT", "g"),
    "37": ("I2S_MCLK", "g"),
    "21": ("SPI_SCLK", "g"), "22": ("SPI_MOSI", "g"), "23": ("SPI_MISO", "g"),
    "10": ("LCD_CS", "g"), "11": ("SD_CS", "g"),
    "39": ("VBAT_SENSE", "g"), "38": ("HOME_OPTO", "g"),
    "35": ("SENSOR_INT", "g"), "36": ("EXPANDER_INT", "g"),
    "24": ("ENC_A", "g"), "25": ("ENC_B", "g"),
    "8":  ("XTAL32K_P", "l"), "9": ("XTAL32K_N", "l"),
    "28": (None, "nc"), "29": (None, "nc"), "30": (None, "nc"),
}


def build(sch):
    sch.text("MCU — ESP32-S3-WROOM-1-N16R8 (host, 3.3 V logic, native USB-JTAG)",
             30, 20, size=2.0)

    U8 = sch.comp("U8", "RF_Module:ESP32-S3-WROOM-1", 250, 165,
                  value="ESP32-S3-WROOM-1-N16R8", footprint="RF_Module:ESP32-S3-WROOM-1")
    for num, (net, kind) in PINMAP.items():
        if kind in ("g", "l", "p"):
            sch.node(U8, num, net)
        elif kind == "nc":
            sch.nc(U8, num)

    # 3V3 decoupling (bulk + per-pad) above the 3V3 pin -> power symbols
    for i, v in enumerate(["10uF", "22uF", "100nF", "100nF"]):
        c = sch.C(f"C14{i}", 234 + i * 9, 120, v, fp="C0805" if "uF" in v else "C0603")
        sch.node(c, "1", "+3V3"); sch.node(c, "2", "GND")

    # EN/IO0 strap cluster (upper-left, above the IO1.. label stubs) --------------
    # EN pin (3) at ~(234.8,142.1); IO0 pin (27) at ~(234.8,147.2)
    R50 = sch.R("R50", 214, 128, "10k"); sch.node(R50, "1", "+3V3"); sch.node(R50, "2", "MCU_EN")
    Cen = sch.C("C144", 224, 128, "1uF"); sch.node(Cen, "1", "MCU_EN"); sch.node(Cen, "2", "GND")
    SW1 = sch.comp("SW1", "Switch:SW_Push", 205, 141, value="RESET",
                   footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
    sch.node(SW1, "1", "MCU_EN"); sch.node(SW1, "2", "GND")
    R51 = sch.R("R51", 199, 128, "10k"); sch.node(R51, "1", "+3V3"); sch.node(R51, "2", "MCU_IO0")
    SW2 = sch.comp("SW2", "Switch:SW_Push", 205, 148, value="BOOT",
                   footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
    sch.node(SW2, "1", "MCU_IO0"); sch.node(SW2, "2", "GND")
    # programming/debug header (3V3,GND,EN,IO0) beside the straps
    J2 = sch.comp("J2", "Connector_Generic:Conn_01x04", 185, 140, value="PROG",
                  footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical")
    sch.node(J2, "1", "+3V3"); sch.node(J2, "2", "GND")
    sch.node(J2, "3", "MCU_EN"); sch.node(J2, "4", "MCU_IO0")

    # IO46 boot-strap pulldown (right side, at IO46 pin) --------------------------
    R52 = sch.R("R52", 290, 183, "10k"); sch.node(R52, "1", "WAKE_COOL_PWM"); sch.node(R52, "2", "GND")

    # 32.768 kHz crystal on IO15/IO16 (lower-left, below the last left pins) ------
    # IO15 (8) ~(234.8,185.3), IO16 (9) ~(234.8,187.9)
    Y1 = sch.comp("Y1", "Device:Crystal", 208, 205, value="ABS07-32.768kHz",
                  footprint="Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm")
    sch.node(Y1, "1", "XTAL32K_P"); sch.node(Y1, "2", "XTAL32K_N")
    Cx1 = sch.C("C145", 195, 214, "18pF"); sch.node(Cx1, "1", "XTAL32K_P"); sch.node(Cx1, "2", "GND")
    Cx2 = sch.C("C146", 221, 214, "18pF"); sch.node(Cx2, "1", "XTAL32K_N"); sch.node(Cx2, "2", "GND")

    sch.text("Logging + flash + JTAG over USB-CDC (IO19/20). IO43=I2S_MCLK, IO44=EXPANDER_INT.",
             30, 245, size=1.3)
    sch.text("IO35/36/37 reserved by octal PSRAM (N16R8) -> not connected.", 30, 251, size=1.3)
