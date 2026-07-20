"""Block: POWER IN — USB-C receptacle + CH224K PD sink (15 V request).
Frame (15,40)-(170,155). Produces the VBUS rail (wire, continues right into
the charger block at y=55.88) and PD_PG / USB_DP / USB_DM labels.
"""
U = 2.54


def build(s):
    s.frame(15, 40, 170, 152, "POWER IN — USB-C PD sink (15 V)")

    # --- USB-C receptacle (VERTICAL since 2026-07-19: stands on the PCB,
    # port exits the back wall; Same Sky UJ20-C-V-C-2 = 16 TH pins for the
    # 1.6 mm board. In-stock SMT alt: GCT USB4145-03-0170-C) ---
    J1 = s.comp("J1", "Connector:USB_C_Receptacle_USB2.0_16P", 40.64, 86.36,
                value="UJ20-C-V-C-2-SMT-TR",
                footprint="clock:SameSky_UJ20-C-V-C-2_USB_C_Vertical_TH")
    # VBUS pins -> up to the VBUS rail at y 55.88
    s.pw(J1, "A4", ("y", 55.88))
    # CC1/CC2 straight across to CH224K
    # D+/D- pin pairs joined, then global labels to the MCU
    s.w((55.88, 83.82), (55.88, 86.36))     # A7-B7 (D-)
    s.w((55.88, 88.90), (55.88, 91.44))     # A6-B6 (D+)
    s.w((55.88, 83.82), (60.96, 83.82))
    s.glabel_at("USB_DM", 60.96, 83.82, 0)
    s.w((55.88, 88.90), (60.96, 88.90))
    s.glabel_at("USB_DP", 60.96, 88.90, 0)
    s.nc(J1, "A8")
    s.nc(J1, "B8")
    s.gnd(J1, "A1", via=0)                  # GND pins (stacked)
    s.pw(J1, "SH", ("dy", 2.54), ("px", J1, "A1"))  # shield -> same GND drop
    s.text("USB 2.0 16-pin; D+/D- = native USB-JTAG (IO19/20)", 20, 146, size=1.3)
    s.text("Vertical receptacle (TH pins) - port straight out the back wall.", 20, 150.5, size=1.3)

    # --- VBUS TVS clamp (pin 1 = top after rot 270; pad 1 = cathode) ---
    D1 = s.D_tvs("D1", 63.5, 63.5, "SMAJ22A", rot=270)
    s.pw(D1, "1", ("y", 55.88))
    s.gnd(D1, "2", drop=2.54, via=0)

    # --- CH224K ---
    U1 = s.comp("U1", "Interface_USB:CH224K", 106.68, 81.28, value="CH224K",
                footprint="Package_SO:TSSOP-10_3x3mm_P0.5mm",
                refpos=(96.52, 64.77, "right"), valpos=(96.52, 67.31, "right"))
    s.route(J1, "A5", U1, "7", "H")         # CC1
    s.route(J1, "B5", U1, "6", "H")         # CC2
    s.route(U1, "5", U1, "4", "V")          # DM-DP shorted (PD-only mode)
    s.gnd(U1, "11")                          # GND

    # VBUS sense via 10k from the rail
    R2 = s.R("R2", 106.68, 63.5, "10k")
    s.pw(R2, "1", ("y", 55.88))
    s.route(R2, "2", U1, "8", "V")
    # VDD via 1k from the rail + 1uF
    R1 = s.R("R1", 114.3, 63.5, "1k")
    s.pw(R1, "1", ("y", 55.88))
    s.pw(R1, "2", ("y", 71.12), ("px", U1, "1"))
    C1 = s.C("C1", 140.97, 71.12, "1uF")
    s.pw(C1, "1", ("y", 67.31), ("px", R1, "2"))
    s.gnd(C1, "2", drop=1.27)
    s.pwr_flag(121.92, 67.31)                        # R1-fed VDD (ERC)
    # PG -> label + 10k pull-up to 3V3
    s.pw(U1, "10", ("x", 128.27), ("y", 93.98), ("x", 134.62))
    s.glabel_at("PD_PG", 134.62, 93.98, 0)
    R4 = s.R("R4", 132.08, 90.17, "10k")
    s.rail(R4, "1", "+3V3", rise=0)
    s.jpin(R4, "2")                          # bottom lands on the PG run
    # CFG1 -> 56k -> GND (15 V request); CFG2/3 float
    R3 = s.R("R3", 121.92, 85.09, "56k")
    s.route(U1, "9", R3, "1", "HV")
    s.gnd(R3, "2", drop=0)
    s.nc(U1, "2")
    s.nc(U1, "3")

    # --- VBUS rail: J1 -> D1 -> R2/R1 -> onward to charger (drawn there) ---
    s.w((55.88, 55.88), (180.34, 55.88))
    s.junction(63.5, 55.88)
    s.junction(106.68, 55.88)
    s.junction(114.3, 55.88)
    # rail tap + ERC flag
    s.power_at(96.52, 53.34, "VBUS")
    s.w((96.52, 53.34), (96.52, 55.88))
    s.junction(96.52, 55.88)
    s.pwr_flag(101.6, 53.34)
    s.w((101.6, 53.34), (101.6, 55.88))
    s.junction(101.6, 55.88)

    s.text("CFG1=56k -> 15 V PDO; falls back to 5 V (charger idles <11.2 V).",
           20, 141, size=1.3)
    s.text("CH224K DP/DM shorted = PD-only; Type-C D± go to the MCU.",
           20, 136, size=1.3)
