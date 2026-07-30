"""Block: ENVIRONMENT — BME688 (gas/VOC + humidity + pressure + temperature,
I2C @ 0x77).

Stock `Sensor:BME680` symbol + LGA-8 footprint: the BME688 pin table
(datasheet Table 26) and package (3x3 mm, 0.8 mm pitch, clockwise numbering)
are identical, only the value differs.  Wiring per §7.2 "Connection diagrams
(a) I2C": 100 nF on each supply, CSB hard-tied to VDDIO, SDO strapped.
"""


def build(s):
    s.frame(12, 118, 140, 186, "ENVIRONMENT — BME688 (I2C 0x77)")

    U2 = s.comp("U2", "Sensor:BME680", 76.20, 153.67, value="BME688",
                footprint="Package_LGA:Bosch_LGA-8_3x3mm_P0.8mm_"
                          "ClockwisePinNumbering",
                refpos=(53.34, 160.02, "left"), valpos=(53.34, 163.19, "left"))

    # supplies (VDD analog, VDDIO interface) — both on the 3V3 rail
    s.rail(U2, "6", "+3V3", rise=5.08)
    s.pw(U2, "8", ("dy", -2.54), ("x", 73.66))
    s.gnd(U2, "1", drop=5.08)
    s.pw(U2, "7", ("dy", 2.54), ("x", 73.66))

    # C10/C11 = the datasheet's C1/C2 (100 nF recommended for the I2C case)
    C10 = s.C("C10", 55.88, 138.43, "100nF")
    s.rail(C10, "1", "+3V3", rise=2.54)
    s.gnd(C10, "2", drop=2.54)
    C11 = s.C("C11", 46.99, 138.43, "100nF")
    s.rail(C11, "1", "+3V3", rise=2.54)
    s.gnd(C11, "2", drop=2.54)

    # CSB HARD-TIED to VDDIO (§6.1): if CSB is ever pulled low the part latches
    # into SPI until the next power-on reset and never answers on I2C again.
    s.pw(U2, "2", ("x", 99.06), ("dy", 7.62), ("x", 91.44))
    s.power_at(91.44, 168.91, "+3V3")

    s.glabel(U2, "3", "I2C_SDA")      # SDI
    s.glabel(U2, "4", "I2C_SCL")      # SCK

    # SDO sets the address LSB and MUST NOT float (§6.2).
    # R10 fitted -> VDDIO -> 0x77, matching the Adafruit 5046 STEMMA-QT board
    # used for bring-up, so firmware ported from it needs no change.
    # Move to R11 for 0x76 (Bosch's own "default address").
    s.pw(U2, "5", ("x", 111.76))
    R10 = s.R("R10", 99.06, 142.24, "0R")
    s.rail(R10, "1", "+3V3", rise=2.54)
    R11 = s.R("R11", 111.76, 149.86, "0R (DNP)")
    s.gnd(R11, "2", drop=0)

    s.text("Gas sensor: needs ambient air and distance from self-heating parts;", 15, 176, size=1.3)
    s.text("keep an enclosure vent over U2 and do not conformal-coat it.", 15, 180.5, size=1.3)
