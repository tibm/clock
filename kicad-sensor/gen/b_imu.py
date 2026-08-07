"""Block: IMU — BNO085 (CEVA/Hillcrest 9-axis fusion, I2C @ 0x4A).

Wired per the datasheet's own I2C reference design (Fig. 1-11 + notes 1-8,
§1.2.1/§1.2.2), with three deliberate deviations, all called out below:
  * pin 18 H_CSN is pulled to VDDIO instead of being left floating,
  * CLKSEL0 is tied straight to GND -- a bare wire, no strap resistor --
    instead of being driven by a host GPIO (Fig. 1-8: "0 or unconnected"
    selects the crystal),
  * NRST gets an RC power-on reset because J7 has no spare host line.

Config straps live in their own row under the symbol and reach the pins by
name (BNO_* labels) — the same style CEVA's reference schematic uses.
"""


def build(s):
    s.frame(150, 26, 410, 180, "IMU — BNO085 (I2C 0x4A, H_INTN -> SENSOR_INT)")

    # Footprint is the vendor (SnapEDA) land, not KiCad's generic
    # Package_LGA:LGA-28_5.2x3.8mm_P0.5mm — the stock one is an IPC pattern whose
    # pads sit 0.10/0.125 mm further out per side; this one measures exactly the
    # 5.2 x 3.8 of datasheet Fig. 7-2.  See ../sensor.pretty + README.
    U1 = s.comp("U1", "sensor:BNO085", 260.35, 100.33, value="BNO085",
                footprint="sensor:CEVA_BNO085_LGA-28_5.2x3.8mm_P0.5mm")

    # ---------------- supplies ----------------
    # VDD (sensors) and VDDIO (core/IO) share the 3V3 rail: §6.3 requires VDD
    # to reach level before or WITH VDDIO — one rail satisfies that by
    # construction.
    s.pw(U1, "3", ("dy", -5.08))
    s.power_at(259.08, 68.58, "+3V3")
    s.pw(U1, "28", ("dy", -2.54), ("x", 259.08))
    s.pw(U1, "2", ("dy", 5.08))
    s.power_at(259.08, 132.08, "GND")
    s.pw(U1, "25", ("dy", 2.54), ("x", 259.08))

    # C3/C4 = the datasheet's C1/C2 (0.1 uF per supply pin); C5 adds the local
    # bulk the reference omits (this board hangs off 200 mm of ZH harness).
    for ref, x, val in (("C3", 236.22, "100nF"), ("C4", 243.84, "100nF"),
                        ("C5", 251.46, "1uF")):
        c = s.C(ref, x, 57.15, val)
        s.rail(c, "1", "+3V3", rise=2.54)
        s.gnd(c, "2", drop=2.54)

    # CAP (pin 9): internal regulator reservoir, 100 nF to GND (Fig. 1-6).
    C6 = s.C("C6", 293.37, 95.25, "100nF", rot=90)
    s.pw(U1, "9", ("x", 289.56))
    s.gnd(C6, "2", drop=2.54)

    # ---------------- 32.768 kHz clock ----------------
    # CLKSEL0 low + crystal on 26/27 = "Crystal" source (Fig. 1-8).  The
    # performance table (Fig. 6-14) is only specified for an external clock or
    # crystal, so the internal RC is not used.  Same ABS07 part as the main
    # board's RTC crystal: 12.5 pF CL, +/-20 ppm — CEVA asks for 50 ppm/12.5 pF.
    Y1 = s.comp("Y1", "Device:Crystal", 297.18, 81.28, value="32.768kHz",
                footprint="Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm", rot=90,
                refpos=(301.63, 70.49, "left"), valpos=(301.63, 73.66, "left"))
    s.pw(U1, "27", ("x", 290.83), ("y", 77.47), ("pin", Y1, "2"))
    s.pw(U1, "26", ("x", 290.83), ("y", 85.09), ("pin", Y1, "1"))
    C7 = s.C("C7", 308.61, 77.47, "22pF", rot=90)
    s.w((297.18, 77.47), (304.80, 77.47))
    s.gnd(C7, "2", drop=2.54, via=5.08)
    C8 = s.C("C8", 308.61, 85.09, "22pF", rot=90)
    s.w((297.18, 85.09), (304.80, 85.09))
    s.gnd(C8, "2", drop=2.54)

    # ---------------- host interface ----------------
    s.glabel(U1, "19", "I2C_SCL")
    s.glabel(U1, "20", "I2C_SDA")
    s.glabel(U1, "14", "SENSOR_INT")   # active low, push-pull (Fig. 6-3)
    s.glabel(U1, "17", "BNO_SA0")
    s.glabel(U1, "18", "BNO_CSN")
    s.glabel(U1, "11", "BNO_NRST")
    s.glabel(U1, "4",  "BNO_BOOTN")
    s.glabel(U1, "15", "BNO_ENV_SCL")
    s.glabel(U1, "16", "BNO_ENV_SDA")

    # PS1 = PS0 = 0 selects I2C (note 5); CLKSEL0 = 0 selects the crystal.
    for p in ("5", "6", "10"):
        s.pw(U1, p, ("x", 226.06))
    s.w((226.06, 105.41), (226.06, 118.11))
    s.power_at(226.06, 118.11, "GND")

    # reserved pins — datasheet Fig. 1-6 says no connect
    for p in ("1", "7", "8", "12", "13", "21", "22", "23", "24"):
        s.nc(U1, p)

    # ---------------- config straps ----------------
    Y = 149.86

    def pu(ref, x, val, net):
        """pull-up: rail -> R -> net label"""
        r = s.R(ref, x, Y, val)
        s.rail(r, "1", "+3V3", rise=2.54)
        s.glabel(r, "2", net)
        return r

    def pd(ref, x, val, net, cap=False):
        """pull-down / decoupler: net label -> part -> GND"""
        p = (s.C if cap else s.R)(ref, x, Y, val)
        s.glabel(p, "1", net)
        s.gnd(p, "2", drop=2.54)
        return p

    # SA0 (pin 17) sets the I2C address LSB: R3 fitted = 0x4A (the CEVA
    # reference's R3/R4 pair, same DNI convention).  Move to R4 for 0x4B.
    # 10k, NOT the reference's 0R (v0.2, REVIEW.md #1): fitting both halves
    # of the pair is the assembly slip this layout invites, and through 0R
    # that shorts the MAIN board's +3V3 to GND -- the rail this board is fed
    # from, on a daughterboard with no LED and no silk hint.  Through 10k the
    # same slip costs 330 uA, the clock still boots, and the part enumerates
    # at a wrong-but-visible address.  SA0 is a CMOS input sampled at reset
    # (§1.2.2.1): 10k gives >=3.29 V / <=1 mV against V_IH = 0.55*VDDIO
    # = 1.82 V, so the strap is just as unambiguous as a jumper.
    pu("R4", 161.29, "10k (DNP)", "BNO_SA0")
    pd("R3", 173.99, "10k", "BNO_SA0")
    # H_CSN (pin 18) is unused in I2C mode.  Fig. 1-11 leaves it open; we tie
    # it to the inactive level so no CMOS input floats on a battery product.
    pu("R5", 186.69, "10k", "BNO_CSN")
    # NRST: no host line is free on J7, so a 10k/100nF POR holds reset until
    # the rail is up — which also guarantees BOOTN/PS1/PS0/CLKSEL0 (all
    # sampled at reset) are settled first.
    pu("R6", 199.39, "10k", "BNO_NRST")
    pd("C9", 212.09, "100nF", "BNO_NRST", cap=True)
    # BOOTN high = normal boot; low at reset = DFU bootloader (note 4).
    pu("R7", 224.79, "10k", "BNO_BOOTN")
    # Secondary "environmental" I2C master bus: unused here, but note 8 says
    # it MUST be pulled up regardless because the SH-2 firmware probes it at
    # every reset.  Do not tie these to the host bus — the BNO085 is master.
    pu("R8", 237.49, "10k", "BNO_ENV_SCL")
    pu("R9", 250.19, "10k", "BNO_ENV_SDA")

    # test pads: ground BOOTN then pulse NRST to enter the DFU bootloader
    TP1 = s.comp("TP1", "Connector:TestPoint", 262.89, Y, value="NRST",
                 footprint="TestPoint:TestPoint_Pad_D1.0mm")
    s.glabel(TP1, "1", "BNO_NRST")
    TP2 = s.comp("TP2", "Connector:TestPoint", 275.59, Y, value="BOOTN",
                 footprint="TestPoint:TestPoint_Pad_D1.0mm")
    s.glabel(TP2, "1", "BNO_BOOTN")

    s.text("CONFIG STRAPS  (BNO_* nets join by name)", 155, 129, size=1.6, bold=True)
    s.text("Read the BNO085 only when SENSOR_INT is asserted: on a poll with no data ready it", 161.29, 172, size=1.3)
    s.text("STRETCHES SCL (§1.2.2.1) and would stall the whole shared bus (BME688/TSL2591/MCP23017).", 161.29, 176.5, size=1.3)
