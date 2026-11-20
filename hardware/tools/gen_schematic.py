#!/usr/bin/env python3
"""Generate hardware/openpelletbridge/openpelletbridge.kicad_sch from the netlist below.

Every pin gets a short wire stub with a net label (or a power symbol), so the
connectivity is defined here, in one place, and checked by KiCad's ERC:

    python3 hardware/tools/gen_schematic.py
    kicad-cli sch erc hardware/openpelletbridge/openpelletbridge.kicad_sch

Library symbols come from the KiCad installation (KICAD_SHARE, default the macOS
app bundle) and are embedded in the schematic, as KiCad does itself.
"""
import math, os, uuid
import kisym
from kisym import Sym as S, dump

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "openpelletbridge", "openpelletbridge.kicad_sch")
PROJECT = "openpelletbridge"
NS = uuid.UUID("6f1c2a52-1d1b-4a58-9a77-1f0e4f6a0b11")   # stable UUIDs between runs

def uid(*parts):
    return str(uuid.uuid5(NS, "/".join(map(str, parts))))

# Pin types changed for this design: the TPS2116 has two VOUT pins (both power outputs on +5V),
# which ERC would report as two drivers on one net.
PIN_TYPE_OVERRIDE = {"Power_Management:TPS2116DRL": {"7": "passive"}}

POWER = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V"}   # nets drawn as power symbols
NC = None

# ── The circuit ───────────────────────────────────────────────────────────────
# (ref, lib_id, value, footprint, x, y, rotation, {pin number: net | NC}, {extra fields})
FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C805 = "Capacitor_SMD:C_0805_2012Metric"
FP_C1206 = "Capacitor_SMD:C_1206_3216Metric"
ESP_NC = {n: NC for n in ["5", "6", "7", "9", "10", "11", "13", "14", "15", "16", "17", "18", "19", "20",
                          "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38",
                          "39", "40", "41", "44"]}
GND_PINS = {n: "GND" for n in ["1", "2", "42", "43"] + [str(i) for i in range(46, 66)]}

