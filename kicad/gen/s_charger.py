"""Sheet: Charger & Battery Safety.
LT3652 1S buck charger (VBUS -> BAT node = VBAT, the always-on system rail),
plus the independent AP9101C + AOSD32334C protector, reverse P-FET, one-shot
TCO, NTC temp-qual, and the Vbat ADC divider.  Values: power_values.md §1/§6.

SAFETY NOTE: double-redundant OV/OD/OC/SC (LT3652 CV 4.05 V + AP9101C 4.28 V).
CO/DO -> charge/discharge FET-gate assignment per AP9101C datasheet; verify on bench.
"""
from sch import FP


def build(sch):
    sch.text("CHARGER & BATTERY SAFETY — LT3652 (BAT node = VBAT) + AP9101C/AOSD32334C protector",
             30, 25, size=2.0)

    # ================= LT3652 charger =================
    U2 = sch.comp("U2", "Battery_Management:LT3652EMSE", 200, 110,
                  value="LT3652EMSE",
                  footprint="Package_SO:MSOP-12-1EP_3x4.039mm_P0.65mm_EP1.651x2.845mm")
    # VIN <- VBUS + input caps
    sch.net(U2, "1", "VBUS")
    for i, v in enumerate(["10uF", "10uF", "100nF"]):
        c = sch.C(f"C10{i}", 150 + i * 8, 55, v, fp="C0805" if "10uF" in v else "C0603")
        sch.net(c, "1", "VBUS"); sch.power(c, "2", "GND")
    # VIN_REG UVLO divider 316k / 100k
    sch.net(U2, "2", "VIN_REG")
    Rin1 = sch.R("R10", 150, 95, "316k"); sch.net(Rin1, "1", "VBUS"); sch.net(Rin1, "2", "VIN_REG")
    Rin2 = sch.R("R11", 150, 110, "100k"); sch.net(Rin2, "1", "VIN_REG"); sch.power(Rin2, "2", "GND")
    # SHDN -> enabled (tie to VBUS)
    sch.net(U2, "3", "VBUS")
    # CHRG / FAULT open-drain status -> 10k pull-ups to 3V3 -> expander
    Rchg = sch.R("R12", 250, 70, "10k"); sch.power(Rchg, "1", "+3V3"); sch.net(Rchg, "2", "CHRG")
    sch.net(U2, "4", "CHRG")
    Rflt = sch.R("R13", 265, 70, "10k"); sch.power(Rflt, "1", "+3V3"); sch.net(Rflt, "2", "FAULT")
    sch.net(U2, "5", "FAULT")
    # TIMER cap
    Ctmr = sch.C("C104", 250, 150, "100nF"); sch.net(Ctmr, "1", "CH_TIMER"); sch.power(Ctmr, "2", "GND")
    sch.net(U2, "6", "CH_TIMER")
    # VFB float divider: RFB1 45.3k (VBAT->VFB), RFB2 200k (VFB->GND), CFF 22pF, + 4.2V full FET
    sch.net(U2, "7", "CH_VFB")
    Rfb1 = sch.R("R14", 250, 100, "45.3k"); sch.net(Rfb1, "1", "VBAT"); sch.net(Rfb1, "2", "CH_VFB")
    Rfb2 = sch.R("R15", 250, 120, "200k"); sch.net(Rfb2, "1", "CH_VFB"); sch.power(Rfb2, "2", "GND")
    Cff = sch.C("C105", 265, 100, "22pF"); sch.net(Cff, "1", "VBAT"); sch.net(Cff, "2", "CH_VFB")
    # 4.2V "full" mode: 976k || RFB2 switched by 2N7002 (gate = FULLCHG_EN)
    Rfull = sch.R("R16", 280, 120, "976k"); sch.net(Rfull, "1", "CH_VFB"); sch.net(Rfull, "2", "CH_FULLND")
    Qfull = sch.nmos("Q1", 280, 140, "2N7002")
    sch.net(Qfull, "3", "CH_FULLND")     # drain
    sch.power(Qfull, "2", "GND")         # source
    sch.net(Qfull, "1", "FULLCHG_EN")    # gate <- expander
    # NTC temp-qual: NTC(10k) + 909 series to GND (on holder)
    sch.net(U2, "8", "CH_NTC")
    RT1 = sch.ntc("RT1", 300, 95, "10k_NTC"); sch.net(RT1, "1", "CH_NTC"); sch.net(RT1, "2", "CH_NTC2")
    Rntc = sch.R("R17", 300, 115, "909R"); sch.net(Rntc, "1", "CH_NTC2"); sch.power(Rntc, "2", "GND")
    sch.text("NTC on the 18650 holder (0/45 C window)", 296, 80, size=1.1)
    # BAT node = VBAT (system rail) + bulk
    sch.net(U2, "9", "VBAT")
    Cbat1 = sch.C("C106", 225, 155, "10uF", fp="C0805"); sch.net(Cbat1, "1", "VBAT"); sch.power(Cbat1, "2", "GND")
    Cbat2 = sch.CP("C107", 240, 155, "100uF"); sch.net(Cbat2, "1", "VBAT"); sch.power(Cbat2, "2", "GND")
    # SENSE: R_SENSE 0.1R between SENSE and BAT
    sch.net(U2, "10", "CH_SENSE")
    Rsense = sch.R("R18", 215, 130, "0.1R", fp="R2010"); sch.net(Rsense, "1", "CH_SENSE"); sch.net(Rsense, "2", "VBAT")
    # BOOST cap + refresh diode
    sch.net(U2, "11", "CH_BOOST")
    Cbst = sch.C("C108", 185, 150, "1uF"); sch.net(Cbst, "1", "CH_BOOST"); sch.net(Cbst, "2", "CH_SW")
    Dref = sch.D("D10", 170, 130, "BAT46", fp="SOD123"); sch.net(Dref, "2", "VBAT"); sch.net(Dref, "1", "CH_BOOST")  # A=VBAT,K=BOOST
    # SW: inductor to VBAT, catch schottky to GND
    sch.net(U2, "12", "CH_SW")
    L1 = sch.L("L1", 175, 110, "10uH", fp="L4030"); sch.net(L1, "1", "CH_SW"); sch.net(L1, "2", "VBAT")
    Dcatch = sch.D_schottky("D11", 165, 150, "B340A", fp="SMA"); sch.net(Dcatch, "1", "CH_SW"); sch.power(Dcatch, "2", "GND")  # K=SW,A=GND
    sch.power(U2, "13", "GND")

    # ================= Battery holder + reverse P-FET =================
    BT1 = sch.comp("BT1", "Connector_Generic:Conn_01x02", 60, 110,
                   value="18650 holder (Keystone 1043)",
                   footprint="Battery:BatteryHolder_Keystone_1042_1x18650")
    sch.net(BT1, "1", "CELLP")    # cell +
    sch.net(BT1, "2", "CELLN")    # cell -
    sch.text("Li-ion 18650 ONLY  2.5-4.2 V", 45, 95, size=1.1)
    # reverse-polarity P-FET: S=CELLP, D=VBAT, G->100k->GND
    Qrev = sch.comp("Q2", "Transistor_FET:AO3401A", 110, 108, value="AO3401A",
                    footprint=FP["SOT23-3"])
    sch.net(Qrev, "2", "CELLP")   # source
    sch.net(Qrev, "3", "VBAT")    # drain
    sch.net(Qrev, "1", "CH_QREVG")
    Rrg = sch.R("R19", 125, 125, "100k"); sch.net(Rrg, "1", "CH_QREVG"); sch.power(Rrg, "2", "GND")

    # ================= Protector: AP9101C + AOSD32334C in cell- path =================
    U3 = sch.comp("U3", "Battery_Management:AP9101CK6", 70, 165,
                  value="AP9101CK6-BX", footprint=FP["SOT23-6"])
    sch.net(U3, "5", "CH_APVDD")  # VDD via 330 from CELLP
    Rap = sch.R("R20", 55, 150, "330R"); sch.net(Rap, "1", "CELLP"); sch.net(Rap, "2", "CH_APVDD")
    Cap = sch.C("C109", 88, 150, "100nF"); sch.net(Cap, "1", "CH_APVDD"); sch.net(Cap, "2", "CELLN")
    sch.net(U3, "6", "CELLN")     # VSS = cell-
    sch.net(U3, "2", "CH_VM")     # VM via 2.7k to PACK- (GND)
    Rvm = sch.R("R21", 95, 180, "2.7k"); sch.net(Rvm, "1", "CH_VM"); sch.power(Rvm, "2", "GND")
    sch.net(U3, "1", "CH_DO")     # discharge FET gate
    sch.net(U3, "3", "CH_CO")     # charge FET gate
    sch.nc(U3, "4")

    U4 = sch.comp("U4", "clock:AOSD32334C", 130, 175, value="AOSD32334C",
                  footprint="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm")
    # M1 (discharge): S1=CELLN, G1=DO, D1=common; M2 (charge): S2=PACKN2, G2=CO, D2=common
    sch.net(U4, "3", "CELLN")     # S1
    sch.net(U4, "4", "CH_DO")     # G1
    sch.net(U4, "1", "CH_PACKN2") # S2
    sch.net(U4, "2", "CH_CO")     # G2
    sch.net(U4, "5", "CH_FETMID"); sch.net(U4, "6", "CH_FETMID")   # D1 common
    sch.net(U4, "7", "CH_FETMID"); sch.net(U4, "8", "CH_FETMID")   # D2 common
    # one-shot TCO between M2 source and system GND (PACK-)
    F1 = sch.fuse("F1", 160, 200, "TCO 77C (SDF-DF077S)",
                  fp="")
    sch.net(F1, "1", "CH_PACKN2"); sch.power(F1, "2", "GND")
    sch.text("TCO one-shot thermal fuse in series with the cell -", 150, 215, size=1.1)

    # ================= Vbat ADC divider (100k/100k + disconnect FET) =================
    Rd1 = sch.R("R22", 40, 175, "100k"); sch.net(Rd1, "1", "CELLP"); sch.net(Rd1, "2", "VBAT_SENSE")
    Rd2 = sch.R("R23", 40, 195, "100k"); sch.net(Rd2, "1", "VBAT_SENSE"); sch.net(Rd2, "2", "CH_DIVND")
    Cdiv = sch.C("C110", 55, 190, "100nF"); sch.net(Cdiv, "1", "VBAT_SENSE"); sch.power(Cdiv, "2", "GND")
    Qdiv = sch.nmos("Q3", 30, 210, "2N7002")
    sch.net(Qdiv, "3", "CH_DIVND"); sch.power(Qdiv, "2", "GND"); sch.net(Qdiv, "1", "VBAT_DIV_EN")
    sch.text("Vbat -> ADC IO1 (VBAT_SENSE); FET gate = VBAT_DIV_EN (off in deep-sleep)",
             30, 225, size=1.1)

    # BAT transient clamp
    Dbat = sch.D_tvs("D12", 100, 140, "SMAJ5.0A", fp="SMA")
    sch.net(Dbat, "1", "VBAT"); sch.power(Dbat, "2", "GND")
