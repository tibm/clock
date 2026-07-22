"""Blocks: CHARGER (LT3652) and BATTERY + PROTECTION (18650, reverse P-FET,
HY2111 + AOSD32334C, TCO). Values per power_values.md §1/§6.

Wire topology (all drawn, no labels except expander control lines):
  VBUS rail (y=55.88) -> LT3652 VIN;  SW -> L1 -> [M] -> R18 -> VBAT bus (x=340.36)
  VBAT bus -> down (y=212.09) -> left (x=175.26, in the frame gutter) -> down
  into the RAILS block.
  Battery: holder+ -> Q2 (reverse P-FET) -> VBAT wire joining the bus staircase.
  Cell-: holder- -> AOSD32334C S1; S2 -> F1 (TCO 77C) -> GND (PACK-).
"""
U = 2.54


def build(s):
    s.frame(180, 40, 395, 215, "CHARGER — LT3652 (1S buck, 4.05 V float, 1.0 A)")
    s.frame(15, 160, 170, 308, "BATTERY — 18650 + reverse P-FET + protector + TCO")

    # ================= LT3652 =================
    U2 = s.comp("U2", "Battery_Management:LT3652EMSE", 287.02, 96.52,
                value="LT3652EMSE",
                footprint="Package_SO:MSOP-12-1EP_3x4.039mm_P0.65mm_EP1.651x2.845mm")
    # VBUS rail continues from POWER IN into VIN (pin 1, top)
    s.w((180.34, 55.88), (287.02, 55.88), (287.02, 83.82))
    # VIN caps
    for i, (x, v, fp) in enumerate([(246.38, "10uF", "C0805"),
                                    (256.54, "10uF", "C0805"),
                                    (266.70, "100nF", "C0603")]):
        c = s.C(f"C10{i}", x, 62.23, v, fp=fp)
        s.pw(c, "1", ("y", 55.88))
        s.gnd(c, "2", drop=0)
    # SHDN tied to VBUS (enabled)
    s.pw(U2, "3", ("x", 251.46), ("y", 55.88))
    # VIN_REG UVLO divider: VBUS -> R10 -> node -> R11 -> GND, node -> pin 2
    R10 = s.R("R10", 236.22, 80.01, "316k")
    s.pw(R10, "1", ("y", 55.88))
    R11 = s.R("R11", 236.22, 97.79, "100k")
    s.route(R10, "2", R11, "1", "V")
    s.pw(R11, "1", ("px", U2, "2"))          # lane y=93.98
    s.gnd(R11, "2", drop=0)
    # CHRG / FAULT open-drain -> pull-ups + labels (to MCP23017 GPB1/GPB2),
    # dropped below the pin lanes so the pull-ups sit in free space
    s.pw(U2, "4", ("x", 254.00), ("y", 116.84), ("x", 233.68))
    s.glabel_at("CHRG", 233.68, 116.84, 180)
    R12 = s.R("R12", 243.84, 113.03, "10k")
    s.rail(R12, "1", "+3V3", rise=0)
    s.pw(U2, "5", ("x", 256.54), ("y", 135.89), ("x", 243.84))
    s.glabel_at("FAULT", 243.84, 135.89, 180)
    R13 = s.R("R13", 248.92, 130.81, "10k")   # pull-up above the FAULT run
    s.rail(R13, "1", "+3V3", rise=2.54)
    s.pw(R13, "2", ("dy", 1.27))
    # TIMER cap
    C104 = s.C("C104", 269.24, 115.57, "100nF")
    s.pw(U2, "6", ("x", 269.24), ("pin", C104, "1"))
    s.gnd(C104, "2", drop=0)
    s.gnd(U2, "13")

    # ---- buck power stage (right); VBAT bus at x=358.14 ----
    L1 = s.L("L1", 316.23, 88.90, "10uH", rot=90)
    R18 = s.R("R18", 328.93, 88.90, "0.1R 1W", fp="R2010", rot=90)
    s.pw(U2, "12", ("px", L1, "1"))                      # SW lane
    s.route(L1, "2", R18, "1", "H")                      # M node
    s.pw(R18, "2", ("x", 358.14))                        # -> VBAT bus
    # SENSE taps the M node (between L1 and R_SENSE)
    s.pw(U2, "10", ("x", 322.58), ("y", 88.90))
    # catch diode SW -> GND
    D11 = s.D_schottky("D11", 302.26, 110.49, "B340A", rot=270)
    s.pw(D11, "1", ("y", 88.90))
    s.gnd(D11, "2", drop=0)
    # bootstrap: BOOST -> C108 -> SW; refresh diode D10 BOOST<-VBAT
    C108 = s.C("C108", 307.34, 85.09, "1uF")             # pin2 on SW lane
    s.pw(U2, "11", ("x", 304.80), ("y", 81.28), ("px", C108, "1"))
    D10 = s.D("D10", 321.31, 81.28, "BAT46")
    s.route(C108, "1", D10, "1", "H")
    s.pw(D10, "2", ("x", 363.22))
    s.pwr_flag(363.22, 78.74)
    s.w((363.22, 78.74), (363.22, 81.28))
    # BAT pin -> bus
    s.pw(U2, "9", ("x", 358.14))
    # VBAT bus + tap
    s.w((358.14, 81.28), (358.14, 212.09))
    s.power_at(358.14, 78.74, "VBAT")
    s.w((358.14, 78.74), (358.14, 81.28))
    # NTC chain (thermistor mounts against the 18650 holder)
    RT1 = s.ntc("RT1", 313.69, 113.03, "10k")
    s.pw(U2, "8", ("x", 313.69), ("pin", RT1, "1"))
    R17 = s.R("R17", 313.69, 120.65, "909R")
    s.gnd(R17, "2", drop=0)
    # VFB divider + full-charge (4.2 V) FET
    s.pw(U2, "7", ("x", 323.85), ("y", 121.92), ("x", 350.52))
    R15 = s.R("R15", 323.85, 125.73, "200k")
    s.gnd(R15, "2", drop=0)
    R14 = s.R("R14", 332.74, 118.11, "45.3k")
    s.pw(R14, "1", ("y", 111.76), ("x", 358.14))
    C105 = s.C("C105", 344.17, 118.11, "22pF")
    s.route(R14, "1", C105, "1", "H")
    R16 = s.R("R16", 350.52, 125.73, "976k")
    Q1 = s.nmos("Q1", 347.98, 134.62, "2N7002")
    s.gnd(Q1, "2", drop=0)                               # source
    s.pw(Q1, "1", ("x", 320.04))                         # gate
    s.glabel_at("FULLCHG_EN", 320.04, 134.62, 180)
    # gate pulldown: MCP23017 is hi-Z at POR -> without it the gate floats
    # and the 4.05 V "fixed in HW" charge cap isn't guaranteed at boot.
    # x=336.55 keeps its texts clear of Q1's "2N7002" value string.
    R24 = s.R("R24", 336.55, 139.70, "100k",
              refpos=(335.53, 138.30, "right"), valpos=(335.53, 141.10, "right"))
    s.pw(R24, "1", ("y", 134.62))
    s.gnd(R24, "2", drop=0)
    # bus-side caps + TVS
    C106 = s.C("C106", 364.49, 110.49, "10uF", fp="C0805")
    s.pw(C106, "1", ("x", 358.14))
    s.gnd(C106, "2", drop=0)
    C107 = s.CP("C107", 372.11, 110.49, "100uF")
    s.route(C107, "1", C106, "1", "H")
    s.gnd(C107, "2", drop=0)
    D12 = s.D_tvs("D12", 382.27, 110.49, "SMAJ5.0A", rot=270)
    s.route(D12, "1", C107, "1", "H")
    s.gnd(D12, "2", drop=0)

    s.text("Float 4.05 V (R14/R15); FULLCHG_EN -> 4.20 V full-charge mode", 250, 145, size=1.3)
    s.text("I_CHG = 1.0 A (R18);  RT1 = NCP18XH103 on the holder, 0..45 C window;  ~3 h timer", 250, 150, size=1.3)
    s.text("Charges only on the 15 V contract (UVLO 11.2 V); runs with no cell.", 250, 155, size=1.3)

    # VBAT staircase down to the RAILS block (joined by the battery below);
    # the column sits at 175.26, clear of the battery frame edge (x=170)
    s.w((358.14, 212.09), (175.26, 212.09))

    # ================= BATTERY + PROTECTION =================
    BT1 = s.comp("BT1", "Connector_Generic:Conn_01x02", 48.26, 203.20,
                 mirror="y", value="18650 (Keystone 1043)",
                 footprint="clock:BatteryHolder_Keystone_1043_1x18650",
                 refpos=(48.26, 197.49, None), valpos=(38.10, 210.82, None))
    s.text("Li-ion 18650 ONLY - 2.5-4.2 V", 25, 190, size=1.3, bold=True)
    # reverse P-FET: S=holder+, D=VBAT, G->100k->GND
    Q2 = s.comp("Q2", "clock:AO3401A", 78.74, 200.66, rot=270, value="AO3401A",
                footprint="Package_TO_SOT_SMD:SOT-23",
                refpos=(69.85, 196.85, None), valpos=(92.71, 196.85, None))
    s.route(BT1, "1", Q2, "2", "H")
    R19 = s.R("R19", 99.06, 194.31, "100k")
    s.pw(Q2, "1", ("y", 190.50), ("px", R19, "1"))
    s.gnd(R19, "2", drop=0)
    # VBAT out -> joins the charger staircase at (175.26, 212.09)
    s.pw(Q2, "3", ("x", 175.26), ("y", 212.09))
    # Vbat ADC divider + disconnect FET (taps the CELL side of the P-FET)
    R22 = s.R("R22", 91.44, 209.55, "100k")
    s.pw(R22, "1", ("x", 66.04), ("y", 203.20))
    s.junction(66.04, 203.20)
    R23 = s.R("R23", 91.44, 219.71, "100k")
    s.route(R22, "2", R23, "1", "V")
    C110 = s.C("C110", 102.87, 217.17, "100nF")
    s.pw(C110, "1", ("px", R22, "2"))
    s.gnd(C110, "2", drop=0)
    s.pw(R22, "2", ("x", 83.82))
    s.glabel_at("VBAT_SENSE", 83.82, 213.36, 180)
    Q3 = s.nmos("Q3", 88.90, 228.60, "2N7002")
    s.route(R23, "2", Q3, "3", "V")
    s.gnd(Q3, "2", drop=0)
    s.pw(Q3, "1", ("x", 73.66))
    s.glabel_at("VBAT_DIV_EN", 73.66, 228.60, 180)
    # gate pulldown (expander hi-Z at POR): divider defaults to disconnected
    R25 = s.R("R25", 67.31, 233.68, "100k",
              refpos=(66.29, 232.28, "right"), valpos=(66.29, 235.08, "right"))
    s.pw(R25, "1", ("y", 228.60), ("x", 73.66))
    s.gnd(R25, "2", drop=0)

    # cell- -> protector dual FET (S1) ; S2 -> TCO -> GND (PACK-)
    U4 = s.comp("U4", "clock:AOSD32334C", 80.01, 246.38, value="AOSD32334C",
                footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm")
    s.pw(BT1, "2", ("y", 245.11), ("px", U4, "3"))
    s.w((91.44, 242.57), (91.44, 250.19))                # D1//D1//D2//D2 tie
    F1 = s.fuse("F1", 102.87, 257.81, "TCO 77C",
                fp="clock:Fuse_TCO_Cantherm_SDF_L10.5mm_D4.0mm_P20.32mm_Horizontal",
                rot=90)
    s.pw(U4, "1", ("x", 60.96), ("y", 257.81), ("px", F1, "1"))
    s.pw(F1, "2", ("x", 111.76), ("dy", 2.54))
    s.power_at(111.76, 260.35, "GND")
    s.w((111.76, 257.81), (114.30, 257.81))
    s.pwr_flag(114.30, 257.81)                       # PACK- drives the GND net

    # HY2111 protector IC (replaced NRND AP9101CK6-BX 2026-07-17; same
    # pinout, R1/R2/C1 values per HYCON datasheet §10)
    U3 = s.comp("U3", "clock:HY2111", 80.01, 276.86,
                value="HY2111-GB",
                footprint="Package_TO_SOT_SMD:SOT-23-6")
    # VDD via R20 from cell+ (junction on the holder+ wire)
    R20 = s.R("R20", 58.42, 265.43, "100R")
    s.w((58.42, 203.20), (58.42, 261.62))
    s.junction(58.42, 203.20)
    s.pw(R20, "2", ("y", 269.24), ("px", U3, "5"))
    # C1 (VDD-VSS)
    C109 = s.C("C109", 46.99, 276.86, "100nF")
    s.pw(C109, "1", ("y", 269.24), ("x", 58.42))
    s.pw(C109, "2", ("y", 292.10), ("x", 55.88))
    s.pwr_flag(52.07, 269.24)                        # R20-fed VDD (ERC)
    s.pwr_flag(50.80, 292.10)                        # cell- node (ERC)
    # VSS -> cell- ; branch from the cell- wire
    s.w((55.88, 245.11), (55.88, 292.10))
    s.junction(55.88, 245.11)
    s.pw(U3, "6", ("y", 292.10), ("x", 55.88))
    # CS via R21 -> GND (pack-)
    R21 = s.R("R21", 62.23, 280.67, "2k")
    s.route(U3, "2", R21, "1", "H")
    s.gnd(R21, "2", drop=0)
    # OD -> G1 (discharge FET), OC -> G2 (charge FET)
    s.pw(U3, "1", ("x", 95.25), ("y", 238.76), ("x", 63.50), ("y", 242.57),
         ("px", U4, "4"))
    s.pw(U3, "3", ("x", 97.79), ("y", 253.99), ("x", 64.77), ("y", 247.65),
         ("px", U4, "2"))
    s.nc(U3, "4")

    s.text("cell- -> AOSD32334C (OD/OC gated) -> TCO 77 C -> PACK- (GND)", 20, 298, size=1.3)
    s.text("OV 4.28 V / OD 2.90 V fixed (-GB). Independent of the charger.", 20, 303, size=1.3)
