"""Block: POWER IN — USB-C receptacle + CH224K PD sink (15 V request).
Frame (15,40)-(170,155). Produces the VBUS rail (wire, continues right into
the charger block at y=55.88) and PD_PG / USB_DP / USB_DM labels.
"""
U = 2.54


def build(s):
    s.frame(15, 40, 170, 152, "POWER IN — USB-C PD sink (15 V)")

    # --- USB-C receptacle (VERTICAL since 2026-07-19: stands on the PCB,
    # port exits the back wall). GCT USB4160-03-0230-C = 24-pin USB3.2
    # vertical, DigiKey-stocked; we use only the USB 2.0 subset, so the 16P
    # symbol stays and the SS pads (A2/A3/A10/A11/B2/B3/B10/B11) are simply
    # unconnected in the footprint. ---
    J1 = s.comp("J1", "Connector:USB_C_Receptacle_USB2.0_16P", 40.64, 86.36,
                value="USB4160-03-0230-C",
                footprint="clock:GCT_USB4160-03-0230-C_USB_C_Vertical")
    # VBUS pins -> up to the VBUS rail at y 55.88
    s.pw(J1, "A4", ("y", 55.88))
    # CC1/CC2 straight across to CH224K
    # D+/D- pin pairs joined, then through the USBLC6-2SC6 ESD array to the
    # MCU labels. Flow-through wiring (J1 side on pins 1/3, MCU side on the
    # internally-tied twins 6/4): the two net segments join INSIDE the part,
    # so the netlist keeps them distinct and routing must pass its pads.
    s.w((55.88, 83.82), (55.88, 86.36))     # A7-B7 (D-)
    s.w((55.88, 88.90), (55.88, 91.44))     # A6-B6 (D+)
    U16 = s.comp("U16", "clock:USBLC6-2SC6", 73.66, 99.06, value="USBLC6-2SC6",
                 footprint="Package_TO_SOT_SMD:SOT-23-6",
                 refpos=(78.74, 95.25, "left"), valpos=(78.74, 106.68, "left"))
    s.w((55.88, 83.82), (60.96, 83.82), (60.96, 99.06), (68.58, 99.06))    # D- -> I/O1
    s.w((55.88, 88.90), (58.42, 88.90), (58.42, 101.60), (68.58, 101.60))  # D+ -> I/O2
    s.pw(U16, "6", ("x", 83.82))
    s.glabel_at("USB_DM", 83.82, 99.06, 0)
    s.pw(U16, "4", ("x", 83.82))
    s.glabel_at("USB_DP", 83.82, 101.60, 0)
    s.gnd(U16, "2", drop=0)
    # VBUS pin NC ON PURPOSE: our VBUS is the 15 V PD contract, far above
    # this pin's 5.25 V rating. Floating, the array still clamps D+/D-
    # through its internal zener, and a cable VBUS-to-D± fault burns the
    # (sacrificial) array instead of pumping a system rail.
    s.nc(U16, "5")
    s.nc(J1, "A8")
    s.nc(J1, "B8")
    s.gnd(J1, "A1", via=0)                  # GND pins (stacked)
    s.pw(J1, "SH", ("dy", 2.54), ("px", J1, "A1"))  # shield -> same GND drop
    s.text("USB 2.0 subset of the 24-pin USB4160 (SS pads unconnected);", 20, 146, size=1.3)
    s.text("D+/D- = native USB-JTAG (IO19/20). Vertical - port out the back wall.", 20, 150.5, size=1.3)

    # --- VBUS TVS clamp. SMAJ22CA = BIDIRECTIONAL (2026-07-21: the D_TVS
    # symbol is bidirectional and the SMA footprint is then orientation-free;
    # the unidirectional SMAJ22A drawn with this symbol had no cathode
    # marking for assembly) ---
    D1 = s.D_tvs("D1", 63.5, 63.5, "SMAJ22CA", rot=270)
    s.pw(D1, "1", ("y", 55.88))
    s.gnd(D1, "2", drop=2.54, via=0)

    # --- CH224K ---
    # ESSOP-10 (WCH): 3.9x4.9 body, 1.0 pitch, GND = exposed belly pad ONLY
    # (pin 11) -- the stock symbol's default footprint. 2026-07-23: was
    # wrongly overridden to TSSOP-10_3x3 (no EP -> U1 had NO ground; wrong
    # pitch), caught by the PCB build's pad->net assignment.
    U1 = s.comp("U1", "Interface_USB:CH224K", 106.68, 81.28, value="CH224K",
                footprint="Package_SO:SSOP-10-1EP_3.9x4.9mm_P1mm_EP2.1x3.3mm",
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
