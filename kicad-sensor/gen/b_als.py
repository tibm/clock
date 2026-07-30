"""Block: AMBIENT LIGHT — TSL2591 (`TSL25911FN`, I2C @ 0x29, fixed address).

Per the datasheet's "Application Information" (Fig. TSL2591-17): a 1 uF
low-ESR cap as close as possible to VDD, and INT is an open-drain output that
needs its own pull-up (10 k suggested).  Pin 4 is "NC — do not connect".
"""


def build(s):
    s.frame(12, 200, 140, 254, "AMBIENT LIGHT — TSL2591 (I2C 0x29)")

    U3 = s.comp("U3", "Sensor_Optical:TSL25911FN", 63.50, 228.60,
                value="TSL25911FN", footprint="OptoDevice:AMS_TSL25911FN",
                refpos=(78.74, 236.22, "left"), valpos=(78.74, 239.37, "left"))

    s.rail(U3, "5", "+3V3", rise=5.08)
    s.gnd(U3, "3", drop=5.08)
    s.nc(U3, "4")                      # "NC - do not connect" (pin table)

    # longer stubs: this symbol's pins sit ON the body edge, so a 2.54 mm
    # stub would park the label flag over the pin name.
    s.glabel(U3, "1", "I2C_SCL", stub=7.62)
    s.glabel(U3, "6", "I2C_SDA", stub=7.62)

    # 1 uF low-ESR at VDD — the one hard placement rule this part has.
    C12 = s.C("C12", 46.99, 220.98, "1uF")
    s.rail(C12, "1", "+3V3", rise=2.54)
    s.gnd(C12, "2", drop=2.54)

    # INT (open drain, active low) -> J1 pin 6.  The pull-up MUST live here:
    # the main board leaves J7 pin 6 unconnected, so nothing pulls it there.
    s.pw(U3, "2", ("x", 101.60))
    R12 = s.R("R12", 101.60, 227.33, "10k")
    s.rail(R12, "1", "+3V3", rise=2.54)
    s.w((101.60, 231.14), (109.22, 231.14))
    s.glabel_at("ALS_INT", 109.22, 231.14, 0)

    s.text("Clear window / no shadow over U3; keep it off-axis from the dial LEDs.",
           15, 249.5, size=1.3)