PARTS = [
    # USB-C: power and the native USB of the ESP32-S3 (flashing, serial console)
    ("J1", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C", "Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal",
     35.56, 50.8, 0, {"A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS", "A5": "CC1", "B5": "CC2",
                      "A6": "USB_D+", "B6": "USB_D+", "A7": "USB_D-", "B7": "USB_D-", "A8": NC, "B8": NC,
                      "A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "SH": "GND"},
     {}),
    ("R1", "Device:R", "5.1k", FP_R, 76.2, 38.1, 0, {"1": "CC1", "2": "GND"}, {"Note": "USB-C sink: 5 V"}),
    ("R2", "Device:R", "5.1k", FP_R, 86.36, 38.1, 0, {"1": "CC2", "2": "GND"}, {}),
    ("U1", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
     81.28, 68.58, 0, {"1": "USB_D+", "6": "USB_D+", "3": "USB_D-", "4": "USB_D-", "5": "VBUS", "2": "GND"},
     {"Note": "ESD protection USB"}),
    ("F1", "Device:Polyfuse", "0.5A", "Fuse:Fuse_1206_3216Metric", 104.14, 38.1, 0,
     {"1": "VBUS", "2": "VBUS_F"}, {"Note": "PTC, hold 500 mA"}),
    # Power mux: the stove (VIN1, +5V_BUCK) has priority, USB (VIN2) takes over when the stove's 5 V
    # drops below ~4.3 V (PR1 divider, VREF 1.0 V +-8 %: 4.0 - 4.6 V). Break-before-make in ~8 us,
    # reverse current blocked both ways. ST high = powered by the stove (to IO4).
    ("U8", "Power_Management:TPS2116DRL", "TPS2116", "Package_TO_SOT_SMD:SOT-583-8", 121.92, 78.74, 0,
     {"3": "+5V_BUCK", "6": "VBUS_F", "2": "+5V", "7": "+5V", "1": "GND", "5": "+5V_BUCK", "4": "PR1", "8": "PWR_STOVE"},
     {"Note": "Power mux, priority mode: stove first, USB as fallback"}),
    ("R14", "Device:R", "33k", FP_R, 142.24, 78.74, 0, {"1": "+5V_BUCK", "2": "PR1"}, {"Note": "PR1 divider: switch to USB below ~4.3 V"}),
    ("R15", "Device:R", "10k", FP_R, 142.24, 91.44, 0, {"1": "PR1", "2": "GND"}, {}),
    ("C14", "Device:C", "1u", FP_C, 101.6, 78.74, 0, {"1": "VBUS_F", "2": "GND"}, {"Note": "Input of the mux, USB side"}),
    ("R16", "Device:R", "10k", FP_R, 154.94, 78.74, 0, {"1": "+3V3", "2": "PWR_STOVE"}, {"Note": "ST pull-up: high = powered by the stove"}),

    # Stove connector: Phoenix MC 1,5/4-G-3,5 header, cable plug FMC 1,5/4-ST-3,5 (push-in)
    ("J2", "Connector_Generic:Conn_01x04", "Stove", "openpelletbridge:KF2EDGR-3.5-4P",
     27.94, 127.0, 0,
     {"1": "GND_S", "2": "+5V_S", "3": "+20V_S", "4": "DATA_S"},
     {"Note": "Pin order of the stove varies: measure GND / 5 V / 20 V / DATA first. Cable plug: Kefa KF2EDGKD-3.5-4P (LCSC C441073, spring) with the Kefa header, Phoenix FMC 1,5/4-ST-3,5 (1952283, push-in) with the Phoenix header"}),
    ("F2", "Device:Polyfuse", "0.35A", "Fuse:Fuse_1206_3216Metric", 55.88, 111.76, 0,
     {"1": "+20V_S", "2": "+20V_F"}, {"Note": "PTC, hold 350 mA, 30 V: worst case ~2.9 W in = 0.24 A at 12 V"}),
    ("D8", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", 45.72, 104.14, 0,
     {"2": "+20V_F", "1": "+20V_P"}, {"Note": "Reverse polarity protection: a swapped stove cable gives no current (~0.3 V drop at 0.1 A)"}),
    ("D3", "Device:D_TVS", "SMF24CA", "Diode_SMD:D_SOD-123F", 66.04, 129.54, 90,
     {"1": "GND_S", "2": "+20V_P"}, {"Note": "TVS 24 V on the stove supply"}),
    ("JP1", "Jumper:SolderJumper_2_Open", "STOVE PWR", "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm",
     81.28, 111.76, 0, {"1": "+20V_P", "2": "VIN_BUCK"},
     {"Note": "Close JP1 + JP2: powered by the stove (not isolated)", "_bom": False}),
    ("JP2", "Jumper:SolderJumper_2_Open", "STOVE GND", "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm",
     81.28, 144.78, 0, {"1": "GND_S", "2": "GND"}, {"Note": "Open both: USB only, stove side isolated", "_bom": False}),
    ("C4", "Device:C", "10u 50V", FP_C1206, 99.06, 129.54, 0, {"1": "VIN_BUCK", "2": "GND"}, {}),
    ("C5", "Device:C", "100n", FP_C, 109.22, 129.54, 0, {"1": "VIN_BUCK", "2": "GND"}, {}),
    ("U2", "Regulator_Switching:AP63205WU", "AP63205WU", "Package_TO_SOT_SMD:TSOT-23-6",
     134.62, 116.84, 0, {"3": "VIN_BUCK", "2": "VIN_BUCK", "4": "GND", "5": "SW", "6": "BST", "1": "+5V_BUCK"},
     {"Note": "Step-down 3.8-32 V -> 5 V, 2 A"}),
    ("C6", "Device:C", "100n", FP_C, 160.02, 101.6, 0, {"1": "BST", "2": "SW"}, {"Note": "Bootstrap"}),
    ("L1", "Device:L", "6.8u", "Inductor_SMD:L_Bourns-SRN4018", 172.72, 116.84, 90,
     {"1": "SW", "2": "+5V_BUCK"}, {"Note": "check the AP63205 datasheet table"}),
    ("C7", "Device:C", "22u", FP_C805, 187.96, 129.54, 0, {"1": "+5V_BUCK", "2": "GND"}, {}),
    ("C8", "Device:C", "22u", FP_C805, 198.12, 129.54, 0, {"1": "+5V_BUCK", "2": "GND"}, {}),

    # 5 V -> 3.3 V
    ("C1", "Device:C", "22u", FP_C805, 139.7, 55.88, 0, {"1": "+5V", "2": "GND"}, {}),
    ("U3", "Regulator_Linear:AP2112K-3.3", "AP2112K-3.3", "Package_TO_SOT_SMD:SOT-23-5",
     162.56, 50.8, 0, {"1": "+5V", "3": "+5V", "2": "GND", "4": NC, "5": "+3V3"}, {"Note": "LDO 600 mA"}),
    ("C2", "Device:C", "10u", FP_C805, 182.88, 55.88, 0, {"1": "+3V3", "2": "GND"}, {}),
    ("C3", "Device:C", "100n", FP_C, 193.04, 55.88, 0, {"1": "+3V3", "2": "GND"}, {"Note": "Next to module pin 3"}),
    ("C15", "Device:C", "10u", FP_C805, 203.2, 55.88, 0, {"1": "+3V3", "2": "GND"}, {"Note": "Bulk at module pin 3 (WiFi peaks); C2 stays at the LDO"}),

    # ESP32-S3-MINI-1-N8
    ("U4", "RF_Module:ESP32-S3-MINI-1", "ESP32-S3-MINI-1-N8", "RF_Module:ESP32-S2-MINI-1",
     289.56, 88.9, 0,
     {**ESP_NC, **GND_PINS, "3": "+3V3", "45": "EN", "4": "BOOT", "12": "LED_DIN",
      "21": "STOVE_TX", "22": "STOVE_RX", "25": "BUZZER", "8": "PWR_STOVE", "23": "USB_D-", "24": "USB_D+"},
     {"Note": "8 MB flash, no PSRAM. Footprint of the pin-compatible S2-MINI-1; keep the antenna area free"}),
    ("R3", "Device:R", "10k", FP_R, 241.3, 50.8, 0, {"1": "+3V3", "2": "EN"}, {}),
    ("C9", "Device:C", "1u", FP_C, 251.46, 66.04, 0, {"1": "EN", "2": "GND"}, {"Note": "EN delay (Espressif: 10k / 1uF)"}),
    ("SW1", "Switch:SW_Push", "RESET", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", 241.3, 76.2, 90,
     {"1": "GND", "2": "EN"}, {}),
    ("R4", "Device:R", "10k", FP_R, 223.52, 50.8, 0, {"1": "+3V3", "2": "BOOT"}, {}),
    ("SW2", "Switch:SW_Push", "BOOT / USER", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", 223.52, 76.2, 90,
     {"1": "GND", "2": "BOOT"}, {"Note": "IO0: user button; held during reset = download mode"}),

    # Stove serial line: 5 V single wire (TX and RX on DATA), galvanically isolated
    ("R5", "Device:R", "220", FP_R, 55.88, 205.74, 0, {"1": "+3V3", "2": "TX_LED"},
     {"Note": "U5 LED ~9.5 mA: at CTR >= 100 % U5 sinks the stove's pull-up plus U6's LED current (2.5 mA). IO17 sinks ~10.7 mA in all"}),
    ("U5", "Isolator:LTV-357T", "LTV-357T", "Package_SO:SO-4_4.4x3.6mm_P2.54mm", 81.28, 220.98, 0,
     {"1": "TX_LED", "2": "STOVE_TX", "4": "DATA_S", "3": "GND_S"},
     {"Note": "TX: STOVE_TX low -> DATA low. CTR >= 100 %"}),
    ("R12", "Device:R", "10k", FP_R, 40.64, 205.74, 0, {"1": "+3V3", "2": "STOVE_TX"},
     {"Note": "Keeps TX idle (high) while the ESP boots: no garbage on the stove bus"}),
    ("R6", "Device:R", "1.5k", FP_R, 132.08, 198.12, 0, {"1": "RX_LED_K", "2": "DATA_S"}, {"Note": "U6 LED ~2.5 mA when DATA low (load on the stove's bus)"}),
    ("U6", "Isolator:LTV-357T", "LTV-357T", "Package_SO:SO-4_4.4x3.6mm_P2.54mm", 165.1, 220.98, 180,
     {"1": "+5V_S", "2": "RX_LED_K", "4": "STOVE_RX", "3": "GND"},
     {"Note": "RX: DATA low -> STOVE_RX low (also our own TX: echo = self-test)"}),
    ("R7", "Device:R", "10k", FP_R, 190.5, 205.74, 0, {"1": "+3V3", "2": "STOVE_RX"}, {"Note": "U6 sinks 0.33 mA + D6 0.8 mA = 1.1 mA of its 2.5 mA (CTR >= 100 %)"}),
    # Serial activity LED, driven by the lines themselves (no GPIO): red = TX, green = RX.
    # Our own TX is echoed on RX, so sending shows orange; the stove answering shows green.
    ("R10", "Device:R", "1.5k", FP_R, 220.98, 205.74, 0, {"1": "+3V3", "2": "LED_TX_A"}, {}),
    ("R11", "Device:R", "1.5k", FP_R, 231.14, 205.74, 0, {"1": "+3V3", "2": "LED_RX_A"}, {}),
    ("D5", "Device:LED", "red TX", "LED_SMD:LED_0603_1608Metric", 241.3, 220.98, 180,
     {"2": "LED_TX_A", "1": "STOVE_TX"}, {"MPN": "KT-0603R", "Note": "~0.8 mA"}),
    ("D6", "Device:LED", "green RX", "LED_SMD:LED_0603_1608Metric", 241.3, 231.14, 180,
     {"2": "LED_RX_A", "1": "STOVE_RX"}, {"MPN": "Hubei KENTO KT-0603YG", "Note": "yellow-green, Vf 2.0-2.2 V (an InGaN green needs ~3 V)"}),
    ("D4", "Device:D_TVS", "PESD5V0S1BB", "Diode_SMD:D_SOD-323", 104.14, 238.76, 90,
     {"1": "GND_S", "2": "DATA_S"}, {"Note": "ESD on the stove data line"}),

    # Status LEDs: 2x WS2812B-2020 in a chain on one GPIO, 3.3 V -> 5 V level shifter
    ("U7", "Logic_LevelTranslator:SN74LV1T34DBV", "SN74LV1T34", "Package_TO_SOT_SMD:SOT-23-5", 330.2, 165.1, 0,
     {"1": NC, "2": "LED_DIN", "3": "GND", "4": "LED_Y", "5": "+5V"},
     {"Note": "3.3 V -> 5 V for the LED data; works down to VCC 1.8 V (the 5 V rail can be ~4.6 V on USB)"}),
    ("R8", "Device:R", "10k", FP_R, 309.88, 182.88, 0, {"1": "LED_DIN", "2": "GND"}, {"Note": "LEDs stay dark during boot"}),
    ("C10", "Device:C", "100n", FP_C, 345.44, 152.4, 0, {"1": "+5V", "2": "GND"}, {}),
    ("R9", "Device:R", "33", FP_R, 375.92, 165.1, 90, {"1": "LED_Y", "2": "LED_D1"}, {}),
    ("LED1", "LED:WS2812B-2020", "WS2812B-2020", "LED_SMD:LED_WS2812B-2020_PLCC4_2.0x2.0mm", 335.28, 210.82, 0,
     {"3": "LED_D1", "1": "LED_D2", "4": "+5V", "2": "GND"}, {"Note": "System: WiFi / Bluetooth / telnet / power"}),
    ("LED2", "LED:WS2812B-2020", "WS2812B-2020", "LED_SMD:LED_WS2812B-2020_PLCC4_2.0x2.0mm", 375.92, 210.82, 0,
     {"3": "LED_D2", "1": NC, "4": "+5V", "2": "GND"}, {"Note": "Stove state"}),
    ("C11", "Device:C", "100n", FP_C, 355.6, 226.06, 0, {"1": "+5V", "2": "GND"}, {}),
    ("C12", "Device:C", "100n", FP_C, 396.24, 226.06, 0, {"1": "+5V", "2": "GND"}, {}),

    # Buzzer on IO21: magnetic 5 x 5 mm transducer (2 - 4 V, ~110 mA at 3 V) on 3.3 V, switched
    # by an N-MOSFET with a PWM signal (~4 kHz); flyback diode across the coil
    ("BZ1", "Device:Buzzer", "MLT-5020", "openpelletbridge:Buzzer_Huaneng_MLT-5020", 365.76, 55.88, 0,
     {"1": "+3V3", "2": "BZ_K"}, {"Note": "Magnetic transducer, needs a PWM signal (4 kHz); pad 3 is mechanical"}),
    ("D7", "Device:D", "1N4148W", "Diode_SMD:D_SOD-123", 386.08, 55.88, 90,
     {"1": "+3V3", "2": "BZ_K"}, {"Note": "Flyback diode across the buzzer coil"}),
    ("Q1", "Transistor_FET:AO3400A", "AO3400A", "Package_TO_SOT_SMD:SOT-23", 365.76, 81.28, 0,
     {"1": "BUZZER", "2": "GND", "3": "BZ_K"}, {"Note": "Logic-level N-MOSFET (Vgs(th) ~1.4 V)"}),
    ("R13", "Device:R", "10k", FP_R, 345.44, 88.9, 0, {"1": "BUZZER", "2": "GND"}, {"Note": "Buzzer silent while the ESP boots"}),
    ("C13", "Device:C", "10u", FP_C805, 401.32, 55.88, 0, {"1": "+3V3", "2": "GND"}, {"Note": "Buffer for the buzzer current pulses"}),

    # Test points: bare pads on the bottom side (nothing assembled there)
    *[("TP%d" % (i + 1), "Connector:TestPoint", name, "TestPoint:TestPoint_Pad_D1.5mm", 30.48 + 15.24 * i, 165.1, 0,
       {"1": net}, {"_bom": False}) for i, (name, net) in enumerate(
        [("3V3", "+3V3"), ("5V", "+5V"), ("GND", "GND"), ("TX", "STOVE_TX"), ("RX", "STOVE_RX"),
         ("20V", "+20V_P"), ("5V_S", "+5V_S"), ("DATA", "DATA_S"), ("GND_S", "GND_S")])],

    # M2 mounting holes in the corners, not plated (the screws touch no copper: the stove side stays isolated)
    *[("MH%d" % (i + 1), "Mechanical:MountingHole", "M2", "MountingHole:MountingHole_2.2mm_M2", 175.26 + 10.16 * i, 165.1, 0,
       {}, {"_bom": False}) for i in range(4)],
]

# LCSC part numbers (JLCPCB assembly). Values from JLCPCB's own BOM matching, or looked up
# on lcsc.com / jlcpcb.com (18/11/2026); stock and Basic / Extended status change, check
# them when ordering.
LCSC = {
    "C1": "C45783", "C7": "C45783", "C8": "C45783",            # 22u 25V X5R 0805 CL21A226MAQNNNE
    "C2": "C15850", "C15": "C15850",                           # 10u 25V X5R 0805 CL21A106KAYNNNE
    "C3": "C14663", "C5": "C14663", "C6": "C14663", "C10": "C14663", "C11": "C14663", "C12": "C14663",  # 100n 50V X7R 0603
    "C4": "C13585",                                             # 10u 50V X5R 1206 CL31A106KBHNNNE
    "C9": "C15849",                                             # 1u 0603 CL10A105KB8NNNC
    "R1": "C23186", "R2": "C23186",                             # 5.1k 0603
    "R3": "C25804", "R4": "C25804", "R8": "C25804", "R12": "C25804", "R7": "C25804",   # 10k 0603
    "R5": "C22962", "R6": "C22843", "R9": "C23140",                 # 220, 1.5k, 33
    "R10": "C22843", "R11": "C22843",                           # 1.5k 0603
    "D3": "C19077515", "D4": "C2687128",                        # SMF24CA, PESD5V0S1BB
    "D5": "C2286", "D6": "C2289",                               # KT-0603R red, KT-0603YG yellow-green (Vf ~2 V)
    "F1": "C315893", "F2": "C2153989",                          # Littelfuse 1206L050/15YR, 1206L035/30WR
    "L1": "C2044369",                                           # Bourns SRN4018-6R8M
    "J1": "C3020560",                                           # GCT USB4105-GF-A
    "J2": "C441173",                                            # Cixi Kefa KF2EDGR-3.5-4P (plug C441073, not on the board)
    "U1": "C2827654", "U2": "C2071056", "U3": "C23380830",      # USBLC6-2SC6, AP63205WU-7, AP2112K-3.3TRG1
    "U4": "C2913206",                                           # ESP32-S3-MINI-1-N8
    "U5": "C119089", "U6": "C119089",                           # LTV-357T
    "U7": "C100024", "U8": "C3235557",                          # SN74LV1T34DBVR, TPS2116DRLR
    "R14": "C4216", "R15": "C25804", "R16": "C25804", "C14": "C15849",   # 33k, 10k, 10k, 1u
    "LED1": "C5349955", "LED2": "C5349955",                     # XINGLIGHT XL-2020RGBC-WS2812B (pin-compatible)
    "SW1": "C318884", "SW2": "C318884",                         # XKB TS-1187A-B-A-B
    "D8": "C8598",                                              # JSCJ B5819W SL, 40 V 1 A Schottky SOD-123
    "BZ1": "C94598",                                            # Jiangsu Huaneng MLT-5020
    "D7": "C81598",                                             # 1N4148W SOD-123
    "Q1": "C20917",                                             # AOS AO3400A
    "R13": "C25804", "C13": "C15850",                           # 10k 0603, 10u 25V 0805
}

# Manufacturer and part number (MPN) for assemblers that buy at Mouser / DigiKey / Farnell
# (Aisler). Where the LCSC part is a Chinese-market equivalent, the MPN is a part with the
# same footprint and pinout: resistors (UNI-ROYAL at LCSC, YAGEO here), D5 / D6 (KENTO /
# Kingbright), F2 (TECHFUSE / Littelfuse), J2 (Kefa / Phoenix MC 1,5: its pins fit the
# 1.4 mm holes), LED1 / LED2 (Xinglight / Worldsemi, pin-compatible), SW1 / SW2 (XKB / C&K
# PTS526: same pads, pins 1-2 and 3-4 connected in both).
MPN = {
    "C1": ("Samsung Electro-Mechanics", "CL21A226MAQNNNE"), "C7": ("Samsung Electro-Mechanics", "CL21A226MAQNNNE"),
    "C8": ("Samsung Electro-Mechanics", "CL21A226MAQNNNE"), "C2": ("Samsung Electro-Mechanics", "CL21A106KAYNNNE"),
    "C15": ("Samsung Electro-Mechanics", "CL21A106KAYNNNE"),
    "C4": ("Samsung Electro-Mechanics", "CL31A106KBHNNNE"), "C9": ("Samsung Electro-Mechanics", "CL10A105KB8NNNC"),
    **{c: ("YAGEO", "CC0603KRX7R9BB104") for c in ("C3", "C5", "C6", "C10", "C11", "C12")},
    "R1": ("YAGEO", "RC0603FR-075K1L"), "R2": ("YAGEO", "RC0603FR-075K1L"),
    **{r: ("YAGEO", "RC0603FR-0710KL") for r in ("R3", "R4", "R7", "R8", "R12")},
    "R5": ("YAGEO", "RC0603FR-07220RL"), "R6": ("YAGEO", "RC0603FR-071K5L"),
    "R9": ("YAGEO", "RC0603FR-0733RL"), "R10": ("YAGEO", "RC0603FR-071K5L"), "R11": ("YAGEO", "RC0603FR-071K5L"),
    "D3": ("Littelfuse", "SMF24CA"), "D4": ("Nexperia", "PESD5V0S1BB,115"),
    "D5": ("Kingbright", "APT1608SURCK"), "D6": ("Kingbright", "APT1608CGCK"),
    "F1": ("Littelfuse", "1206L050/15YR"), "F2": ("Littelfuse", "1206L035/30WR"),
    "L1": ("Bourns", "SRN4018-6R8M"),
    "J1": ("GCT", "USB4105-GF-A"), "J2": ("Phoenix Contact", "1844236"),
    "U1": ("STMicroelectronics", "USBLC6-2SC6"), "U2": ("Diodes Incorporated", "AP63205WU-7"),
    "U3": ("Diodes Incorporated", "AP2112K-3.3TRG1"), "U4": ("Espressif", "ESP32-S3-MINI-1-N8"),
    "U5": ("Lite-On", "LTV-357T"), "U6": ("Lite-On", "LTV-357T"), "U7": ("Texas Instruments", "SN74LV1T34DBVR"),
    "U8": ("Texas Instruments", "TPS2116DRLR"),
    "R14": ("YAGEO", "RC0603FR-0733KL"), "R15": ("YAGEO", "RC0603FR-0710KL"), "R16": ("YAGEO", "RC0603FR-0710KL"),
    "C14": ("Samsung Electro-Mechanics", "CL10A105KB8NNNC"),
    "LED1": ("Worldsemi", "WS2812B-2020"), "LED2": ("Worldsemi", "WS2812B-2020"),
    "SW1": ("C&K", "PTS526 SM15 SMTR2 LFS"), "SW2": ("C&K", "PTS526 SM15 SMTR2 LFS"),
    "D8": ("onsemi", "MBR0540T1G"), "D7": ("Diodes Incorporated", "1N4148W-7-F"),
    "Q1": ("Alpha & Omega Semiconductor", "AO3400A"), "R13": ("YAGEO", "RC0603FR-0710KL"),
    "C13": ("Samsung Electro-Mechanics", "CL21A106KAYNNNE"),
    # 5 x 5 mm magnetic transducer, same size; pad layout not checked against this footprint
    "BZ1": ("Same Sky (CUI Devices)", "CMT-5023S-SMT-TR"),
}

# PWR_FLAG: nets that are only fed through passive parts (connectors, diodes, jumpers)
# (+5V needs none: the TPS2116 drives it)
FLAGS = [("GND", 241.3, 25.4), ("VBUS", 254.0, 25.4), ("VIN_BUCK", 266.7, 25.4),
         ("+5V_S", 279.4, 25.4), ("GND_S", 292.1, 25.4), ("VBUS_F", 304.8, 25.4), ("+5V_BUCK", 317.5, 25.4)]
FLAGS += [("+20V_P", 330.2, 25.4)]

TEXTS = [
    (20.32, 20.32, "USB-C: power + native USB of the ESP32-S3 (flashing, console)", 2.0),
    (127.0, 20.32, "5 V system rail (stove first, USB fallback: TPS2116 power mux) -> 3.3 V", 2.0),
    (20.32, 93.98, "Stove supply: 20 V -> 5 V step-down (only with JP1 + JP2 closed)", 2.0),
    (215.9, 20.32, "ESP32-S3-MINI-1-N8, reset and user button", 2.0),
    (20.32, 182.88, "Stove serial: single 5 V data wire, 1200 baud 8N2, isolated by optocouplers", 2.0),
    (304.8, 139.7, "Status LEDs (WS2812B chain on IO8)", 2.0),
    (335.28, 38.1, "Buzzer on IO21 (PWM ~4 kHz)", 2.0),
    (20.32, 154.94, "Test points (pads on the bottom) and M2 mounting holes", 2.0),
    (20.32, 266.7,
     "Isolation: with JP1 and JP2 open the stove side (GND_S, +5V_S, DATA_S, +20V_S) is galvanically separated;\n"
     "the optocouplers are powered from the stove's 5 V. Closing JP1 + JP2 powers the board from the stove's 20 V,\n"
     "which connects GND_S to GND (USB / PC ground) - only when the stove's low voltage is safely isolated from mains.\n"
     "The echo of our own TX on RX is kept on purpose: the firmware uses it to tell a missing bus from a silent stove.", 1.5),
    (304.8, 248.92,
     "GPIO: IO17 STOVE_TX (UART1), IO18 STOVE_RX, IO8 LED data, IO0 BOOT / user button,\n"
     "IO21 buzzer, IO4 power source (high = stove), IO19/20 USB D-/D+. Strapping pins IO3, IO45, IO46 unconnected (defaults).\n"
     "D5 shows the serial line in hardware: red = TX, green = RX (orange while sending: echo).\n"
     "Draft for review before ordering.", 1.5),
]


# ── Geometry ──────────────────────────────────────────────────────────────────
def rot(x, y, r):
    a = math.radians(r)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)

