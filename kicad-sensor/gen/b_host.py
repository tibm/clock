"""Block: HOST LINK — J1 (JST ZH 1x06) to main-board J7, rail entry
decoupling, board-side I2C pull-ups and the two ERC power-source flags.

J1 mirrors the main board's J7 1:1 (pin n <-> pin n):
    1 GND | 2 +3V3 | 3 I2C_SDA | 4 I2C_SCL | 5 SENSOR_INT | 6 ALS_INT
Pin 6 is a spare wire on the main board (J7 pin 6 = no-connect there); the
TSL2591 interrupt is landed on it anyway so a future firmware / main-board
revision can pick it up without a new harness.
"""


def build(s):
    s.frame(12, 26, 140, 96, "HOST LINK  (J1 -> main-board J7, JST ZH 1x06)")

    J1 = s.comp("J1", "Connector_Generic:Conn_01x06", 95.25, 46.99,
                value="To main board J7",
                footprint="Connector_JST:JST_ZH_B6B-ZR_1x06_P1.50mm_Vertical",
                refpos=(95.25, 37.47, None), valpos=(95.25, 60.33, None))

    # GND leaves downwards, +3V3 upwards.  With pin 1 sitting above pin 2 the
    # two runs cannot both clear each other, so they cross once at
    # (63.50, 44.45) with no junction — same idiom as the main board's J7.
    s.pw(J1, "1", ("x", 63.50), ("dy", 13.97))
    s.power_at(63.50, 55.88, "GND")
    s.pw(J1, "2", ("x", 53.34), ("dy", -6.35))
    s.power_at(53.34, 38.10, "+3V3")

    s.glabel(J1, "3", "I2C_SDA")
    s.glabel(J1, "4", "I2C_SCL")
    s.glabel(J1, "5", "SENSOR_INT")   # BNO085 H_INTN (active low)
    s.glabel(J1, "6", "ALS_INT")      # TSL2591 INT  (open drain, active low)

    # --- rail decoupling at the cable entry ------------------------------
    # The 6-way ZH harness has ONE power and ONE ground wire; C1 absorbs the
    # BME688 heater bursts (~18 mA peak) and the BNO085 turn-on so they do not
    # ride back up the cable into the main board's 3V3 rail.
    C1 = s.C("C1", 33.02, 68.58, "10uF", fp="C0805")
    s.rail(C1, "1", "+3V3", rise=2.54)
    s.gnd(C1, "2", drop=2.54)
    C2 = s.C("C2", 43.18, 68.58, "100nF")
    s.rail(C2, "1", "+3V3", rise=2.54)
    s.gnd(C2, "2", drop=2.54)

    # --- board-side I2C pull-ups -----------------------------------------
    # The main board already pulls SDA/SCL up with 4.7k (R95/R96).  10k here
    # parallels to ~3.2k, inside the 2-4k window the BNO08X datasheet asks for
    # (note 7 of Fig. 1-11) and quick enough for 400 kHz over the harness.
    # Depopulate both if you would rather run on the 4.7k alone at 100 kHz.
    R1 = s.R("R1", 53.34, 68.58, "10k")
    s.rail(R1, "1", "+3V3", rise=2.54)
    s.glabel(R1, "2", "I2C_SDA")
    R2 = s.R("R2", 78.74, 68.58, "10k")
    s.rail(R2, "1", "+3V3", rise=2.54)
    s.glabel(R2, "2", "I2C_SCL")

    # --- ERC power-source declarations -----------------------------------
    # Nothing on this board drives the rails; they arrive on the harness, so
    # each one needs a PWR_FLAG or ERC calls it undriven.
    s.power_at(24.13, 83.82, "+3V3")
    s.w((24.13, 83.82), (24.13, 86.36), (31.75, 86.36))
    s.pwr_flag(31.75, 86.36)
    s.power_at(36.83, 88.90, "GND")
    s.w((36.83, 88.90), (36.83, 86.36), (44.45, 86.36))
    s.pwr_flag(44.45, 86.36)

    s.text("Rails are fed by the main board — the flags only tell ERC so.",
           58, 86, size=1.3)
    s.text("Harness must be 1:1 (pin n <-> pin n), NOT reversed — see below.",
           58, 90.5, size=1.3)

    # The one hazard this 6-way connector cannot be keyed out of; it belongs
    # on the printed sheet, not only in the README (REVIEW.md #4).
    s.text("HARNESS POLARITY IS A DESTRUCTIVE FAULT", 14, 100, size=1.6,
           bold=True)
    for i, t in enumerate([
        "Both ends are ZHR-6 and J1 mirrors J7 1:1, so a cable loaded BACKWARDS plugs in perfectly — and",
        "J1.5 then receives J7.2 +3V3 while this board's VDDIO is 0 V.  BNO08X abs max (Fig. 6-1) is \"VDDIO",
        "+ 0.3 V at any logic pin\", series-limited only by the main board's R97 10k: assume U1 is destroyed.",
        "J10 (knob) is the SAME ZH 1x06 header 13.5 mm from J7, with +5 V on pin 2 — that mis-mate puts",
        "5 V on +3V3 and takes all three sensors at once.  Label both cables; check before first power-up.",
    ]):
        s.text(t, 14, 104.5 + i * 2.9, size=1.15)
