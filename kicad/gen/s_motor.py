"""Sheet: Motor — 2x TB6612FNG (PWM-on-IN microstep) driving the dual-shaft
Juken X40.879.  Driver #1 = minute (external shaft), #2 = hour (internal shaft).
PWMA/PWMB tied HIGH to VCC; STBY shared (STEP_STBY). Map per esp32.md.
"""
from sch import FP


def _driver(sch, ref, x, ain1, ain2, bin1, bin2, coils, cbase):
    """Place one TB6612FNG. coils = dict for AO1/AO2/BO1/BO2 net names."""
    U = sch.comp(ref, "Driver_Motor:TB6612FNG", x, 130, value="TB6612FNG",
                 footprint="Package_SO:SSOP-24_5.3x8.2mm_P0.65mm")
    # logic inputs
    sch.net(U, "21", ain1)  # AIN1
    sch.net(U, "22", ain2)  # AIN2
    sch.net(U, "17", bin1)  # BIN1
    sch.net(U, "16", bin2)  # BIN2
    # PWMA/PWMB tied HIGH to VCC (3V3)
    sch.power(U, "23", "+3V3")
    sch.power(U, "15", "+3V3")
    # STBY shared
    sch.net(U, "19", "STEP_STBY")
    # supplies
    sch.power(U, "13", "+5V"); sch.power(U, "14", "+5V"); sch.power(U, "24", "+5V")
    sch.power(U, "20", "+3V3")
    sch.power(U, "18", "GND")
    for p in ("3", "4", "9", "10"):
        sch.power(U, p, "GND")
    # coil outputs (doubled pads)
    sch.net(U, "1", coils["AO1"]); sch.net(U, "2", coils["AO1"])
    sch.net(U, "5", coils["AO2"]); sch.net(U, "6", coils["AO2"])
    sch.net(U, "11", coils["BO1"]); sch.net(U, "12", coils["BO1"])
    sch.net(U, "7", coils["BO2"]); sch.net(U, "8", coils["BO2"])
    # decoupling
    c1 = sch.C(cbase + "0", x - 22, 175, "10uF", fp="C0805")
    sch.power(c1, "1", "+5V"); sch.power(c1, "2", "GND")
    c2 = sch.C(cbase + "1", x - 14, 175, "100nF")
    sch.power(c2, "1", "+5V"); sch.power(c2, "2", "GND")
    c3 = sch.C(cbase + "2", x - 6, 175, "100nF")
    sch.power(c3, "1", "+3V3"); sch.power(c3, "2", "GND")
    return U


def build(sch):
    sch.text("MOTOR — 2x TB6612FNG (PWM-on-IN) -> Juken X40.879 dual-shaft stepper",
             30, 25, size=2.0)

    # Driver #1 — minute / external shaft
    _driver(sch, "U11", 90,
            "STEP_M_AIN1", "STEP_M_AIN2", "STEP_M_BIN1", "STEP_M_BIN2",
            {"AO1": "STEP_1E", "AO2": "STEP_2E", "BO1": "STEP_4E", "BO2": "STEP_3E"},
            "C20")
    # Driver #2 — hour / internal shaft
    _driver(sch, "U12", 230,
            "STEP_H_AIN1", "STEP_H_AIN2", "STEP_H_BIN1", "STEP_H_BIN2",
            {"AO1": "STEP_1I", "AO2": "STEP_2I", "BO1": "STEP_4I", "BO2": "STEP_3I"},
            "C21")

    # Stepper connector (8 coil terminals: external 1e-4e, internal 1i-4i)
    J = sch.comp("J4", "Connector_Generic:Conn_01x08", 320, 130,
                 value="X40.879 dual-shaft stepper",
                 footprint="Connector_JST:JST_PH_B8B-PH-K_1x08_P2.00mm_Vertical")
    for i, net in enumerate(["STEP_1E", "STEP_2E", "STEP_3E", "STEP_4E",
                             "STEP_1I", "STEP_2I", "STEP_3I", "STEP_4I"]):
        sch.net(J, str(i + 1), net)

    sch.text("PWMA/PWMB tied to VCC (3V3). STBY = STEP_STBY (expander, idle-low at boot).",
             30, 195, size=1.2)
    sch.text("Coil map per esp32.md: chA=AO1/AO2, chB=BO1/BO2; shaft/phase firmware-trimmable.",
             30, 200, size=1.2)