def snap(v):
    return round(v / 1.27) * 1.27

def pin_points(lib_id, X, Y, r):
    """{pin number: (x, y, outward direction in degrees, screen: 0 right, 90 up)}"""
    out = {}
    for num, name, px, py, pa, ptype, unit in kisym.pins(lib_id):
        rx, ry = rot(px, py, r)
        out[num] = (snap(X + rx), snap(Y - ry), (pa + r + 180) % 360)
    return out

def step(x, y, d, dist=2.54):
    a = math.radians(d)
    return snap(x + dist * math.cos(a)), snap(y - dist * math.sin(a))

def effects(size=1.27, hide=False, justify=None):
    e = [S("effects"), [S("font"), [S("size"), size, size]]]
    if justify:
        e.append([S("justify")] + [S(j) for j in justify])
    if hide:
        e.append([S("hide"), S("yes")])
    return e

def prop(name, value, x, y, hide=False, angle=0):
    return [S("property"), name, value, [S("at"), x, y, angle], effects(hide=hide)]


def override_pins(lib_id, sym):
    for sub in kisym.find(sym, "symbol"):
        for p in kisym.find(sub, "pin"):
            t = PIN_TYPE_OVERRIDE.get(lib_id, {}).get(kisym.first(p, "number")[1])
            if t:
                p[1] = S(t)
    return sym


