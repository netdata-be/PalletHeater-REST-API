# Controller board: to do

[← Board](README.md)

Everything that has to happen before the board is ordered, when it arrives and before it replaces the ESP32 DevKit. Tick items off here; add new ones as they come up.

## GPIO: DevKit (now) → board

| Function | ESP32 DevKit (current firmware) | Board (ESP32-S3-MINI-1-N8) |
|---|---|---|
| Stove UART RX | GPIO16 (`RX_PIN`, lib/stove/Stove.cpp) | **IO18**, through optocoupler U6 (not inverted) |
| Stove UART TX | GPIO17 (`TX_PIN`) | **IO17**, through optocoupler U5 |
| RX enable | GPIO18 (`ENABLE_RX`, src/constants.h) | **gone**: the optocouplers need no enable |
| Button (short press / 5 s = full reset) | GPIO13 (`RESET_PIN`) | **IO0** (BOOT / USER button; held during reset = download mode) |
| Status LED | GPIO14 (`LED`) | **IO8**: 2× WS2812B (LED1 system, LED2 stove), via SN74LV1T34 |
| Onboard LED | GPIO2 (`ONBOARD_LED`) | **gone** (LED1 takes over) |
| Buzzer | – | **IO21**: PWM ~4 kHz via Q1 |
| Power source | – | **IO4**: TPS2116 ST, high = stove, low = USB (input, 10 kΩ pull-up) |
| USB | USB-serial chip on the DevKit | **IO19 / IO20**: native USB of the S3 (CDC console, flashing) |
| Serial TX / RX activity | – | D5 / D6, in hardware (no GPIO) |

Free on the board for later: IO1, IO2, IO5 – IO7, IO9 – IO16, IO33 – IO42, IO47, IO48 (IO35 – IO37 because the N8 has no PSRAM). Not to use: IO0, IO3, IO45, IO46 (strapping), IO43 / IO44 (ROM boot log).

## Before ordering

- [ ] **Stove connector pin order.** Not standardised: measure GND, 5 V, 20 V and DATA on the stove. The board expects GND_S, +5V_S, +20V_S, DATA_S on pins 1 – 4 (silkscreen GND / 5V / 20V / DATA). D8 protects the 20 V input against reversal, not the 5 V or DATA pins.
- [ ] **Stove supply.** How much current can the stove's 20 V and 5 V give, and is that low voltage safely isolated from mains? Needed before closing JP1 + JP2.
- [x] **F2 rating (review feedback, 19/11/2026).** Worst case the board takes ~2.9 W from the stove (WiFi peak + buzzer + LEDs): 0.24 A at 12 V. Done: F2 is now a Littelfuse 1206L035/30WR (350 mA hold, 30 V, LCSC C2153989).
- [ ] **F1 (USB, 0.5 A hold)** is tight at ~0.5 A worst case: keep the firmware from running the buzzer, full-white LEDs and WiFi transmission at the same time, or limit the LED brightness.
- [x] **Power priority (review feedback, 19/11/2026).** Done: the two LM66100 (no priority, and with CE on GND no reverse current blocking either) are replaced by a TPS2116 power mux: stove first, USB below ~4.3 V on the stove's 5 V.
- [x] **Decoupling at the module (review feedback, 19/11/2026).** Done: C3 (100 nF) right next to the module's 3V3 pin 3, C15 (10 µF) at the module; C2 stays at the LDO.
- [x] **3.3 V regulator heat (review feedback, 19/11/2026).** The board runs at ~20 °C ambient. AP2112K: ~0.3 W with WiFi and the buzzer (PWM, ~55 mA average), ~+70 °C at an estimated 200 – 250 °C/W: ~90 °C, within its 125 °C. Kept; measured at bring-up.
- [ ] **AP63205 parts.** Inductor (6.8 µH) and output capacitors against the datasheet table for 5 V out.
- [ ] **Optocoupler CTR rank.** LTV-357T with CTR ≥ 100 % (the currents in the README assume it; review feedback 19/11/2026: R5 220, R6 1.5k, R7 10k).
- [ ] **Stove DATA pull-up.** Measure it on the stove (idle voltage, and the current into a 1 kΩ to GND_S): U5 can pull DATA low against a pull-up down to ~750 Ω.
- [ ] **Buzzer pads.** MLT-5020 footprint (from LCSC / EasyEDA): pads 1 (+), 2 (−), 3 (mechanical, no net): check against the datasheet. For Aisler: check that the CMT-5023S fits the same pads, or pick another part.
- [x] **Temperature at the mounting place.** ~20 °C (user, 19/11/2026); limits: buzzer −30 – 70 °C, ESP32-S3-MINI-1-N8 up to 85 °C.
- [ ] **JLCPCB preview.** Check the orientation of every polarised part, especially the new ones: Q1 (SOT-23), D7 / D8 (SOD-123), BZ1, U8 (TPS2116, SOT-583: new package, no correction known yet), and again U1 / U2 / U3 / U7 / LED1 / LED2 / J1. Corrections go in `CPL_FIX` (tools/export_production.sh).
- [ ] **PCB review.** Step-down loop (input capacitor, inductor), USB D+ / D- pair, isolation strip (creepage ≥ 1 mm between GND_S and GND copper apart from JP1 / JP2), mounting holes against the enclosure.
- [ ] **Stock** of every LCSC / MPN part on the day of ordering.
- [ ] **J1 USB-C at JLCPCB.** GCT USB4105-GF-A (C3020560) has no JLCPCB stock (LCSC has it): pre-order it into the JLCPCB parts library before ordering the assembly. Fallback if that fails: HRO TYPE-C-31-M-12 (C165948, plenty of stock) with its own footprint, but then Aisler needs another part. Preview offset fixed in `CPL_FIX` (19/11/2026): check J1 again in the JLCPCB preview.
- [ ] Commit the hardware folder (not committed yet).

