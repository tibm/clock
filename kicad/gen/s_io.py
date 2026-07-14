"""Sheet: IO Expander / Sensors / LED drivers / Homing / Encoder / Buttons.
MCP23017 (I2C, addr 0x20) fans out slow lines; QRE1113 optical homing;
STEMMA-QT sensor header; rotary encoder + 3 buttons; 3x AO3400A LED low-side.
Maps per esp32.md + led.md + power_values.md §6/§8.
"""
from sch import FP

# MCP23017 pad number -> net (GPA0..7 = 21..28, GPB0..7 = 1..8)
EXP = {
    "21": "SPK_SD", "22": "STEP_STBY", "23": "BOOST12_EN",
    "24": "BTN1", "25": "BTN2", "26": "BTN3", "27": "ENC_SW", "28": None,   # GPA7 free
    "1": "PD_PG", "2": "CHRG", "3": "FAULT", "4": "LCD_DISP",
    "5": "FULLCHG_EN", "6": "VBAT_DIV_EN", "7": "SPK_FAULT", "8": None,       # GPB7 free
}


def build(sch):
    sch.text("IO EXPANDER (MCP23017 0x20) / SENSORS / LEDs / HOMING / ENCODER / BUTTONS",
             30, 20, size=2.0)

    # ================= MCP23017 =================
    U13 = sch.comp("U13", "Interface_Expansion:MCP23017x-x-SS", 130, 120,
                   value="MCP23017", footprint="Package_SO:SSOP-28_5.3x10.2mm_P0.65mm")
    for num, net in EXP.items():
        if net:
            sch.net(U13, num, net)
        else:
            sch.nc(U13, num)
    sch.power(U13, "9", "+3V3")
    sch.power(U13, "10", "GND")
    Cmcp = sch.C("C240", 95, 160, "100nF"); sch.power(Cmcp, "1", "+3V3"); sch.power(Cmcp, "2", "GND")
    sch.net(U13, "12", "I2C_SCL")
    sch.net(U13, "13", "I2C_SDA")
    # address 0x20: A0/A1/A2 -> GND via 0R straps
    for i, p in enumerate(("15", "16", "17")):
        r = sch.R(f"R9{i}", 80 + i * 8, 130, "0R")
        sch.net(r, "1", f"MCP_A{i}"); sch.power(r, "2", "GND")
        sch.net(U13, p, f"MCP_A{i}")
    # RESET -> 10k pull-up
    Rrst = sch.R("R93", 80, 100, "10k"); sch.power(Rrst, "1", "+3V3"); sch.net(Rrst, "2", "MCP_RESET")
    sch.net(U13, "18", "MCP_RESET")
    # INTA/INTB mirrored -> EXPANDER_INT + 10k pull-up
    sch.net(U13, "19", "EXPANDER_INT"); sch.net(U13, "20", "EXPANDER_INT")
    Rint = sch.R("R94", 180, 100, "10k"); sch.power(Rint, "1", "+3V3"); sch.net(Rint, "2", "EXPANDER_INT")
    sch.nc(U13, "11"); sch.nc(U13, "14")

    # ================= I2C main-board pull-ups =================
    Rsda = sch.R("R95", 210, 60, "4.7k"); sch.power(Rsda, "1", "+3V3"); sch.net(Rsda, "2", "I2C_SDA")
    Rscl = sch.R("R96", 224, 60, "4.7k"); sch.power(Rscl, "1", "+3V3"); sch.net(Rscl, "2", "I2C_SCL")

    # ================= Sensor STEMMA-QT header (I2C chain) =================
    J7 = sch.comp("J7", "Connector_Generic:Conn_01x04", 250, 70,
                  value="STEMMA-QT (BME688/TSL2591/LIS3DH)",
                  footprint="Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal")
    sch.power(J7, "1", "GND")
    sch.power(J7, "2", "+3V3")
    sch.net(J7, "3", "I2C_SDA")
    sch.net(J7, "4", "I2C_SCL")
    sch.text("Sensor INT (LIS3DH tap / TSL2591) -> MCU IO42 (SENSOR_INT) on the header board.",
             240, 95, size=1.1)
    # SENSOR_INT reference (routed to the STEMMA board's INT); pulled up here
    Rsi = sch.R("R97", 300, 70, "10k"); sch.power(Rsi, "1", "+3V3"); sch.net(Rsi, "2", "SENSOR_INT")

    # ================= QRE1113 optical homing =================
    U14 = sch.comp("U14", "Sensor_Proximity:QRE1113", 130, 210, value="QRE1113",
                   footprint="")
    Rled = sch.R("R98", 100, 195, "150R"); sch.power(Rled, "1", "+3V3"); sch.net(Rled, "2", "HOME_LEDA")
    sch.net(U14, "1", "HOME_LEDA")   # anode
    sch.power(U14, "2", "GND")       # cathode
    Rpt = sch.R("R99", 160, 195, "10k"); sch.power(Rpt, "1", "+3V3"); sch.net(Rpt, "2", "HOME_OPTO")
    sch.net(U14, "3", "HOME_OPTO")   # collector -> ADC
    sch.power(U14, "4", "GND")       # emitter
    Chome = sch.C("C241", 175, 210, "100nF"); sch.net(Chome, "1", "HOME_OPTO"); sch.power(Chome, "2", "GND")

    # ================= Rotary encoder + push switch =================
    ENC = sch.comp("SW3", "Device:RotaryEncoder_Switch", 250, 150, value="Encoder",
                   footprint="Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm")
    sch.net(ENC, "A", "ENC_A")
    sch.net(ENC, "B", "ENC_B")
    sch.power(ENC, "C", "GND")
    sch.net(ENC, "S1", "ENC_SW")
    sch.power(ENC, "S2", "GND")
    # encoder A/B pull-ups (MCU internal PU also, but add for clean edges)
    Rea = sch.R("R100", 285, 130, "10k"); sch.power(Rea, "1", "+3V3"); sch.net(Rea, "2", "ENC_A")
    Reb = sch.R("R101", 297, 130, "10k"); sch.power(Reb, "1", "+3V3"); sch.net(Reb, "2", "ENC_B")

    # ================= Rear tactile buttons =================
    for i, net in enumerate(("BTN1", "BTN2", "BTN3")):
        b = sch.comp(f"SW{4+i}", "Switch:SW_Push", 250 + i * 25, 210, value=net,
                     footprint="Button_Switch_SMD:SW_SPST_CK_RS282G05A3")
        sch.net(b, "1", net); sch.power(b, "2", "GND")

    # ================= LED low-side drivers (3x AO3400A) =================
    def led_fet(ref, rg, rpd, pwm_net, drain_net, x):
        Q = sch.comp(ref, "Transistor_FET:AO3400A", x, 260, value="AO3400A",
                     footprint=FP["SOT23-3"])
        Rgg = sch.R(rg, x - 25, 260, "100R"); sch.net(Rgg, "1", pwm_net); sch.net(Rgg, "2", f"{ref}_G")
        Rpull = sch.R(rpd, x - 12, 275, "10k"); sch.net(Rpull, "1", f"{ref}_G"); sch.power(Rpull, "2", "GND")
        sch.net(Q, "1", f"{ref}_G")
        sch.power(Q, "2", "GND")
        sch.net(Q, "3", drain_net)
        return Q

    led_fet("Q5", "R102", "R103", "PANEL_PWM", "PANEL_K", 90)
    led_fet("Q6", "R104", "R105", "WAKE_WARM_PWM", "WARM_K", 190)
    led_fet("Q7", "R106", "R107", "WAKE_COOL_PWM", "COOL_K", 290)

    # LED connectors
    Jpanel = sch.comp("J8", "Connector_Generic:Conn_01x02", 120, 285,
                      value="Panel LEDs (5V, CLM3C x<=12)",
                      footprint="Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical")
    sch.power(Jpanel, "1", "+5V"); sch.net(Jpanel, "2", "PANEL_K")
    Jwake = sch.comp("J9", "Connector_Generic:Conn_01x03", 240, 285,
                     value="Wake COB (12V warm+cool)",
                     footprint="Connector_JST:JST_PH_B3B-PH-K_1x03_P2.00mm_Vertical")
    sch.power(Jwake, "1", "+12V"); sch.net(Jwake, "2", "WARM_K"); sch.net(Jwake, "3", "COOL_K")

    sch.text("Wake LEDs 12V plugged-only; panel LEDs 5V always. Each series R is on the LED board.",
             30, 300, size=1.2)