# ── Build ─────────────────────────────────────────────────────────────────────
root = uid("root")
items, lib_ids = [], []
nets_seen = {}
pwr_count = [0]

def need(lib_id):
    if lib_id not in lib_ids:
        lib_ids.append(lib_id)

def place_symbol(ref, lib_id, value, fp, x, y, r, extra, pinmap, is_power=False):
    need(lib_id)
    node = [S("symbol"), [S("lib_id"), lib_id], [S("at"), x, y, r], [S("unit"), 1],
            [S("exclude_from_sim"), S("no")], [S("in_bom"), S("no" if is_power or extra.get("_bom") is False else "yes")],
            [S("on_board"), S("no" if is_power else "yes")], [S("dnp"), S("no")], [S("uuid"), uid("sym", ref)]]
    if is_power:
        node.append(prop("Reference", ref, x, y + 3.81, hide=True))
        node.append(prop("Value", value, x, y - 3.81 if "GND" not in value else y + 3.81))
        node.append(prop("Footprint", "", x, y, hide=True))
        node.append(prop("Datasheet", "", x, y, hide=True))
    else:
        # Reference / Value where the library symbol puts them (rotated with the symbol,
        # text kept horizontal); for 2-pin parts beside the body
        def libpos(field, dflt):
            for pr in kisym.find(kisym.symbol(lib_id), "property"):
                if pr[1] == field:
                    at = kisym.first(pr, "at")
                    rx, ry = rot(float(at[1]), float(at[2]), r)
                    return x + rx, y - ry
            return dflt
        # field angles are stored relative to the symbol: compensate the rotation so text stays horizontal
        fa = (360 - r) % 360 if r % 180 else 0   # 180: KiCad keeps the text readable itself
        if len(kisym.pins(lib_id)) == 2:
            pp = list(pin_points(lib_id, x, y, r).values())
            vertical = abs(pp[0][0] - pp[1][0]) < abs(pp[0][1] - pp[1][1])
            if vertical:
                rp, vp, j = (x + 2.54, y - 1.27), (x + 2.54, y + 1.27), ["left"]
            else:
                rp, vp, j = (x, y - 2.54), (x, y + 3.81), None
            node.append([S("property"), "Reference", ref, [S("at"), rp[0], rp[1], fa], effects(justify=j)])
            node.append([S("property"), "Value", value, [S("at"), vp[0], vp[1], fa], effects(justify=j)])
        else:
            rp = libpos("Reference", (x, y - 5.08)); vp = libpos("Value", (x, y + 5.08))
            node.append(prop("Reference", ref, rp[0], rp[1], angle=fa))
            node.append(prop("Value", value, vp[0], vp[1], angle=fa))
        node.append(prop("Footprint", fp, x, y, hide=True))
        node.append(prop("Datasheet", kisym.prop(lib_id, "Datasheet"), x, y, hide=True))
        for k, v in extra.items():
            if not k.startswith("_"):
                node.append(prop(k, v, x, y, hide=True))
    for num, *_ in kisym.pins(lib_id):
        node.append([S("pin"), num, [S("uuid"), uid("pin", ref, num)]])
    node.append([S("instances"), [S("project"), PROJECT, [S("path"), "/" + root, [S("reference"), ref], [S("unit"), 1]]]])
    items.append(node)