## When the board arrives (bring-up)

Test pads on the bottom: 3V3, 5V, GND, TX, RX (under the module), 20V, 5V_S, DATA, GND_S (stove side).

- [ ] Visual check: solder bridges, orientation of the polarised parts.
- [ ] USB-C only, JP1 / JP2 open: 5V ≈ 5 V, 3V3 = 3.3 V; current from USB well below 100 mA without WiFi.
- [ ] Flash a test sketch over USB (BOOT held, RESET): blink both WS2812s, beep the buzzer, read the BOOT button.
- [ ] Stove side, JP1 / JP2 open, stove cable connected: 20V ≈ 20 V (positive: D8 conducts), 5V_S ≈ 5 V, DATA idles high. The green RX LED (above U6) blinks when the stove or the remote talks.
- [ ] Loopback: send from the ESP and check the echo on IO18 (red TX and green RX light together). Scope DATA, STOVE_TX and STOVE_RX: DATA low < 0.8 V while we send, clean edges on STOVE_RX (rise through the 10k pull-up, fall time of U6) well within a bit (833 µs).
- [ ] Status LEDs during boot and reset: no flashes or random colours (R8 holds the SN74LV1T34 input low; IO8 is not a strapping pin).
- [ ] Only if the stove's low voltage is isolated from mains: close JP1 + JP2 (silkscreen + and − at STOVE PWR), unplug USB, check 5V and 3V3 under WiFi load. Note: GND_S is then tied to GND and to a PC on USB.
- [ ] **Worst-case current** (WiFi transmitting, both WS2812 full white, buzzer on), measured in the USB cable and in the stove's 20 V: below the hold current of F1 (0.5 A) and F2 (0.35 A); neither PTC gets warm.
- [ ] **Power mux (U8, TPS2116: stove first, USB fallback).** With a scope on the 5V and 3V3 pads: power-up and power-down from each source; plug and unplug USB while the stove powers the board (nothing should change: USB only carries data, check that the board draws ~0 mA from USB), and switch the stove supply on / off while USB is plugged in (break-before-make, ~8 µs: 3V3 must not dip enough to reset the ESP, check the reset reason). IO4 high on stove power, low on USB. Switchover threshold of the stove's 5 V: ~4.3 V.
- [ ] **3.3 V regulator temperature**: with WiFi busy (and the buzzer on), measure the temperature of U3 (thermocouple or IR camera) and the 3V3 ripple at the module during transmission peaks; again inside the stove.
- [ ] Mount it in the stove (nylon or metal M2 screws: the holes have no copper). Check WiFi signal strength in the final place.

## Case

