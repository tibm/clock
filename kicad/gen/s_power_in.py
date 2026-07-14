"""Sheet: Power In — USB-C receptacle + CH224K PD sink (request 15 V).
Produces VBUS (-> LT3652 VIN) and PD_PG. Native USB D+/D- pass to the MCU.
On-page nets are wired; only VBUS/PD_PG/USB_D* (cross-sheet) use labels.
"""
from sch import FP


def build(sch):
    sch.text("POWER IN  —  USB-C / USB-PD sink (CH224K, 15 V request)", 40, 25, size=2.2)

    # --- USB-C receptacle (left) ---
    J1 = sch.comp("J1", "Connector:USB_C_Receptacle_USB2.0_16P", 70, 120,
                  value="USB4105-GF-A",
                  footprint="Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal")
    sch.node(J1, "A1", "GND"); sch.node(J1, "SH", "GND")
    sch.node(J1, "A4", "VBUS")
    sch.node(J1, "A6", "USB_DP"); sch.node(J1, "B6", "USB_DP")
    sch.node(J1, "A7", "USB_DM"); sch.node(J1, "B7", "USB_DM")
    sch.node(J1, "A5", "CC1"); sch.node(J1, "B5", "CC2")
    sch.nc(J1, "A8"); sch.nc(J1, "B8")

    # --- VBUS clamp near the connector ---
    D1 = sch.D_tvs("D1", 110, 150, "SMAJ22A", fp="SMA")
    sch.node(D1, "1", "VBUS"); sch.node(D1, "2", "GND")

    # --- CH224K PD sink: placed 5.08 above J1 so CC1/CC2 are straight wires
    #     (J1 CC pins at lib y 10.16/7.62 align with U1 CC pins at 5.08/2.54) ---
    U1 = sch.comp("U1", "Interface_USB:CH224K", 210, 114.92, value="CH224K",
                  footprint="Package_SO:TSSOP-10_3x3mm_P0.5mm")
    sch.node(U1, "7", "CC1"); sch.node(U1, "6", "CC2")     # left side -> J1
    sch.node(U1, "4", "CH_DPDM"); sch.node(U1, "5", "CH_DPDM")  # DP/DM shorted
    sch.node(U1, "8", "CH_VBUS_S")                          # VBUS sense (top)
    sch.node(U1, "1", "CH_VDD")                             # VDD (top-right)
    sch.node(U1, "9", "CH_CFG1")                            # CFG1 (right)
    sch.nc(U1, "2"); sch.nc(U1, "3")                        # CFG2/3 float
    sch.node(U1, "10", "PD_PG")                             # PG (right) -> label
    sch.node(U1, "11", "GND")                              # GND (bottom)

    # VBUS -> 10k -> VBUS-sense pin (above U1)
    R2 = sch.R("R2", 210, 95, "10k")                        # vertical above pin 8
    sch.node(R2, "2", "CH_VBUS_S"); sch.node(R2, "1", "VBUS")
    # VBUS -> 1k -> VDD, + 1uF decouple (right of U1)
    R1 = sch.R("R1", 245, 100, "1k", fp="R0603")
    sch.node(R1, "1", "VBUS"); sch.node(R1, "2", "CH_VDD")
    C1 = sch.C("C1", 258, 120, "1uF")
    sch.node(C1, "1", "CH_VDD"); sch.node(C1, "2", "GND")
    # CFG1 -> 56k -> GND (below-right)
    R3 = sch.R("R3", 245, 135, "56k")
    sch.node(R3, "1", "CH_CFG1"); sch.node(R3, "2", "GND")
    # PG -> 10k pull-up to 3V3 (far right)
    R4 = sch.R("R4", 270, 100, "10k")
    sch.node(R4, "1", "+3V3"); sch.node(R4, "2", "PD_PG")

    sch.text("15 V PDO (CFG1=56k); falls back to 5 V.  VBUS -> LT3652 VIN (charger).",
             40, 205, size=1.4)
    sch.text("USB D+/D- -> MCU IO20/IO19 (native USB-JTAG).  CH224K DP/DM shorted (PD-only).",
             40, 212, size=1.4)
