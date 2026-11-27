# Development

[← README](../README.md)

Building, flashing, CI and the source layout.

## Building and uploading

Built with [PlatformIO](https://platformio.org/). Environments in [platformio.ini](../platformio.ini):

| Environment | Upload method |
|---|---|
| `esp32doit-devkit-v1-serial` | USB serial |
| `esp32doit-devkit-v1-http` | WiFi over HTTP (`POST /api/update`), fastest; needs firmware that already has `/api/update` |
| `esp32doit-devkit-v1-ota` | WiFi (ArduinoOTA) |
| `opb-s3-ble-ota` | Board only: Bluetooth LE ([scripts/ble_ota_upload.py](../scripts/ble_ota_upload.py)) |

```bash
pio run -e esp32doit-devkit-v1-serial -t upload
pio device monitor -e esp32doit-devkit-v1-serial
```

The WiFi and Bluetooth uploads authenticate with the controller's **API key** (web app → *Device*, or `GET /api/auth`), taken from the environment or, when it is not set there, from `.envrc` in the project folder (git-ignored, read by [scripts/api_key.py](../scripts/api_key.py); also works for the PlatformIO buttons in VS Code):

```bash
echo 'OPB_API_KEY=<key>' > .envrc        # or: export OPB_API_KEY=<key>
pio run -e esp32doit-devkit-v1-http -t upload
```

Building works without the key; only an upload stops and says the key is missing.

Partitions: `min_spiffs.csv` (4 MB flash): two OTA slots of 1.9 MB, 128 KB SPIFFS (history, events), 64 KB core dump. The DevKit firmware (without Bluetooth) uses about 87 % of a slot; a larger slot is only possible by dropping the core dump or shrinking SPIFFS, and needs a USB flash (an OTA update cannot change the partition table).

Build flags (in `build_flags` of an environment):

| Flag | Default | Effect |
|---|---|---|
| `-DOPB_BLUETOOTH=0` | `1` | Leaves Bluetooth out (BLE app, BLE OTA): about 250 KB less flash. Set for every DevKit environment (`[devkit]` in `platformio.ini`, with `lib_ignore = NimBLE-Arduino`): with Bluetooth the DevKit image (1.97 MB) no longer fits an app slot (1.875 MB). The S3 board keeps Bluetooth. `/api/bluetooth` then answers `"available":false`. |
| `-DOPB_WIFI_COUNTRY='"NL"'` | `"BE"` | WiFi country code (allowed channels and transmit power; 802.11d on). |

Use one PlatformIO Core: two versions (e.g. the one of the IDE and one in the shell) clean each other's build folders, and a build that runs at that moment fails with missing files.

### OpenPelletBridge board (ESP32-S3)

The board ([hardware/](../hardware/)) has an ESP32-S3-MINI-1-N8 (8 MB flash, no PSRAM) and needs its own image: an S3 image does not run on an ESP32 and the other way round (every update path refuses an image for the other chip from its header, before anything is written: [Wrong chip](debugging.md#post-apiupdate)). The build flag `-DBOARD_OPB_S3` selects the pins and what the board has in [src/board.h](../src/board.h); `controller.board` / `controller.hwRev` in `GET /api/state` tell which one runs.

| Environment | Upload method |
|---|---|
| `opb-s3` | USB (native USB of the S3, USB-C on the board) |
| `opb-s3-http` | WiFi over HTTP (`POST /api/update`), `--upload-port <ip>` |

```bash
pio run -e opb-s3                    # build only
pio run -e opb-s3 -t upload          # flash over USB-C
pio device monitor -e opb-s3         # console (USB CDC)
pio run -e opb-s3-http -t upload --upload-port <ip>
```

The first flash of a new board: hold BOOT, press RESET, release BOOT (download mode), then upload. After that no button is needed: esptool resets the S3 into download mode through its USB Serial/JTAG. The console is on the S3's own USB (USB Serial/JTAG): it disappears for a moment at every restart, and output before the host opened the port is lost (the live log `/api/log` has it).

Partitions: [partitions/opb_s3_8mb.csv](../partitions/opb_s3_8mb.csv): two OTA slots of 3 MB, 1.8 MB SPIFFS, 128 KB core dump. Switching an existing board to another partition table needs a USB flash. A core dump of the S3 is decoded with the S3 toolchain (`xtensa-esp32s3-elf-addr2line`, `esp-coredump --chip esp32s3`).

## CI and releases

[.github/workflows/firmware.yml](../.github/workflows/firmware.yml) runs the unit tests, checks the web app ([scripts/check_webapp.mjs](../scripts/check_webapp.mjs): script syntax, every text in English and Dutch, no unknown text keys), validates the OpenAPI spec and builds the firmware of both boards (plus the board's BLE OTA environment) on every push to `master`, every pull request and on demand (*Run workflow*). Run the web app check locally with `node scripts/check_webapp.mjs`. The images are kept as workflow artifact `openpelletbridge-<version>` (Actions → run → *Artifacts*):

| ESP32 DevKit (`esp32doit-devkit-v1-http`) | OpenPelletBridge board, ESP32-S3 (`opb-s3`) | Use |
|---|---|---|
| `openpelletbridge-firmware.bin` | `openpelletbridge-opb-s3-firmware.bin` | OTA image: web app upload, `POST /api/update`, `/api/firmware` |
| `openpelletbridge-factory.bin` | `openpelletbridge-opb-s3-factory.bin` | Complete flash image for a new chip, written at offset 0 (below) |
| `openpelletbridge-firmware.elf` | `openpelletbridge-opb-s3-firmware.elf` | Debug symbols of the same build: decode a core dump (`/api/coredump`) or a backtrace |
| `SHA256SUMS` | | Checksums of all files |

Each controller installs only the OTA image of its own board from a release ([/api/firmware](debugging.md#apifirmware)). Do not rename `openpelletbridge-firmware.bin`: the DevKit firmware in the field (up to v1.4.x) looks for exactly that name. The S3 assets have a name that firmware never matches (it compares the whole name), so an old DevKit never downloads an S3 image.

Flashing a factory image (bootloader, partition table, `boot_app0`, app, with `0xFF` in the gaps: it also clears NVS at `0x9000`, so the settings and the API key start fresh; SPIFFS and the core dump further on stay):

```bash
esptool.py --chip esp32   write_flash 0x0 openpelletbridge-factory.bin          # DevKit
esptool.py --chip esp32s3 write_flash 0x0 openpelletbridge-opb-s3-factory.bin   # board: hold BOOT, press RESET first
```

What is in it, at the offsets PlatformIO flashes (`pio project metadata -e <env>`; CI builds it with `esptool.py merge_bin`):

| Part | ESP32 DevKit | ESP32-S3 board |
|---|---|---|
| bootloader | `0x1000` | `0x0` |
| partition table | `0x8000` | `0x8000` |
| `boot_app0.bin` (otadata) | `0xe000` | `0xe000` |
| app (`firmware.bin`) | `0x10000` | `0x10000` |
| flash mode / frequency / size | DIO, 40 MHz, 4 MB | DIO, 80 MHz, 8 MB |

The parts can also be flashed one by one from `.pio/build/<env>/`, e.g. for the board: `esptool.py --chip esp32s3 write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 0x10000 firmware.bin`.

A tag `vX.Y.Z` also creates a GitHub release with these files, which the controllers then offer as update:

```bash
git tag v1.0.0 && git push origin v1.0.0
```

The firmware version is `git describe --tags`, so the release build reports exactly `v1.0.0`. A tag with a suffix (`v1.1.0-rc1`) becomes a pre-release, which the controllers ignore.

## Unit tests

The pure logic (timer and clock encodings, fan and pellet decisions, error debounce, quality labels, address lists, MQTT payloads) lives in [lib/pelletcore](../lib/pelletcore/src/pelletcore.h) without Arduino dependencies. It is tested on the development machine:

```bash
pio test -e native
```

## Source layout

`src/main.cpp` only holds `setup()`, `loop()` and the route table. The rest is split into headers that main.cpp includes in a fixed order (one translation unit, because [src/constants.h](../src/constants.h) defines the globals): `stoveCache.h` (reading the stove: the read planner with one interval per group of registers, and the sensor task that runs it), `stoveControl.h` (writing settings), `registers.h` (register names, `/api/dump`, `/api/remote`), `stoveClock.h`, `routesBasic.h`, `routesTimers.h`, `routesControl.h`, `status.h` (`/api/state`), `history.h`, `pellets.h` (pellet consumption, `/api/pellets`), `events.h`, `routesDevice.h`, `mqtt.h`, `wifiInfo.h`, `fwUpdate.h` (GitHub update), `liveDebug.h` (`/api/log`, `/api/busmon`), `coreDump.h` (`/api/coredump`), `https.h`, `http6.h`, `telnetMenu.h` (last: the telnet screens use the modules above; it includes `telnetServer.h`, the server on lwIP sockets for IPv4 and IPv6, and `telnetUi.h`, the generic pages), plus `liveLog.h` (included first: copies `Serial` output into the live log) and the older `auth.h`, `bleUart.h`, `httpUpdate.h`, `keyWatch.h`, `metrics.h`.

## Updating the API documentation

[docs/openapi.json](openapi.json) is the single source of truth. It is embedded in the firmware at build time, gzipped like the web app ([scripts/gzip_assets.py](../scripts/gzip_assets.py), saves ~240 KB of flash), and served on `/api-specs.json` with `Content-Encoding: gzip`. Browsers handle that; with curl use `--compressed` (without `Accept-Encoding: gzip` the answer is `406`). When you add or change an endpoint:

1. Update `docs/openapi.json` and [api.md](api.md).
2. Validate the spec, e.g. `pipx run openapi-spec-validator docs/openapi.json`.
3. Rebuild and flash the firmware.