- [ ] Test print base and lid; fit of the board, the lid's lip, the USB-C cable and the stove cable plug (KF2EDGKD height not known: the notch is open through the lid).
- [ ] Buttons: travel and force of the flexure tongues (plunger 0.3 mm above the switch; adjust `button_top` / tongue size if needed).
- [ ] LEDs visible through the lid (translucent filament or clear fill), no colour bleed between SYS and STOVE.
- [ ] Snap fit: the hooks click in and hold, the board sits without play on the pegs (adjust `snap_hook`, `snap_t`, `peg_d` or the tube length if needed; print in PETG if PLA hooks break).

## Firmware port (ESP32 → ESP32-S3)

- [ ] **PlatformIO environment** for the board: `board = esp32-s3-devkitc-1` (or a custom board JSON for the MINI-1-N8), 8 MB flash, `ARDUINO_USB_CDC_ON_BOOT=1` (console over native USB), partition table for 8 MB (two larger OTA slots, larger SPIFFS, core dump). Keep the DevKit build: both boards must build in CI. A build flag (e.g. `-DBOARD_OPB_S3`) selects the pins.
- [ ] **Pins in one place** (constants.h / a board header) instead of `RX_PIN` / `TX_PIN` in lib/stove and the defines in constants.h; see the table above.
- [ ] **Stove UART**: UART1 on IO18 (RX) / IO17 (TX). Check the signal polarity through the optocouplers: TX low → DATA low (not inverted), DATA low → RX low (not inverted) according to the design; confirm with the scope / loopback. Remove `ENABLE_RX` handling for this board.
- [ ] **Echo check**: the bus self-test ("bus not connected" vs "stove does not answer") must work with the optocoupler echo.
- [ ] **Status LEDs**: WS2812 driver (RMT; e.g. Adafruit NeoPixel or FastLED, both open source) replacing EasyLed. Define the colours / blink patterns: LED1 system (WiFi, Bluetooth, telnet, OTA, error), LED2 stove state (off, starting, working, cleaning, alarm). Brightness setting in the web app.
- [ ] **Buzzer**: LEDC PWM on IO21 (`ledcWriteTone`). The PWM frequency is the pitch: loudest near the resonance (~4 kHz), usable roughly 1 – 6 kHz, quieter away from 4 kHz; volume coarsely via the duty cycle (50 % = loudest). Non-blocking player (a task or the loop, no `delay()`).
  - [ ] **Tone pattern per event**: a sequence of (frequency, duration, pause) steps, repeated or not. Defaults, e.g.: button press = one short high beep; success / OTA done = rising two tones; pellet reserve = two tones; stove alarm / error = fast repeating high tone; E1 countdown = beep every second, faster near the end.
  - [ ] **Settings in the web app and API**: buzzer on / off, volume (duty), per event on / off and its pattern (choose from presets, a test button that plays it), quiet hours. Stored in NVS.
  - [ ] MQTT / Home Assistant: switch for the buzzer, and a command to play a pattern (e.g. from an automation).
- [ ] **Button on IO0**: short press and 5 s full reset as now; IO0 is also the boot strap, so nothing may hold it low at reset.
- [ ] **Core dump, watchdog, breadcrumbs, reset reasons** checked on the S3 (other IDF config, two cores, RTC memory layout). Decode a test core dump with the S3 toolchain.
- [ ] **BLE**: NimBLE on the S3 (BLE 5), BLE OTA tested.
- [ ] **HTTPS** (port 443, src/https.h) on the S3: certificate generation, handshake time and free memory with a TLS session open (~40 KB).
- [ ] **OTA from GitHub**: release assets per board (DevKit and S3), the updater picks the right one; never flash an S3 image on a DevKit or the other way round.
- [ ] **CI**: build both boards, attach both images and ELFs to the release.
- [ ] **Power source** on IO4: show stove / USB in the web app, API, MQTT; log the switchovers as events.
- [ ] **Web app / API**: show the board type and hardware revision; hide what the board does not have (and show the buzzer settings only on the board).
- [ ] **Optional, free GPIOs**: stove supply voltage (only with JP1 / JP2 closed), internal temperature sensor of the S3 as a warning.

## Documentation

- [ ] README / docs: the board as the recommended hardware, wiring photo, the JP1 / JP2 choice explained for users.
- [ ] docs/development.md: building and flashing for the S3 (USB, no BOOT button needed after the first flash).
