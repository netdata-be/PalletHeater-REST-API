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
| `esp32doit-devkit-v1-ble-ota` | Bluetooth LE ([scripts/ble_ota_upload.py](../scripts/ble_ota_upload.py)) |

```bash
pio run -e esp32doit-devkit-v1-serial -t upload
pio device monitor -e esp32doit-devkit-v1-serial
```

The WiFi and Bluetooth uploads authenticate with the controller's **API key** (web app → *Device*, or `GET /api/auth`), taken from the environment:

```bash
export OPB_API_KEY=<key>
pio run -e esp32doit-devkit-v1-http -t upload
```

Partitions: `min_spiffs.csv` (4 MB flash): two OTA slots of 1.9 MB, 128 KB SPIFFS (history, events), 64 KB core dump. The firmware uses about 91 % of a slot; a larger slot is only possible by dropping the core dump or shrinking SPIFFS, and needs a USB flash (an OTA update cannot change the partition table).

## CI and releases

[.github/workflows/firmware.yml](../.github/workflows/firmware.yml) runs the unit tests, checks the web app ([scripts/check_webapp.mjs](../scripts/check_webapp.mjs): script syntax, every text in English and Dutch, no unknown text keys), validates the OpenAPI spec and builds the firmware (plus the BLE OTA environment) on every push to `master`, every pull request and on demand (*Run workflow*). Run the web app check locally with `node scripts/check_webapp.mjs`. The images are kept as workflow artifact `openpelletbridge-<version>` (Actions → run → *Artifacts*):

| File | Use |
|---|---|
| `openpelletbridge-firmware.bin` | OTA image: web app upload, `POST /api/update`, `/api/firmware` |
| `openpelletbridge-factory.bin` | Complete flash image for a new ESP32: `esptool.py --chip esp32 write_flash 0x0 openpelletbridge-factory.bin` |
| `SHA256SUMS` | Checksums |

A second artifact `openpelletbridge-elf-<version>` holds `firmware.elf` with the debug symbols of the same build, to decode a core dump (`/api/coredump`) or a backtrace; it is not part of the release.

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

`src/main.cpp` only holds `setup()`, `loop()` and the route table. The rest is split into headers that main.cpp includes in a fixed order (one translation unit, because [src/constants.h](../src/constants.h) defines the globals): `stoveCache.h` (reading the stove, sensor task), `stoveControl.h` (writing settings), `registers.h` (register names, `/api/dump`, `/api/remote`), `stoveClock.h`, `routesBasic.h`, `routesTimers.h`, `routesControl.h`, `status.h` (`/api/state`), `history.h`, `pellets.h` (pellet consumption, `/api/pellets`), `events.h`, `routesDevice.h`, `mqtt.h`, `wifiInfo.h`, `fwUpdate.h` (GitHub update), `liveDebug.h` (`/api/log`, `/api/busmon`), `coreDump.h` (`/api/coredump`), plus `liveLog.h` (included first: copies `Serial` output into the live log) and the older `auth.h`, `bleUart.h`, `httpUpdate.h`, `keyWatch.h`, `metrics.h`, `telnetMenu.h`.

## Updating the API documentation

[docs/openapi.json](openapi.json) is the single source of truth. It is embedded in the firmware at build time, gzipped like the web app ([scripts/gzip_assets.py](../scripts/gzip_assets.py), saves ~240 KB of flash), and served on `/api-specs.json` with `Content-Encoding: gzip`. Browsers handle that; with curl use `--compressed` (without `Accept-Encoding: gzip` the answer is `406`). When you add or change an endpoint:

1. Update `docs/openapi.json` and [api.md](api.md).
2. Validate the spec, e.g. `pipx run openapi-spec-validator docs/openapi.json`.
3. Rebuild and flash the firmware.
