"""Sheet: Charger & Battery Safety.
LT3652 buck charger (BAT node = VBAT) + AP9101C/AOSD32334C protector, reverse
P-FET, one-shot TCO, NTC, Vbat divider.  Values: power_values.md §1/§6.
Parts are clustered at their pins so on-page nets are short wires.

SAFETY: double-redundant OV/OD/OC/SC (LT3652 CV 4.05 V + AP9101C 4.28 V).
CO/DO -> charge/discharge FET-gate assignment per AP9101C datasheet; verify on bench.
"""
from sch import FP


def build(sch):
    sch.text("CHARGER & BATTERY SAFETY  —  LT3652 (BAT node = VBAT) + AP9101C/AOSD32334C protector",
             30, 20, size=2.0)

    # =============== LT3652 (centre) ===============
    U2 = sch.comp("U2", "Battery_Management:LT3652EMSE", 210, 150,
                  value="LT3652EMSE",
                  footprint="Package_SO:MSOP-12-1EP_3x4.039mm_P0.65mm_EP1.651x2.845mm")
    sch.node(U2, "1", "VBUS")            # VIN (top)
    sch.node(U2, "3", "VBUS")            # SHDN -> enabled
    sch.node(U2, "13", "GND")            # GND (bottom)

    # VIN input caps (above VIN)
    for i, v in enumerate(["10uF", "10uF", "100nF"]):
        c = sch.C(f"C10{i}", 188 + i * 9, 120, v, fp="C0805" if "uF" in v else "C0603")
        sch.node(c, "1", "VBUS"); sch.node(c, "2", "GND")

    # VIN_REG UVLO divider (left of pin 2), stacked vertical
    Rin1 = sch.R("R10", 176, 132, "316k"); sch.node(Rin1, "1", "VBUS"); sch.node(Rin1, "2", "VIN_REG")
    Rin2 = sch.R("R11", 176, 145, "100k"); sch.node(Rin2, "1", "VIN_REG"); sch.node(Rin2, "2", "GND")
    sch.node(U2, "2", "VIN_REG")
    # CHRG / FAULT (left) -> pull-ups + cross-sheet labels
    R12 = sch.R("R12", 168, 158, "10k"); sch.node(R12, "1", "+3V3"); sch.node(R12, "2", "CHRG")
    sch.node(U2, "4", "CHRG")
    R13 = sch.R("R13", 155, 158, "10k"); sch.node(R13, "1", "+3V3"); sch.node(R13, "2", "FAULT")
    sch.node(U2, "5", "FAULT")
    # TIMER cap (left)
    C104 = sch.C("C104", 176, 168, "100nF"); sch.node(C104, "1", "CH_TIMER"); sch.node(C104, "2", "GND")
    sch.node(U2, "6", "CH_TIMER")

    # SW (right top) -> inductor to VBAT + catch diode
    L1 = sch.L("L1", 245, 142.4, "10uH", fp="L4030")
    L1.rot = 90                          # horizontal
    sch.node(U2, "12", "CH_SW"); sch.node(L1, "1", "CH_SW"); sch.node(L1, "2", "VBAT")
    Dcatch = sch.D_schottky("D11", 231, 128, "B340A", fp="SMA")  # K(1)=SW top, A(2)=GND
    sch.node(Dcatch, "1", "CH_SW"); sch.node(Dcatch, "2", "GND")
    # BOOST (right) -> cap to SW + refresh diode from VBAT
    Cbst = sch.C("C108", 233, 137.5, "1uF"); Cbst.rot = 90
    sch.node(U2, "11", "CH_BOOST"); sch.node(Cbst, "1", "CH_BOOST"); sch.node(Cbst, "2", "CH_SW")
    Dref = sch.D("D10", 246, 137.5, "BAT46", fp="SOD123"); Dref.rot = 90  # K(1)=BOOST,A(2)=VBAT
    sch.node(Dref, "1", "CH_BOOST"); sch.node(Dref, "2", "VBAT")
    # SENSE (right) -> R_SENSE to VBAT
    Rsense = sch.R("R18", 233, 150, "0.1R", fp="R2010"); Rsense.rot = 90
    sch.node(U2, "10", "CH_SENSE"); sch.node(Rsense, "1", "CH_SENSE"); sch.node(Rsense, "2", "VBAT")
    # BAT (right) -> VBAT + bulk
    sch.node(U2, "9", "VBAT")
    Cbat1 = sch.C("C106", 250, 162, "10uF", fp="C0805"); sch.node(Cbat1, "1", "VBAT"); sch.node(Cbat1, "2", "GND")
    Cbat2 = sch.CP("C107", 262, 162, "100uF"); sch.node(Cbat2, "1", "VBAT"); sch.node(Cbat2, "2", "GND")
    # NTC (right) -> NTC + 909 to GND
    RT1 = sch.ntc("RT1", 240, 170, "10k_NTC"); sch.node(U2, "8", "CH_NTC"); sch.node(RT1, "1", "CH_NTC"); sch.node(RT1, "2", "CH_NTC2")
    R17 = sch.R("R17", 240, 184, "909R"); sch.node(R17, "1", "CH_NTC2"); sch.node(R17, "2", "GND")
    # VFB (right bottom) -> divider + 4.2V full FET
    sch.node(U2, "7", "CH_VFB")
    Rfb1 = sch.R("R14", 258, 178, "45.3k"); sch.node(Rfb1, "1", "VBAT"); sch.node(Rfb1, "2", "CH_VFB")
    Rfb2 = sch.R("R15", 258, 192, "200k"); sch.node(Rfb2, "1", "CH_VFB"); sch.node(Rfb2, "2", "GND")
    Cff = sch.C("C105", 271, 178, "22pF"); sch.node(Cff, "1", "VBAT"); sch.node(Cff, "2", "CH_VFB")
    Rfull = sch.R("R16", 273, 192, "976k"); sch.node(Rfull, "1", "CH_VFB"); sch.node(Rfull, "2", "CH_FULLND")
    Qfull = sch.nmos("Q1", 273, 206, "2N7002")
    sch.node(Qfull, "3", "CH_FULLND"); sch.node(Qfull, "2", "GND"); sch.node(Qfull, "1", "FULLCHG_EN")

    # =============== Battery + reverse P-FET (left block) ===============
    BT1 = sch.comp("BT1", "Connector_Generic:Conn_01x02", 55, 118,
                   value="18650 holder (Keystone 1043)",
                   footprint="Battery:BatteryHolder_Keystone_1042_1x18650")
    sch.node(BT1, "1", "CELLP"); sch.node(BT1, "2", "CELLN")
    sch.text("Li-ion 18650 ONLY  2.5-4.2 V", 40, 104, size=1.2)
    Qrev = sch.comp("Q2", "Transistor_FET:AO3401A", 95, 116, value="AO3401A", footprint=FP["SOT23-3"])
    sch.node(Qrev, "2", "CELLP"); sch.node(Qrev, "3", "VBAT"); sch.node(Qrev, "1", "CH_QREVG")
    Rrg = sch.R("R19", 110, 130, "100k"); sch.node(Rrg, "1", "CH_QREVG"); sch.node(Rrg, "2", "GND")
    Dbat = sch.D_tvs("D12", 128, 116, "SMAJ5.0A", fp="SMA"); Dbat.rot = 90
    sch.node(Dbat, "1", "VBAT"); sch.node(Dbat, "2", "GND")

    # Vbat ADC divider (below holder)
    Rd1 = sch.R("R22", 55, 150, "100k"); sch.node(Rd1, "1", "CELLP"); sch.node(Rd1, "2", "VBAT_SENSE")
    Rd2 = sch.R("R23", 55, 164, "100k"); sch.node(Rd2, "1", "VBAT_SENSE"); sch.node(Rd2, "2", "CH_DIVND")
    Cdiv = sch.C("C110", 68, 157, "100nF"); sch.node(Cdiv, "1", "VBAT_SENSE"); sch.node(Cdiv, "2", "GND")
    Qdiv = sch.nmos("Q3", 55, 178, "2N7002")
    sch.node(Qdiv, "3", "CH_DIVND"); sch.node(Qdiv, "2", "GND"); sch.node(Qdiv, "1", "VBAT_DIV_EN")

    # =============== Protector AP9101C + AOSD32334C (bottom block) ===============
    U3 = sch.comp("U3", "Battery_Management:AP9101CK6", 95, 210, value="AP9101CK6-BX", footprint=FP["SOT23-6"])
    Rap = sch.R("R20", 74, 197, "330R"); sch.node(Rap, "1", "CELLP"); sch.node(Rap, "2", "CH_APVDD")
    sch.node(U3, "5", "CH_APVDD")
    Cap = sch.C("C109", 116, 197, "100nF"); sch.node(Cap, "1", "CH_APVDD"); sch.node(Cap, "2", "CELLN")
    sch.node(U3, "6", "CELLN")
    Rvm = sch.R("R21", 74, 220, "2.7k"); sch.node(Rvm, "1", "CH_VM"); sch.node(Rvm, "2", "GND"); sch.node(U3, "2", "CH_VM")
    sch.node(U3, "1", "CH_DO"); sch.node(U3, "3", "CH_CO"); sch.nc(U3, "4")

    U4 = sch.comp("U4", "clock:AOSD32334C", 160, 212, value="AOSD32334C",
                  footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm")
    sch.node(U4, "3", "CELLN"); sch.node(U4, "4", "CH_DO")
    sch.node(U4, "1", "CH_PACKN2"); sch.node(U4, "2", "CH_CO")
    for pd in ("5", "6", "7", "8"):
        sch.node(U4, pd, "CH_FETMID")
    F1 = sch.fuse("F1", 195, 220, "TCO 77C (SDF-DF077S)", fp=""); F1.rot = 90
    sch.node(F1, "1", "CH_PACKN2"); sch.node(F1, "2", "GND")

    sch.text("Cell- path: BATT- -> AOSD32334C (DO/CO gated) -> TCO 77C -> GND (PACK-).", 40, 240, size=1.2)
    sch.text("Vbat -> ADC IO1 (VBAT_SENSE); FULLCHG_EN=4.2V mode; VBAT_DIV_EN gates the divider.",
             40, 246, size=1.2)
