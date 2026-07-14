"""Sheet: Display & microSD (shared SPI2 bus).
Sharp LS032B7DD02 via FH34 10-pin FPC (software VCOM: EXTMODE=L, EXTCOMIN unused).
microSD DM3AT in SPI mode. Display is write-only (no MISO); SD owns MISO.
"""
from sch import FP


def build(sch):
    sch.text("DISPLAY (LS032B7DD02, FH34 FPC) + microSD (DM3AT) — shared SPI2",
             30, 25, size=2.0)

    # ===== Display FPC connector (FH34SRJ-10S-0.5SH) =====
    # Pinout: 1 SCLK 2 SI 3 SCS 4 EXTCOMIN 5 DISP 6 VDDA 7 VDD 8 EXTMODE 9 VSS 10 VSSA
    JD = sch.comp("J5", "Connector_Generic:Conn_01x10", 90, 130,
                  value="LS032B7DD02 (FH34SRJ-10S-0.5SH)",
                  footprint="Connector_FFC-FPC:Amphenol_F32Q-1A7x1-11010_1x10-1MP_P0.5mm_Horizontal")
    sch.net(JD, "1", "SPI_SCLK")
    sch.net(JD, "2", "SPI_MOSI")   # SI
    sch.net(JD, "3", "LCD_CS")     # SCS (active-HIGH)
    sch.power(JD, "4", "GND")      # EXTCOMIN unused (software VCOM)
    sch.net(JD, "5", "LCD_DISP")   # DISP (from expander)
    sch.power(JD, "6", "+5V")      # VDDA
    sch.power(JD, "7", "+5V")      # VDD
    sch.power(JD, "8", "GND")      # EXTMODE = LOW (software VCOM)
    sch.power(JD, "9", "GND")      # VSS
    sch.power(JD, "10", "GND")     # VSSA
    Cvdd = sch.C("C220", 70, 175, "1uF"); sch.power(Cvdd, "1", "+5V"); sch.power(Cvdd, "2", "GND")
    Cvda = sch.C("C221", 82, 175, "1uF"); sch.power(Cvda, "1", "+5V"); sch.power(Cvda, "2", "GND")
    sch.text("EXTMODE=GND, EXTCOMIN unused -> software VCOM (frame-inversion bit).",
             40, 195, size=1.2)

    # ===== microSD (DM3AT-SF-PEJM5) in SPI mode =====
    JS = sch.comp("J6", "Connector:Micro_SD_Card_Det_Hirose_DM3AT", 250, 130,
                  value="DM3AT-SF-PEJM5",
                  footprint="Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5")
    sch.net(JS, "5", "SPI_SCLK")   # CLK
    sch.net(JS, "3", "SPI_MOSI")   # CMD = MOSI
    sch.net(JS, "7", "SPI_MISO")   # DAT0 = MISO
    sch.net(JS, "2", "SD_CS")      # DAT3/CD = CS
    sch.power(JS, "4", "+3V3")     # VDD
    sch.power(JS, "6", "GND")      # VSS
    sch.power(JS, "SH", "GND")     # shield
    sch.nc(JS, "9"); sch.nc(JS, "10")  # card-detect switch (no spare GPIO)
    # SD_CS ext pull-up (idle at boot), DAT1/DAT2 pull-ups (avoid SD-mode entry)
    Rcs = sch.R("R70", 210, 100, "10k"); sch.power(Rcs, "1", "+3V3"); sch.net(Rcs, "2", "SD_CS")
    Rd1 = sch.R("R71", 300, 100, "10k"); sch.power(Rd1, "1", "+3V3"); sch.net(Rd1, "2", "SD_DAT1")
    Rd2 = sch.R("R72", 312, 100, "10k"); sch.power(Rd2, "1", "+3V3"); sch.net(Rd2, "2", "SD_DAT2")
    sch.net(JS, "8", "SD_DAT1"); sch.net(JS, "1", "SD_DAT2")
    Csd = sch.C("C222", 300, 175, "10uF", fp="C0805"); sch.power(Csd, "1", "+3V3"); sch.power(Csd, "2", "GND")
    Csd2 = sch.C("C223", 312, 175, "100nF"); sch.power(Csd2, "1", "+3V3"); sch.power(Csd2, "2", "GND")
    sch.text("Shared SPI2: LCD (LSB-first, CS active-HIGH, <=2 MHz) + SD (MSB, CS low, ~25 MHz).",
             210, 195, size=1.2)
