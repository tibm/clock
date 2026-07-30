"""Generate sensor_custom.kicad_sym — symbols KiCad's stock libraries lack for
the sensor daughterboard.

Only one part needs a custom symbol: the **BNO085** (CEVA/Hillcrest 9-axis
fusion IMU, 28-pin LGA 5.2x3.8).  KiCad ships `Sensor_Motion:BNO055`, which is
the same *package* but a completely different pinout, so it cannot be reused.

The other two sensors use stock symbols verbatim:
  - `Sensor:BME680`            -> BME688 (value overridden).  Pin table is
    identical (BME688 datasheet Table 26: 1 GND, 2 CSB, 3 SDI, 4 SCK, 5 SDO,
    6 VDDIO, 7 GND, 8 VDD) and so is the package (LGA-8 3x3 mm, 0.8 mm pitch,
    clockwise numbering).
  - `Sensor_Optical:TSL25911FN`

Pin `(at)` is the outer connection endpoint; library Y is up.
Run:  python3 mksym.py   (then build.py)
"""
from __future__ import annotations
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN_GEN = os.path.abspath(os.path.join(HERE, "..", "..", "kicad", "gen"))
sys.path.insert(0, MAIN_GEN)

from mksym import HEADER, ic_symbol, P  # noqa: E402  (main-board helpers)

OUT = os.path.join(HERE, "sensor_custom.kicad_sym")

DS = "https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf"


def bno085():
    """BNO08X pin map per datasheet Figure 1-6 (§1.2.1), grouped by function:

      left   = host I2C + the pins that get config straps (SA0, H_CSN, NRST,
               BOOTN) + the three tie-to-GND mode pins (PS1, PS0, CLKSEL0)
      right  = clock (XIN32/XOUT32), CAP, the secondary "environmental" I2C
               master bus, then the nine RESV_NC pins in a block
      top    = VDD (sensors, 2.4-3.6 V) + VDDIO (core/IO, 1.65-3.6 V)
      bottom = GND + GNDIO

    Slots are spaced so wires/labels never collide in the sheet layout.
    """
    L = [  # (slot, num, name, etype)
        (0,  "19", "H_SCL/SCK/RX",   "bidirectional"),
        (1,  "20", "H_SDA/MISO/TX",  "bidirectional"),
        (2,  "14", "H_INTN",         "output"),
        (4,  "17", "SA0/H_MOSI",     "input"),
        (5,  "18", "H_CSN",          "input"),
        (7,  "11", "NRST",           "input"),
        (8,  "4",  "BOOTN",          "input"),
        (10, "5",  "PS1",            "input"),
        (11, "6",  "PS0/WAKE",       "input"),
        (12, "10", "CLKSEL0",        "input"),
    ]
    R = [
        (0,  "27", "XIN32",           "input"),
        (1,  "26", "XOUT32/CLKSEL1",  "output"),
        (3,  "15", "ENV_SCL",         "bidirectional"),
        (4,  "16", "ENV_SDA",         "bidirectional"),
        (6,  "9",  "CAP",             "passive"),
    ]
    # 9x reserved / no-connect, kept visible so every pad is accounted for
    RESV = ["1", "7", "8", "12", "13", "21", "22", "23", "24"]

    pins = [P(num, nm, "L", slot, et) for slot, num, nm, et in L]
    pins += [P(num, nm, "R", slot, et) for slot, num, nm, et in R]
    pins += [P(num, "RESV_NC", "R", 8 + i, "no_connect")
             for i, num in enumerate(RESV)]
    pins.append(P("3", "VDD", "T", 0, "power_in"))
    pins.append(P("28", "VDDIO", "T", 1, "power_in"))
    pins.append(P("2", "GND", "B", 0, "power_in"))
    pins.append(P("25", "GNDIO", "B", 1, "power_in"))

    return ic_symbol(
        "BNO085", "Package_LGA:LGA-28_5.2x3.8mm_P0.5mm", DS, pins,
        description="CEVA BNO085 9-axis fusion IMU (accel+gyro+mag + SH-2 "
                    "sensor hub), I2C/SPI/UART, 28-LGA 5.2x3.8mm",
        width=45.72, value="BNO085")


def build():
    with open(OUT, "w") as f:
        f.write(HEADER)
        f.write(bno085())
        f.write(")\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    build()