def wire(a, b, key):
    items.append([S("wire"), [S("pts"), [S("xy"), a[0], a[1]], [S("xy"), b[0], b[1]]],
                  [S("stroke"), [S("width"), 0], [S("type"), S("default")]], [S("uuid"), uid("wire", key)]])

def label(net, x, y, d, key):
    ang = {0: 0, 90: 90, 180: 180, 270: 270}[int(d) % 360]
    just = ["left", "bottom"] if ang in (0, 90) else ["right", "bottom"]
    items.append([S("label"), net, [S("at"), x, y, ang], effects(justify=just), [S("uuid"), uid("label", key)]])

def power(net, x, y, d, key):
    pwr_count[0] += 1
    lib_id = POWER[net]
    # orient the symbol body away from the pin: GND body points down (270), supplies up (90)
    body = 270 if net == "GND" else 90
    r = (d - body) % 360
    place_symbol("#PWR%03d" % pwr_count[0], lib_id, net, "", x, y, r, {}, {}, is_power=True)

for ref, lib_id, value, fp, x, y, r, pinmap, extra in PARTS:
    if ref in LCSC:
        extra = dict(extra, LCSC=LCSC[ref])
    if ref in MPN:
        extra = dict(extra, Manufacturer=MPN[ref][0], MPN=MPN[ref][1])
    place_symbol(ref, lib_id, value, fp, x, y, r, extra, pinmap)
    pts = pin_points(lib_id, x, y, r)
    unknown = set(pinmap) - set(pts)
    assert not unknown, (ref, unknown)
    missing = set(pts) - set(pinmap)
    assert not missing, (ref, "pins without a net", sorted(missing))
    done = {}
    drawn = []   # (net, direction, stub end) already labelled for this part
    for num, (px, py, d) in sorted(pts.items(), key=lambda kv: (kv[1][2], kv[1][1], kv[1][0])):
        net = pinmap[num]
        key = (px, py)
        if key in done:
            assert done[key] == net, (ref, num, "stacked pins on different nets")
            continue
        done[key] = net
        if net is NC:
            items.append([S("no_connect"), [S("at"), px, py], [S("uuid"), uid("nc", ref, num)]])
            continue
        nets_seen.setdefault(net, []).append(ref + "." + num)
        end = step(px, py, d)
        wire((px, py), end, (ref, num))
        near = [e for (n2, d2, e) in drawn if n2 == net and d2 == d and abs(e[0] - end[0]) + abs(e[1] - end[1]) <= 2.55]
        drawn.append((net, d, end))
        if near:
            wire(near[0], end, (ref, num, "tie"))   # joins the stub of the neighbouring pin
            continue
        if net in POWER:
            power(net, end[0], end[1], d, (ref, num))
        else:
            label(net, end[0], end[1], d, (ref, num))

