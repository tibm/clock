"""Sheet: Power In — USB-C receptacle + CH224K PD sink (request 15 V).
Produces the VBUS rail (feeds LT3652 VIN on the charger sheet) and PD_PG.
Native USB D+/D- pass straight through to the MCU (USB_DP/USB_DM);
CH224K DP/DM are shorted at the chip (PD-only mode).  Values: power_values.md §2.
"""
from sch import FP


def build(sch):
    sch.text("POWER IN — USB-C / USB-PD sink (CH224K, 15 V request)", 40, 30, size=2.0)

    # --- USB-C receptacle ---
    J1 = sch.comp("J1", "Connector:USB_C_Receptacle_USB2.0_16P", 70, 120,
                  value="USB4105-GF-A",
                  footprint="Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal")
    sch.power(J1, "A1", "GND")          # 4x GND stacked
    sch.power(J1, "SH", "GND")          # shield -> GND
    sch.net(J1, "A4", "VBUS")           # 4x VBUS stacked
    sch.net(J1, "A6", "USB_DP"); sch.net(J1, "B6", "USB_DP")   # tie A/B D+
    sch.net(J1, "A7", "USB_DM"); sch.net(J1, "B7", "USB_DM")   # tie A/B D-
    sch.net(J1, "A5", "CC1")
    sch.net(J1, "B5", "CC2")
    sch.nc(J1, "A8"); sch.nc(J1, "B8")  # SBU unused

    # --- VBUS transient clamp + power flag (VBUS is a source) ---
    Dtvs = sch.D_tvs("D1", 120, 150, "SMAJ22A", fp="SMA")
    sch.net(Dtvs, "1", "VBUS")
    sch.power(Dtvs, "2", "GND")

    # --- CH224K PD sink ---
    U1 = sch.comp("U1", "Interface_USB:CH224K", 200, 120, value="CH224K",
                  footprint="Package_SO:TSSOP-10_3x3mm_P0.5mm")
    # VDD: series R from VBUS + 1uF decouple (datasheet: "series resistance to VBUS")
    Rvdd = sch.R("R1", 165, 95, "1k")
    sch.net(Rvdd, "1", "VBUS")
    sch.net(Rvdd, "2", "CH_VDD")
    sch.net(U1, "1", "CH_VDD")
    Cvdd = sch.C("C1", 180, 150, "1uF", fp="C0603")
    sch.net(Cvdd, "1", "CH_VDD")
    sch.power(Cvdd, "2", "GND")
    # VBUS sense pin via 10k series
    Rvb = sch.R("R2", 165, 110, "10k")
    sch.net(Rvb, "1", "VBUS")
    sch.net(Rvb, "2", "CH_VBUS_S")
    sch.net(U1, "8", "CH_VBUS_S")
    # CFG1 -> 56k -> GND (request 15 V); CFG2/CFG3 float
    Rcfg = sch.R("R3", 235, 150, "56k")
    sch.net(Rcfg, "1", "CH_CFG1")
    sch.power(Rcfg, "2", "GND")
    sch.net(U1, "9", "CH_CFG1")
    sch.nc(U1, "2"); sch.nc(U1, "3")
    # CC1/CC2 to Type-C
    sch.net(U1, "7", "CC1")
    sch.net(U1, "6", "CC2")
    # DP/DM shorted at the CH224K (PD-only); NOT routed to the bus D+/-
    sch.net(U1, "4", "CH_DPDM")
    sch.net(U1, "5", "CH_DPDM")
    # PG open-drain -> 10k pull-up to 3V3 -> PD_PG (to expander)
    Rpg = sch.R("R4", 235, 95, "10k")
    sch.power(Rpg, "1", "+3V3")
    sch.net(Rpg, "2", "PD_PG")
    sch.net(U1, "10", "PD_PG")
    sch.power(U1, "11", "GND")

    sch.text("15 V PDO (CFG1=56k); falls back to 5 V. VBUS -> LT3652 VIN (charger sheet).",
             40, 200, size=1.3)
    sch.text("USB D+/D- -> MCU IO20/IO19 (native USB-JTAG). CH224K DP/DM shorted (PD-only).",
             40, 205, size=1.3)

    # --- ERC power-flags (one per source / derived-supply net; global labels
    #     connect them to the whole hierarchy) ---
    sch.text("Power flags (ERC):", 300, 92, size=1.2)
    flags = ["GND", "+3V3", "+5V", "+12V", "VBUS", "PVDD",
             "CH_VDD", "CH_APVDD", "CELLN"]
    for i, net in enumerate(flags):
        sch.flag(net, 305, 100 + i * 8)