for i, (net, x, y) in enumerate(FLAGS):
    place_symbol("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", "", x, y, 0, {}, {}, is_power=True)
    end = (x, snap(y + 5.08))
    wire((x, y), end, ("flag", net))
    if net in POWER:
        power(net, end[0], end[1], 270, ("flag", net))
    else:
        label(net, end[0], end[1], 270, ("flag", net))

for i, (x, y, t, size) in enumerate(TEXTS):
    items.append([S("text"), t, [S("exclude_from_sim"), S("no")], [S("at"), x, y, 0],
                  [S("effects"), [S("font"), [S("size"), size, size]], [S("justify"), S("left"), S("bottom")]],
                  [S("uuid"), uid("text", i)]])

sch = [S("kicad_sch"), [S("version"), S("20250114")], [S("generator"), "eeschema"],
       [S("generator_version"), "9.0"], [S("uuid"), root], [S("paper"), "A3"],
       [S("title_block"), [S("title"), "OpenPelletBridge controller"], [S("date"), "2026-11-18"],
        [S("rev"), "0.1 draft"], [S("company"), "OpenPelletBridge"],
        [S("comment"), 1, "ESP32-S3-MINI-1, USB-C + stove power, isolated Micronova serial"],
        [S("comment"), 2, "Generated by hardware/tools/gen_schematic.py - edit the script, not this file"]],
       [S("lib_symbols")] + [override_pins(l, kisym.symbol(l)) for l in lib_ids]]
sch += items
sch.append([S("sheet_instances"), [S("path"), "/", [S("page"), "1"]]])

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as f:
    f.write(dump(sch) + "\n")
print("written", os.path.relpath(OUT), "-", len(PARTS), "parts,", len(nets_seen), "nets")
for net, pins in sorted(nets_seen.items()):
    if len(pins) == 1 and net not in POWER:
        print("  single-pin net:", net, pins)
