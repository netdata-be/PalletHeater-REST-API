# REST API reference

[← README](../README.md)

The HTTP REST API of the controller. The OpenAPI 3 specification is [openapi.json](openapi.json); the firmware renders it with Swagger UI on `http://<device>/api`.

## API conventions

- **Base URL:** `http://<device-ip>` or `http://openpelletbridge.local` (port 80), or encrypted `https://openpelletbridge.local` (port 443, self-signed certificate, see [`/api/https`](#apihttps)). IPv4 and IPv6 (link-local, and SLAAC / ULA addresses where the network offers them; `/api/wifi` lists them under `ipv6`). Found on the network with mDNS / DNS-SD as "OpenPelletBridge (<model>)": `_http._tcp`, `_https._tcp` (TXT `path`, `api`, `version`, `model`) and `_arduino._tcp` (OTA). Only expose it on a trusted network.
- **API key (optional):** see [Authentication](#authentication).
- **Request parameters:** write operations take **query parameters** (form-encoded bodies also work), e.g. `POST /api/flame?power=3`. Only the timer endpoints take a **JSON body**.
- **Responses:** always `application/json`, except `/metrics` (Prometheus text), `/`, `/api` (HTML) and `/restart` (plain text).
- **Write responses** contain a `result` field (`"success"` or an error message) plus the value read back from the stove after the write.
- **Status codes:**

  | Code | Meaning |
  |---|---|
  | `200` | Success |
  | `202` | Accepted but nothing changed (e.g. power on while already on) |
  | `400` | Invalid parameter value |
  | `405` | HTTP method not supported on this route |
  | `500` | Serial communication with the stove failed |
  | `503` | The stove bus is busy (another request held it for more than 5 s). Retry. |
  | `503` | While the controller installs a firmware from GitHub (`/api/firmware`): `{"error":"Firmware update in progress - try again later","firmwareUpdate":true,"progress":42,"retryAfter":30}` with a `Retry-After: 30` header. The TLS download needs most of the memory, so every route except `/api/firmware`, `/api/health` and the status page `/update` is refused (`/` and `/app` get a small HTML page that reloads itself; after the restart the download runs with a minimal server that answers only `/update`, `/` and `/api/firmware`, see [debugging.md](debugging.md)) until the controller restarts on the new firmware (or the install fails). |

- **Caching:** `/api/state`, `/metrics`, `GET /api/timers` and (on the main screen) `/api/remote` are served from a cache that a background task keeps up to date with a **read planner**: every value has its own interval (one table in `src/stoveCache.h`, live with `GET /api/plan`), e.g. the stove state every second, fumes, auger and fume fan every 2 s while the stove is active, the active error every 5 s (the stove acts on an error only after ~55 s), the actual flame power every 30 s, the room temperatures every 60 s, settings every 10 – 60 s, each timer program at least every 60 s, error memory and counters every 5 – 10 min, the stove's firmware version and serial number at a (re)connect of the serial link. A register is read again at once after a write to it; while the remote control shows a menu its settings (and in the chrono menu the timers) are read every few seconds, and once more when it returns to the main screen. Spare bus time (up to 75 % of the bus by default, settable with `POST /api/plan`) refreshes settings and timers more often. Other endpoints talk to the stove directly, so each call takes a few serial round trips.
- **Bus locking:** the serial bus is shared with the background sensor task and BLE. Every endpoint that talks to the stove takes the bus lock first, so requests are handled one at a time on the bus. Commands go first: while a request (web, MQTT, telnet, Bluetooth) waits for the bus, the background reads hand it over before their next register, also in the middle of a group, and start nothing new until it is done. A command so waits for one register at most (~40 ms; measured: a one-register request in 101 ms median, 149 ms max, HTTP included). `GET /api/plan` → `handedOver` counts these hand-overs.
- **Retries:** every serial read and write is retried up to 3 times by the firmware before an endpoint reports a `500`. Clients do not need their own retry logic. Failures and retries are counted in `/metrics` (`stove_serial_read_errors_total`, `stove_serial_write_errors_total`, `stove_serial_retries_total`).

## Authentication

The controller generates a random 128-bit API key (32 hex characters) on first boot and keeps it in NVS; an existing key is reused. Using it is **optional** and off by default.

When the key is **required**, every route needs one of these headers, otherwise the response is `401`:

```bash
curl -H "X-API-Key: <key>" http://openpelletbridge.local/api/state
curl -H "Authorization: Bearer <key>" http://openpelletbridge.local/metrics
```

Always reachable without key: `/` and `/app` (web app), `/device` (fallback page), `/api/health` (for monitoring), `/api` + `/api-specs.json` (API docs, use **Authorize** in Swagger UI) and `/api/auth`.

| Call | Effect |
|---|---|
| `GET /api/auth` | `{ "required": false, "key": "…" }`. The key is only returned to authorised callers (or while it is not required). |
| `POST /api/auth?required=true` | Require the key (`false` makes it optional again). Needs the key while it is required. |
| `POST /api/auth?rotate=true` | Generate a new key. |

The web app (Device card) shows, copies and rotates the key and asks for it once when needed. The key is also printed on the serial console at boot. Plain HTTP is not encrypted: the key protects against other devices and web pages on the network, not against someone sniffing traffic. Use HTTPS (port 443) to keep the key off the wire. Telnet and BLE are not covered by the key; telnet can be switched off with [`/api/telnet`](#apitelnet).

For Prometheus:

```yaml
scrape_configs:
  - job_name: openpelletbridge
    authorization:
      credentials: "<key>"
    static_configs:
      - targets: ["openpelletbridge.local:80"]
```

## Endpoint overview

| Method | Path | Description |
|---|---|---|
| `GET` | [`/api/state`](#get-apistate) | All stove info in one call (cached) |
| `GET` | [`/api/health`](#get-apihealth) | Serial link health check |
| `GET` `POST` | [`/api/power`](#apipower) | Get or switch the power state |
| `GET` `POST` | [`/api/flame`](#apiflame) | Flame power 1 – 4 |
| `GET` `POST` | [`/api/fan`](#apifan) | Room fan speed 0 – 3 |
| `GET` `POST` | [`/api/setpoint`](#apisetpoint) | Room temperature set point 7 – 30 °C |
| `GET` `POST` | [`/api/energysaving`](#apienergysaving) | Energy saving start/stop offsets |
| `GET` `POST` | [`/api/burn`](#apiburn) | Combustion quality and pellet quality |
| `GET` `POST` | [`/api/beep`](#apibeep) | Stove beep on/off |
| `GET` `PATCH` | [`/api/time`](#apitime) | Stove clock vs NTP, sync clock |
| `GET` `POST` | [`/api/errors`](#apierrors) | Error memory (5 positions) |
| `GET` `POST` | [`/api/timers`](#apitimers) | All chrono programs PR1 – PR6 |
| `GET` | [`/api/plan`](#get-apiplan) | Read plan of the background task: intervals and age per group of registers |
| `GET` `POST` `PATCH` `DELETE` | [`/api/timers/pr{1-6}`](#apitimerspr1-6) | Single chrono program |
| `GET` | [`/api/remote`](#get-apiremote) | Current menu on the remote / display |
| `GET` | [`/api/dump`](#get-apidump) | Raw RAM / EEPROM register dump |
| `GET` `POST` `DELETE` | [`/api/keywatch`](#apikeywatch) | Fast watch of a few RAM addresses (reverse engineering) |
| `GET` | [`/api/history`](#get-apihistory) | Last 24 h (2-min samples) or 7 days (hourly averages) |
| `GET` | [`/api/log`](debugging.md#live-debugging) | Live firmware log (cursor) |
| `GET` `POST` | [`/api/busmon`](debugging.md#live-debugging) | Live stove bus monitor (cursor) |
| `GET` `POST` | [`/api/loglevel`](debugging.md#live-debugging) | Log level per firmware module |
| `POST` | [`/api/diag/reset`](debugging.md#live-debugging) | Reset the serial counters (`serial=true`) and / or the crash counter (`crashes=true`) |
| `GET` | [`/api/events`](#get-apievents) | Log of stove state changes and errors with exact times |
| `GET` `POST` | [`/api/bluetooth`](#apibluetooth) | Enable / disable Bluetooth (BLE) |
| `GET` `POST` | [`/api/mqtt`](#apimqtt) | MQTT settings and status |
| `GET` `POST` | [`/api/buzzer`](#apibuzzer) | Buzzer of the S3 board: settings, test sounds (404 on the DevKit) |
| `GET` `POST` | [`/api/wifi`](#apiwifi) | WiFi status, network, hostname and DHCP / static IP |
| `GET` | [`/api/wifi/scan`](#apiwifi) | WiFi networks in range |
| `GET` `POST` | [`/api/telnet`](#apitelnet) | Enable / disable the telnet menu |
| `GET` `POST` | [`/api/leds`](#apileds) | Status LEDs of the board: on / off, brightness, night (board only) |
| `GET` `POST` | [`/api/https`](#apihttps) | HTTPS on port 443: state, certificate, on / off, new key |
| `GET` `POST` | [`/api/auth`](#authentication) | API key status, require, rotate |
| `POST` | [`/api/update`](debugging.md#post-apiupdate) | Firmware update over HTTP |
| `GET` `POST` | [`/api/firmware`](debugging.md#apifirmware) | Check GitHub for a new firmware release and install it |
| `GET` | [`/update`](debugging.md#apifirmware) | Status page of an install from GitHub (`?to=<tag>&lang=nl\|en`), public |
| `GET` | [`/metrics`](#get-metrics) | Prometheus metrics |
| `GET` | [`/`](../README.md#web-app) | Web app (also on `/app`) |
| `GET` | `/device` | Small fallback page (IP, SSID, RSSI, uptime, links) |
| `GET` | `/restart` | Reboot the ESP32 |
| `GET` | `/api` | Swagger UI |
| `GET` | `/api-specs.json` | OpenAPI specification |
| | [Legacy routes](#legacy-routes) | `/on`, `/off`, `/state`, `/flame`, `/fan`, `/get`, `/set`, `/error`, `/cq`, `/display` |

## Endpoint reference

The examples use `curl` against `openpelletbridge.local`.

### `GET /api/state`

Everything you need for a dashboard in one call, served from cache.

```bash
curl http://openpelletbridge.local/api/state
```

```json
{
  "stoveState": { "stoveState": 4, "state": "Working", "poweredOn": true },
  "flame": { "power": 2, "actual": 1 },
  "bluetooth": true,
  "temperatures": { "room": 20.5, "stove": 21.0, "remoteControl": 20.5, "setPoint": 21, "fume": 118 },
  "wirelessSensor": "off",
  "energySaving": { "start": -1, "stop": 2, "enabled": true, "startTemperature": 20, "stopTemperature": 23 },
  "RoomFanSpeed": 2,
  "roomFan": { "speed": 2, "actual": 2, "running": true, "mode": "on", "hotEnough": true, "drive": 23 },
  "lowPalletAlarm": 0,
  "pelletReserve": { "enabled": true, "sensor": 0, "counter": 20 },
  "brazierCleaning": { "minutesLeft": 12, "active": false, "interval": 45 },
  "stoveBeepEnabled": true,
  "numberOfStarts": 412,
  "hoursSinceLastService": 213,
  "totalRuntime": 1873,
  "firmwareVersion": "FW-PZ-02-V-01-04-00",
  "serialNumber": 12345678,
  "augerSpeed": 2.4,
  "burn": { "combustionQuality": "0", "pelletQuality": "0" },
  "fumeFanRPM": 1550,
  "RemoteControlLastSeen": 12,
  "previousErrors": false,
  "time": { "ntp": "2026-11-15 17:50:06", "stove": "2026-11-15 17:49:35", "delta": 31 },
  "dataAge": { "fastSeconds": 2, "slowSeconds": 9 },
  "serial": { "readErrors": 0, "writeErrors": 0, "retries": 3 },
  "uptime": { "seconds": 93784, "human": "1d 02h 03m 04s" },
  "controller": { "version": "674b88d-dirty", "built": "2026-11-16T17:05:12Z", "branch": "master", "env": "opb-s3",
                  "board": "opb-s3", "hwRev": "1.0", "features": ["bluetooth", "ws2812", "buzzer", "powerSense", "chipTemp"], "chipTemp": 41.5 },
  "power": { "source": "stove", "since": 1794843000, "switchovers": 0 }
}
```

| Field | Description |
|---|---|
| `stoveState` | Numeric state, label and `poweredOn` flag. See [stove states](#stove-states). |
| `flame.power` / `flame.actual` | Set flame power (EEPROM `0x7F`) and the power the stove actually runs at (RAM `0x34`); when the set point is reached the stove modulates down, e.g. set 2, actual 1. |
| `bluetooth` | `true` when BLE is running on the controller (see [`/api/bluetooth`](#apibluetooth)). |
| `hydro` | `{ "Pressure", "Temp" }`, only present when hydro mode is enabled in `/config.txt` on SPIFFS. |
| `temperatures.room` | Room temperature as used by the stove (RAM `0x01`). Comes from the sensor selected in the MULTICOMFORT menu (remote control, stove or wireless sensor); without contact with the remote / wireless sensor for more than 4 minutes the stove falls back to its own sensor. The MULTICOMFORT register is not known yet. |
| `wirelessSensor` | Installer setting *INSTELL WIRELESS* (bit 0 of RAM `0xB6`): `on` / `off` = external wireless temperature sensor enabled, `unknown` until the first read. Read-only: it can only be changed on the remote control, see [docs/discovered-values.md](discovered-values.md#instell-wireless-wireless-temperature-sensor). |
| `temperatures.stove` / `remoteControl` | Raw readings of the stove sensor and the remote control sensor (°C). |
| `temperatures.fume` | Exhaust temperature (°C). |
| `energySaving` | See [`/api/energysaving`](#apienergysaving). |
| `lowPalletAlarm` | `1` when the pellet level is low. |
| `pelletReserve` | `enabled` (installer setting PELLET RESERVE), `sensor` (RAM 0xA4: `5` = the level sensor sees low pellets), `counter` (RAM 0x89) and `minutesLeft`: while the sensor reports low pellets RAM 0x89 counts down from 20 to 0, about one step per minute, to the reserve alarm; `null` otherwise. `sensor` `4` was seen for ~40 s during a reserve; the countdown went on, so it counts as low too. `shutdownMinutes`: after the countdown the stove runs 30 more minutes at flame power 1 (RAM 0x91, half minutes) and then switches itself off; `null` outside that phase. `switchedOff`: `true` once the stove switched itself off because the reserve ran out (refill and switch it on again). `refilling`: `true` while `sensor` counts down 3 → 0 after a refill (the stove confirms the pellets, one step every ~40 – 60 s while burning); `refillConfirmSeconds` estimates the time left from the step time the controller measured at the last refill (60 s until it saw one) (`null` otherwise). During that time `shutdownMinutes` is `null` and `lowPalletAlarm` is `false`; `switchedOff` and the `PELLETS` error stay until `sensor` is 0. |
| `pendingError` | The error the stove reports right now (RAM 0x33), also before it counts as `currentError`, with `secondsLeft`: seconds until the stove acts on it (RAM 0x30). E1 (hopper lid open) gives 55 s; closing the lid in time clears it. `null` without an error. The web app shows it as an alert with the countdown. |
| `brazierCleaning` | `minutesLeft`: minutes to the next brazier cleaning (RAM 0x32), `null` when the stove is not working; `active`: `true` while the cleaning runs (state 5); `interval`: minutes between cleanings (EEPROM 0x02). |
| `totalRuntime` | Total working hours. |
| `augerPercent` | Share of the time the auger runs, 0 – 100 %: `augerSpeed` / 12 s (100 while pre-loading). |
| `augerSpeed` | Auger on-time per cycle in seconds (RAM 0x0D / 10). Measured 20/11/2026 at flame power 3 (6.2) and 2 (4.6): the auger runs that many seconds and stands still the rest of a fixed ~12 s cycle (RAM 0xB2 follows the motor); 20.0 at the start = running continuously (pre-loading). The name is kept for compatibility; earlier docs called it rpm. |
| `serialNumber` | Serial number of the stove: **MATR** on the nameplate (EEPROM 0xCD – 0xCF, read once). `null` until read. |
| `RemoteControlLastSeen` | Seconds since the last contact with the remote control, `-1` when unknown. |
| `previousErrors` | `false` when the error memory is empty, otherwise an object with the non-empty positions. See [`/api/errors`](#apierrors). |
| `time.delta` | NTP time minus stove time, in seconds. |
| `temperatures.roomSource` | The sensor the stove regulates on: `stove` (its own, `temperatures.stove`) or `remote` (`temperatures.remoteControl`), `null` until known. Derived from the readings: the room temperature (RAM 0x01) equals that sensor. A change is logged as an event (`/api/events`, `type: "source"`). |
| `dataAge` | `fastSeconds`: since the stove state was last read (every second); `slowSeconds`: age of the oldest regular value or setting (temperatures, pellet sensor, settings; intervals of 5 – 60 s, see `GET /api/plan`). `null` until everything was read once after a (re)connect. A large value means the stove is not answering. |
| `serial` | Serial bus counters since boot: reads / writes that failed after all retries, and individual retries. |
| `warnings` | Things that are probably not as intended while the stove works normally (not `currentError`, which means an error or something that blocks): `[]` or a list of `{ "id", "desc" }`. `REMOTE_SENSOR_NOT_USED`: the wireless sensor (`wirelessSensor`) is on, but for 2 minutes or more the stove regulates on its own sensor instead of the remote control's, because the remote control is not heard (switched off, empty battery, out of range). The stove sensor sits in the stove and follows the room slowly (24/11/2026: 20.5 °C while the room was 16 °C). The web app shows an orange banner; MQTT `warning` / `warning_active`, metric `stove_warning_remote_sensor_not_used`. `CHIP_TEMP_HIGH` (board only): `controller.chipTemp` reached 70 °C (cleared below 65 °C); the board sits at ~20 °C, the ESP32-S3-MINI-1 is rated up to 85 °C. |
| `currentError` | Active error: `false`, or `{ "id", "error", "desc", "source" }`. `source` `stove`: an error code of the stove (RAM `0x33`, like the error memory). `source` `controller`: `{"id": -1, "error": "LINK", "desc": "No answer from the stove (serial link down)"}` while the stove does not answer; `{"id": -2, "error": "PELLETS", "desc": "Pellets used up: …"}` once the stove switched itself off at the end of the pellet reserve (the stove stores no error for this; it clears when the level sensor sees pellets again after a refill). |
| Unknown values | Values never read from the stove since the start are `null`; once read, the last known value stays (see `dataAge`). While the stove does not answer `stoveState.poweredOn` and `brazierCleaning.active` are `null` too. |
| `controller` | Controller (ESP32) firmware build: `version` from `git describe --tags --always --dirty` (`-dirty` = built with uncommitted changes), `built` build time in UTC, `branch`, PlatformIO `env`. Generated by [scripts/buildinfo.py](../scripts/buildinfo.py) on every build. Also exported as the Prometheus metric `esp32_build_info`. `board`: `esp32-devkit` or `opb-s3` (OpenPelletBridge board), `hwRev` its hardware revision (board only). `features`: what this build and board have, from the `BOARD_HAS_*` flags in [src/board.h](../src/board.h): `bluetooth`, `onboardLed` (DevKit LEDs), `ws2812` (RGB status LEDs), `buzzer`, `powerSense` (see `power`), `chipTemp`; the web app hides what is not listed, new names may be added. `chipTemp` (board only): internal temperature of the ESP32-S3 in °C, read every 30 s and smoothed, `null` until the first reading. |
| `power` | Board with `powerSense` only: `source` `stove` (the stove's 5 V, JP1 / JP2 closed) or `usb`, from the TPS2116 power mux (stove first, USB below ~4.3 V; ST on IO4); `since` Unix time of the switch to it (or of the start), `null` without NTP time; `switchovers` since the start. A switchover is logged (`sys`, info) and stored in `/api/events` (`type: "power"`). |

On a serial error the cache keeps the last known value, so a failed read never shows up as `-1` or `255`.

### `GET /api/health`

Checks whether the stove answers on the serial bus.

```bash
curl http://openpelletbridge.local/api/health
# 200 {"health":"Healthy"}
# 500 {"health":"Failed to talk to Stove"}
```

### `/api/power`

```bash
curl http://openpelletbridge.local/api/power
# {"stoveState":0,"state":"Off","poweredOn":false}

curl -X POST "http://openpelletbridge.local/api/power?state=on"
# 200 {"result":"Powered on sent","stoveState":1,"state":"Starting","poweredOn":true}
# 202 {"result":"Stove already on","stoveState":4,"state":"Working","poweredOn":true}
# 409 {"result":"Refused: pellets used up (…) - refill the hopper first","stoveState":0,"state":"Off","poweredOn":false}
```

| Parameter | Values |
|---|---|
| `state` | `on`, `off` |

- `on` only acts when the stove is `Off` (state 0); otherwise `202 Stove already on`.
- `on` is refused with `409` while `currentError` is `PELLETS` (the stove switched itself off because the pellet reserve ran out): refill the hopper first. MQTT, Bluetooth and telnet refuse it the same way; the remote control and the stove's own timers are not affected.
- `off` writes state `0` while starting / pellet loading and state `6` (final cleaning, the stove's own shut-down) while igniting, working, in brazier cleaning and in standby. When the stove is already off, in final cleaning or in an alarm (8, 9, 10), it writes nothing and returns `202`: an alarm is never reset this way (clear it on the stove).

### `/api/flame`

```bash
curl http://openpelletbridge.local/api/flame
# {"power":2}

curl -X POST "http://openpelletbridge.local/api/flame?power=3"
# {"result":"success","power":3}
```

| Parameter | Values |
|---|---|
| `power` | `1` – `4` |

### `/api/fan`

```bash
curl http://openpelletbridge.local/api/fan
# {"speed":2}

curl -X POST "http://openpelletbridge.local/api/fan?speed=0"
# {"result":"success","speed":0}
```

| Parameter | Values |
|---|---|
| `speed` | `0` (off) – `3` |

### `/api/setpoint`

```bash
curl http://openpelletbridge.local/api/setpoint
# {"setpoint":21}

curl -X POST "http://openpelletbridge.local/api/setpoint?value=22"
# {"result":"success","setpoint":22}
```

| Parameter | Values |
|---|---|
| `value` | `7` – `30` (°C) |

### `/api/energysaving`

Energy saving: when the room reaches the set point the stove goes to power 1; when it keeps rising to `setpoint + stop` the stove extinguishes (standby); when it drops below `setpoint + start` it restarts. `stop = 0` (OFF) disables energy saving (the stove only modulates); `start = 0` (OFF) means the stove does not restart automatically after stopping. `enabled` is `true` when `stop` is set.

```bash
curl http://openpelletbridge.local/api/energysaving
# {"energySaving":{"start":-1,"stop":2,"enabled":true,"startTemperature":20,"stopTemperature":23}}

curl -X POST "http://openpelletbridge.local/api/energysaving?start=-1&stop=2"
# {"result":"success","energySaving":{...}}
```

| Parameter | Values |
|---|---|
| `start` | `-3`, `-2`, `-1`, `0` |
| `stop` | `0`, `1`, `2`, `3` |

Both parameters are optional; pass either one or both.

### `/api/burn`

Combustion quality and pellet quality correction (installer menu).

```bash
curl http://openpelletbridge.local/api/burn
# {"combustionQuality":"0","pelletQuality":"-1"}

curl -X POST "http://openpelletbridge.local/api/burn?combustionQuality=1&pelletQuality=-2"
# {"result":"success","combustionQuality":"1","pelletQuality":"-2"}

curl -X POST "http://openpelletbridge.local/api/burn?combustionQuality=%2BG"
# {"result":"success","combustionQuality":"+G","pelletQuality":"-2"}
```

| Parameter | Values |
|---|---|
| `combustionQuality` | `+G`, `-3`, `-2`, `-1`, `0`, `1`, `2`, `3` |
| `pelletQuality` | `-5` … `5` |

The leading `+` is optional (`+1` and `1`, `+G` and `G` are equivalent), so an unencoded `+` that a client turns into a space is accepted too. Responses always return positive values without a sign, except `+G`.

### `/api/beep`

```bash
curl http://openpelletbridge.local/api/beep
# {"enabled":true}

curl -X POST "http://openpelletbridge.local/api/beep?enabled=false"
# {"result":"success","enabled":false}
```

| Parameter | Values |
|---|---|
| `enabled` | `true`, `false`, `1`, `0` |

### `/api/time`

The stove has its own clock without DST support. The firmware compares it with NTP time.

```bash
curl http://openpelletbridge.local/api/time
# {"time":{"stove":"2026-11-15 17:50:12","ntp":"2026-11-15 17:50:38","delta":26}}

curl -X PATCH http://openpelletbridge.local/api/time
# {"result":"success Synced NTP time to stove","time":{"stove":"2026-11-15 17:51:00","ntp":"2026-11-15 17:51:01","delta":1}}
```

`PATCH` writes the current NTP time into the stove clock. Pass `datetime=YYYY-MM-DDTHH:MM[:SS]` (local time) to set a specific time instead, e.g. the browser time:

```bash
curl -X PATCH "http://openpelletbridge.local/api/time?datetime=2026-11-16T14:30:00"
```

The day of the week is written as 1 = Monday … 7 = Sunday, which the chrono timers depend on. The stove has no daylight saving time; set the clock again after the change. The stove clock is read from the stove every 5 min (every 10 s until a valid time has been read); in between the controller lets it tick on, so `stove`, `ntp` and `delta` are current on every request. A clock change made on the stove itself shows up after at most 5 min. A PATCH re-reads the stove clock right away.

### `/api/errors`

The stove keeps the last 5 errors in `MemoryPos_1` … `MemoryPos_5`.

```bash
curl http://openpelletbridge.local/api/errors
# {"previousErrors":false}
# {"previousErrors":{"MemoryPos_1":{"id":32,"error":"E1","desc":"Pellet loading door is open for too long or air pressure inside stove is wrong"}}}

# clear position 1
curl -X POST "http://openpelletbridge.local/api/errors?MemoryPos_1=0"
# {"action":"MemoryPos_1 set","previousErrors":false}
```

| Parameter | Values |
|---|---|
| `MemoryPos_1` … `MemoryPos_5` | Error id, see [error codes](#error-codes). `0` clears the position. Unknown ids return `400` and nothing is written. |

### `GET /api/plan`

The read plan of the background task: which groups of registers it reads, how often, and how old each value is. For troubleshooting and tuning. The web app shows it in expert mode (card *Read planner*: a bar per group that fills up towards its next read, bus use, reads per minute).

```json
{"active": true, "remoteMenu": 0, "busPercent": 39, "busCap": 75, "early": 25, "fullRead": true, "handedOver": 7,
 "groups": [{"name": "state", "prio": 1, "intervalS": 1, "ageS": 0, "reads": 3121, "fails": 0},
            {"name": "timer PR1", "prio": 4, "intervalS": 60, "ageS": 17, "reads": 52, "fails": 0}]}
```

`active`: the stove is starting, burning, cleaning or in alarm (the "active" intervals apply). `remoteMenu`: the menu on the remote control (RAM 0x1A, 0 = main screen). `busPercent`: share of the last ~5 s the bus was busy with planned reads. `fullRead`: everything was read once since the last (re)connect. `busCap` / `early`: the spare-time settings (below). `handedOver`: how often the background reads handed the bus over to a waiting command in the middle of a group (since the start). Per group: priority (1 live – 5 static), the interval that applies now (longer while a Bluetooth client is connected), seconds since the last read, reads and failed reads since the start.

`POST /api/plan?busCap=75&early=25` sets the spare-time reads (kept in NVS): `busCap` (10 – 95 %, default 75) is how busy the bus may get with extra reads of settings, timers and counters in spare time; `early` (5 – 100 % of the interval, default 25) is from when such a group may be read again (100 = never before its interval). Commands always get the bus before the next register, so a higher `busCap` does not delay them; it does make the stove answer more questions per second (watch `stove_serial_retries_total` and the fails in the plan). A higher `busCap` only adds reads together with a lower `early`.

### `/api/timers`

The six chrono programs `PR1` – `PR6`. A disabled program returns its stored times and settings too, with `"enabled": false`. `GET` answers from the cache of the read planner (each program read at least every 60 s, usually every ~20 s, at once after a write and when the remote control leaves a menu) with `readSecondsAgo` per program; `?live=1` reads the stove now (~2 s for all six). After `POST` / `PATCH` / `DELETE` the program is always read back from the stove. When a program cannot be read (after the serial retries) it is returned as `{"error": "Failed to read timer from stove - RS232 error"}` instead of partial data (and `GET /api/timers/prN` returns `500`).

```bash
curl http://openpelletbridge.local/api/timers
```

```json
{
  "timers": {
    "PR1": {
      "enabled": true,
      "on": "06:30",
      "off": "08:30",
      "enabled_on": { "monday": true, "tuesday": true, "wednesday": true, "thursday": true, "friday": true, "saturday": false, "sunday": false },
      "settings": { "setpoint": 21, "flame_power": 3, "RoomFanSpeed": 2 }
    },
    "PR2": { "enabled": false }
  }
}
```

`POST` updates several programs at once. Only the programs and fields present in the body are written:

```bash
curl -X POST http://openpelletbridge.local/api/timers \
  -H "Content-Type: application/json" \
  -d '{"timers":{"PR1":{"enabled":true,"on":"06:30","off":"08:30"},"PR2":{"enabled":false}}}'
# {"result":"success"}
```

Timer fields:

| Field | Values |
|---|---|
| `enabled` | `true` / `false` |
| `on`, `off` | `HH:MM` on a 10 minute boundary (`06:30`, not `06:35`), or `off` |
| `enabled_on.<day>` | `monday` … `sunday`: `true` / `false`. If you send `enabled_on`, days you leave out are disabled. |
| `settings.setpoint` | `7` – `30` |
| `settings.flame_power` | `1` – `4` |
| `settings.RoomFanSpeed` | `0` – `3` |

The whole body is validated before anything is written. Invalid values, unknown program names or unknown days return `400` with an `error` message, and nothing is written:

```json
{ "error": "PR1: 'on' must be HH:MM on a 10 minute boundary or 'off'" }
```

### `/api/timers/pr{1-6}`

Single program, path `pr1` … `pr6`.

| Method | Body | Effect |
|---|---|---|
| `GET` | – | Return the program |
| `POST` | none | Enable the program, keep its settings |
| `POST` | JSON timer object | Partial update (same as `PATCH`). Not a full replace. |
| `PATCH` | JSON timer object | Partial update, only provided fields are written |
| `DELETE` | – | Disable the program (times and settings are kept) |

```bash
curl -X PATCH http://openpelletbridge.local/api/timers/pr1 \
  -H "Content-Type: application/json" \
  -d '{"off":"09:00","settings":{"setpoint":22}}'
# {"result":"success","PR1":{"enabled":true,"on":"06:30","off":"09:00",...}}

curl -X DELETE http://openpelletbridge.local/api/timers/pr1
# {"result":"success","PR1":{"enabled":false}}
```

### `GET /api/remote`

What the remote control / display currently shows. Useful for reverse engineering.

```bash
curl http://openpelletbridge.local/api/remote
# {"displayed_menu":"Main screen","diplayed_menu":"Main screen","selected_parameter":"Flame power","language":"NLD","menuId":0,"parameterId":0,"editValue":0}
```

`menuId` is the raw id of the screen on the remote (RAM `0x1A`, 0 = main screen), `parameterId` the parameter being edited (RAM `0x18`, 0 = none) and `editValue` the value the remote shows while editing, before it is confirmed (RAM `0x19`). The web app uses these to mirror the remote's display.

`diplayed_menu` is a deprecated, misspelled alias of `displayed_menu` kept for existing clients. It will be removed in a future release.

### `/api/keywatch`

Reverse-engineering aid to find what the remote control does when a key is pressed. The remote talks to the stove over radio, so only its effect in the stove RAM can be seen, and some registers change for well under a second. While a watch runs the controller stops its normal polling and reads only the chosen RAM addresses back to back (about 20 reads per second in total), logging every change with a millisecond timestamp. Read-only: nothing is written to the stove. `/api/state` is not refreshed during a watch.

| Method | Parameters | |
|---|---|---|
| `POST` | `address` (max 8, default `0x08,0x15,0x16,0x18,0x19,0x1A,0x24`), `seconds` (1 – 300, default 60) | Start a watch; clears the log |
| `GET` | | Status and log |
| `DELETE` | | Stop |

```bash
curl -X POST "http://openpelletbridge.local/api/keywatch?address=0x08,0x16,0x24&seconds=60"
curl "http://openpelletbridge.local/api/keywatch"
```

```json
{
  "active": true, "remainingSeconds": 42, "started": 1794901687, "elapsedMs": 18120,
  "reads": 362, "readErrors": 0, "readsPerSecond": 20.0, "overflow": false,
  "addresses": { "0x08": { "start": 0, "now": 0 }, "0x16": { "start": 0, "now": 0 }, "0x24": { "start": 0, "now": 0 } },
  "events": [ { "ms": 5310, "addr": "0x08", "from": 0, "to": 8 }, { "ms": 5460, "addr": "0x08", "from": 8, "to": 0 } ]
}
```

`ms` is the time since the start of the watch (`started`, Unix time). The log holds 600 changes; `overflow` is `true` when more were dropped.

### `GET /api/dump`

Raw register dump for reverse engineering. Each register is read over the serial bus, so a full dump takes several seconds.

| Parameter | Values |
|---|---|
| `register` | `ram` (default), `eeprom`, `all`. Add `unknown` (e.g. `ram,unknown`) to only return addresses that are not decoded yet. |
| `address` | Comma separated list, decimal or hex (`0x21,0x34,125`). Default: all 256. |
| `async` | `1`: read in the background. Without it the dump runs in the web server's task: for those seconds no other request, telnet or MQTT is served. With `async=1` the request answers at once with `202 {"state":"running","done":0,"total":41}`; repeat the same request to follow the progress (`202`) until the dump is ready, then it returns the dump itself (once; the same request after that starts a new dump). A dump with other parameters while one runs: `409`. The background task pauses 20 ms after every register, so the sensor task and other requests get the stove bus in between. The web app's parameter table uses it. |

```bash
curl "http://openpelletbridge.local/api/dump?register=ram&address=0x21,0x34"
```

```json
{
  "RAM(0x00)": {
    "0x21": { "Address_int": 33, "Value_int": 4, "Value_txt": "Working", "Value_hex": "0x04" },
    "0x34": { "Address_int": 52, "Value_int": 2, "Value_txt": "Flame power 2", "Value_hex": "0x02" }
  }
}
```

Known addresses are documented in [docs/discovered-values.md](discovered-values.md).

### `GET /api/history`

The controller keeps one sample every 2 minutes for the last 24 hours (720 samples). Samples are kept in RAM and every sample is also appended to SPIFFS (`/hist_a.bin` and `/hist_b.bin`, rotated every 24 h), so the history survives a restart, an OTA update or a power cut; at most the last sample is lost. Appending ≈16 bytes per 2 minutes keeps flash wear far below the endurance of the 128 KB SPIFFS partition. Samples are only recorded once NTP time and stove data are available.

| Parameter | Values |
|---|---|
| `hours` | `1` – `24` (default `24`): the 2-minute samples. `25` – `168`: hourly averages of the last 7 days (see below). |

```bash
curl "http://openpelletbridge.local/api/history?hours=6"
```

```json
{
  "interval": 120,
  "fields": ["t", "room", "stove", "remote", "setpoint", "fume", "flame", "state", "fan", "flameActual", "auger", "fumeFan", "fanActual", "fanPower"],
  "samples": [[1794843000, 20.5, 22.0, 20.5, 22, 118, 2, 4, 1, 2, 1.6, 2650, 1, 21], [1794843120, 20.5, 22.0, 20.5, 22, 119, 2, 4, 1, 1, 1.2, 2590, 1, 21]]
}
```

`t` is Unix time (UTC), temperatures in °C (0.5 °C resolution as reported by the stove), `state` is the stove state code, `fan` the room fan speed, `flame` the set and `flameActual` the actual flame power, `auger` the auger (feed screw) on-time per cycle in seconds, `fumeFan` the fume fan speed in rpm, `fanActual` the speed the room fan really ran at (0 = stopped), `fanPower` the raw room fan power (RAM 0x0A: 0 off, 21/23/25 at speed 1/2/3) (`null` for samples recorded by older firmware). Each sample is an array in the order of `fields`. Changes shorter than the 2 minute interval are not captured in the samples; use [`/api/events`](#get-apievents) for exact state changes.

**7 days (`hours` 25 – 168):** the 2-minute samples are also averaged per clock hour into 168 hourly records, kept in `/hist_week.bin` (one append per hour). After an update to firmware with this feature the week is filled from the 24 h history. The answer has `"interval": 3600` and the fields `t` (start of the hour), `room`, `stove`, `remote`, `fume`, `flame`, `flameActual`, `fan`, `fanActual`, `auger`, `fumeFan` (hourly averages), `setpoint` (last value), `fumeMax`, `state` (most frequent state), `burning` (% of the hour in a heating state 1 – 5) and `n` (samples in that hour). The hour in progress is included as the last record.

```bash
curl "http://openpelletbridge.local/api/history?hours=168"
# {"interval":3600,"fields":["t","room","stove","remote","setpoint","fume","fumeMax","flame","flameActual","fan","fanActual","auger","fumeFan","state","burning","n"],
#  "samples":[[1794841200,20.6,21.0,20.8,21,118,131,2.0,1.8,1.0,1.0,1.5,2610,4,100,30], …]}
```

### `GET /api/events`

Log of every stove state change and every change of the active error, with the exact time. The stove itself does not store when an error happened, so this log is the only source for error times. Values are read about once a second; the last 200 entries are kept and appended to SPIFFS (`/events.bin`), so the log survives restarts and costs only one small flash write per change. Changes are recorded once NTP time is available.

| Parameter | Description |
|---|---|
| `hours` | Optional, 1–720: only return changes from the last N hours. Default: all stored changes |

```bash
curl "http://openpelletbridge.local/api/events?hours=24"
```

```json
{"events": [
  {"t": 1794843000, "type": "state", "from": 7, "to": 1},
  {"t": 1794843065, "type": "state", "from": 1, "to": 2},
  {"t": 1794843600, "type": "error", "id": 4, "error": "E9"},
  {"t": 1794844200, "type": "error", "id": 0, "error": null}
]}
```

`t` is Unix time (UTC). For `type: "state"`, `from` and `to` are stove state codes (see `/api/state`); `from` is `null` for the first change after the log was empty. For `type: "error"`, `id` is the raw error value and `error` the code (`id` 0 / `error` null = error cleared). Errors are only logged once they last 30 s (3 minutes during start-up, pellet loading, ignition, brazier cleaning and shut-down) or the stove is in an alarm state; RAM 0x33 briefly shows E12 when brazier cleaning starts and stops and E1 during the start-up check, which are not errors. For `type: "source"` the sensor the stove regulates on changed: `from` / `to` are `stove` or `remote`, `room` the room temperature then (e.g. the remote control switched on after the night: room 20.5 -> 16.0 °C). For `type: "pellet"`, `register` is `0xA4` (pellet reserve sensor: `5` = low pellets) with the old and new value. The reserve countdown (`0x89`) is not logged: it changes every minute during the reserve and pushed the useful entries out; entries of older firmware are dropped at the next start. Oldest first. The web app uses the error entries to show when each error memory entry occurred.


Serial link events: `{"t": …, "type": "link", "up": false}` when the stove stopped answering on the serial bus, `"up": true` when it answered again (the chart draws them red / green). Power events (OpenPelletBridge board): `{"t": …, "type": "power", "from": "stove", "to": "usb"}` when the board switched between the stove's supply and USB; a switch while the controller was off (e.g. the stove supply gone, later started on USB) is logged at the start. The first source after this firmware is installed is not logged. Brazier cleaning (state 5) is not logged as a state change: it is a short periodic step while working.

### `/api/pellets`

Pellets and burning hours left until the low pellet alarm. The controller counts the auger feed: the auger value (RAM 0x0D, fixed per flame power: 3.0 / 4.6 / 6.2 / 7.8) times the time, in "auger units" (an auger value above the 12 s cycle, 20.0 while pre-loading, counts as 12). The level sensor (RAM 0xA4) trips at the same level in the hopper each time, so between two alarms exactly the pellets added in between were burned: kg per unit = kg added / units. The last 10 cycles are kept in NVS. They are weighted: the newest cycle counts 1, each older one 0.75 times the next, so a change (other pellets, another refill amount) shows up after one or two alarms.

- **Self-correcting:** at every alarm the controller keeps the kg the estimate still predicted (`history[].predKg`): positive = the alarm came that much earlier than calculated, negative = later. That cycle is part of the next estimate. While the stove burns past the estimate without an alarm, `overdue` is `true` (`remainingKg` stays 0).

- **Refill:** detected when the alarm clears again (sensor back to normal for 30 s after it was low for at least 1 min), with the standard amount `refillKg` (15 kg). E1 (hopper lid) is not used: it does not always occur.
- **Topped up without an alarm, or another amount:** `POST /api/pellets?add=<kg>`, or *Refilled…* in the web app. While the alarm is on, `add` sets the amount of the coming refill instead of the standard one.
- **First estimate:** after one refill at the alarm and the next alarm. Each cycle after that makes it more accurate (`spread`).

| Method | Parameters |
|---|---|
| `GET` | – |
| `POST` | `add` (kg, 0 – 100), `refillKg` (kg, 1 – 100), `resetCalibration=true`, `pricePerBag` (€, 0 – 200, 0 = not kept), `bagKg` (1 – 100), `stockBags` (0 – 10000, `off` = not kept), `lat` + `lon` (location for the outdoor temperature, rounded to 0.01°; `lat=off` clears it) |

```json
{"calibrated": true, "cycles": 3, "low": false, "overdue": false, "lastRefillAt": 1794990120, "lastRefillKg": 15, "remainingKg": 6.2, "hoursLeft": 4.15, "hoursLeftAverage": 9.6,
 "alarmExpectedAt": 1795037700, "kgPerHour": 1.49, "refillKg": 15, "kgRefilledSinceAlarm": 15.0, "kgBurnedSinceAlarm": 8.81, "gramsPerUnit": 0.0668,
 "spread": 0.041, "averageAuger": 2.91, "averageHours": 168, "unitsSinceAlarm": 131740, "pendingKg": null,
 "history": [{"t": 1794990000, "kg": 15, "units": 224300, "gramsPerUnit": 0.0669, "weight": 1.000, "predKg": -0.42}],
 "debug": {"now": 1795000000, "input": {...}, "tracker": {...}, "constants": {...}, "calc": {...}, "loop": {...}, "save": {...}}}
```

`debug` (only in `GET /api/pellets`) holds everything the estimate is made of, for troubleshooting (the web app shows it in the *Pellet estimate* tile in debug mode):

| Key | Content |
|---|---|
| `input` | `sensor` (RAM 0xA4) and `sensorUse`: `low` (5, counts as the alarm), `ok` (0, counts as the refill after an alarm) or `ignored` (other values); `auger` (RAM 0x0D) and `augerCounted` (capped at 12); `stoveState`, `linkDown`, `dataAgeS` |
| `tracker` | the stored counter (NVS `pellet`): `units`, `cycleStart` (units at the last alarm, < 0 = none yet), `cycleKg`, `low`, `lowSince` / `okSince` (confirmation running), `lowUnits`, `lastT`, `pendingKg`, `refillKg`, `nCycles`, `version` |
| `constants` | `lowConfirmS` (60), `refillConfirmS` (30), `maxStepS` (30), `augerCycleS` (12), `maxCycles` (10), `recentWeight` (0.75) |
| `calc` | `kgPerUnit`, `unitsSinceAlarm`, `motorHoursSinceAlarm` (units / 12 / 3600), `kgBurnedSinceAlarm`, `remainingUnclamped` (< 0 = more burned than refilled), `unitsPerHourNow`, `kgPerHourAverage` |
| `loop` | since the controller started: `ticks`, `lastTickAgoS`, `lastDtS`, `maxDtS`, `capped` / `cappedLostS` (gaps over 30 s while the auger fed, and the seconds not counted), `feedS`, `preloadS`, `unitsSinceStart`, `skipped` (`clock`, `link`, `state`, `sensor`), `lastSkip`, `lastEvent` (`trip` / `refill`) and `lastEventT` |
| `save` | `agoS` (last NVS write), `unsavedUnits`, `bytes` |

`lastRefillAt` / `lastRefillKg`: time (Unix) and amount of the last refill, detected or entered with `add` (`null` before the first one). `hoursLeft` is the time at the current auger value (`null` while the auger does not feed); `hoursLeftAverage` and `alarmExpectedAt` use the average auger value of the hourly history of the last 7 days (from 6 hours of history). After the alarm the stove's own countdown takes over (`pelletReserve` in `/api/state`). `/api/state` has the same estimate under `pellets` (without the calibration detail); MQTT publishes `pellets_kg` and `pellets_hours` (at the average use).

**Use per day and cost.** The controller keeps the auger units of every local day for up to 400 days (SPIFFS `/pellet_days.bin`; the days before this existed are filled in from the 7-day hourly history). Kilograms always use the calibration of now, so older days get more accurate with every cycle too. `usage` in `GET /api/pellets`: `pricePerBag`, `bagKg`, `pricePerKg`, `stockBags`, `days` (closed days kept), `unitsToday`, and `last7` / `last30` / `last365` with `days`, `kg`, `kgPerDay`, `cost`; `stockDays` = (bags × bag kg + what is left above the alarm level) / kg per day of the last 7 days. `location` (`{lat, lon}` or `null`): kept for the web app, which fetches the daily mean outdoor temperature from [Open-Meteo](https://open-meteo.com) itself (the controller makes no request) and compares it with the use per day: trend per degree colder (from 10 days), kg per heating degree day (base 18 °C) and the expected use of the next 7 days. Every refill (detected or entered with `add` while not low) takes its kg from `stockBags`. `/api/state` → `pellets` has `kgToday`, `kgPerDay` (7 days), `costPerMonth` (kg per day × 30.44 × price per kg) and `stockBags`; `null` while not calibrated / no price / no stock. MQTT: `pellets_kg_today`, `pellets_kg_per_day`, `pellets_cost_month`, `pellets_stock_bags`; metrics: `stove_pellets_kg_today`, `stove_pellets_kg_per_day`, `stove_pellet_stock_bags`.

### `/api/pellets/days`

`GET /api/pellets/days?days=31` (1 – 400): the use per day, oldest first, today (so far) last. `kg` and `cost` use the calibration and price of now (`null` before the first calibration / without a price).

```json
{"kgPerUnit": 0.000061239, "pricePerKg": 0.363,
 "days": [{"d": "2026-11-22", "units": 102240, "kg": 6.26, "cost": 2.27}, {"d": "2026-11-23", "units": 63000, "kg": 3.86, "cost": 1.40}]}
```

### `/api/bluetooth`

Bluetooth (BLE) can be switched off to give WiFi the radio to itself. A firmware built without Bluetooth ([`-DOPB_BLUETOOTH=0`](development.md#building-and-uploading)) answers `"available":false` and refuses `enabled=true` (409). The setting is stored in NVS and applied at the next boot: with BLE off, `setupBLE()` and `bleLoop()` are skipped, the BT controller memory is released and WiFi modem sleep is disabled. The iOS app then only works via WiFi.

```bash
curl http://openpelletbridge.local/api/bluetooth
# {"enabled":true,"active":true,"restartRequired":false}

curl -X POST "http://openpelletbridge.local/api/bluetooth?enabled=false&restart=true"
# {"result":"success - restarting","enabled":false,"active":true,"restartRequired":true}
```

| Parameter | Values |
|---|---|
| `enabled` | `true`, `false` |
| `restart` | `true` to restart the controller right away when the setting changed (the stove keeps running) |

### `/api/mqtt`

MQTT settings and connection status, see [MQTT and Home Assistant](mqtt.md#mqtt-and-home-assistant). Stored in NVS; a change reconnects right away. The password is never returned.

```bash
curl -X POST "http://openpelletbridge.local/api/mqtt?enabled=true&host=192.168.1.10&user=ha&password=secret"
curl http://openpelletbridge.local/api/mqtt
# {"enabled":true,"host":"192.168.1.10","port":1883,"user":"ha","passwordSet":true,"prefix":"openpelletbridge",
#  "discovery":true,"discoveryPrefix":"homeassistant","clientId":"openpelletbridge_a1b2c3","connected":true,
#  "lastError":null,"publishes":42,"commands":0}
```

| Parameter (POST) | Values |
|---|---|
| `enabled` | `true` / `false` |
| `host`, `port` | Broker address, port (default 1883) |
| `user`, `password` | Optional. An empty `password` keeps the stored one, `-` clears it |
| `prefix` | Topic prefix, default `openpelletbridge` |
| `discovery`, `discoveryPrefix` | Home Assistant discovery on / off (default on), prefix (default `homeassistant`) |

Only the given parameters change.

### `/api/buzzer`

Only on the OpenPelletBridge board (S3, `BOARD_HAS_BUZZER`); the DevKit firmware answers 404. The buzzer on the board, not the stove's own beep ([`/api/beep`](#apibeep)). Settings are stored in NVS and apply at once.

```bash
curl http://openpelletbridge.local/api/buzzer
# {"enabled":true,"volume":100,
#  "quietHours":{"enabled":false,"from":"22:00","to":"07:00","alarms":true,"now":false},
#  "events":{"button":{"enabled":true,"preset":"click"},"success":{"enabled":true,"preset":"rising"},
#            "reserve":{"enabled":true,"preset":"two_tone"},"countdown":{"enabled":true,"preset":"tick"},
#            "alarm":{"enabled":true,"preset":"alarm"}},
#  "presets":["click","beep","double","rising","falling","two_tone","chime","tick","pip","long","alarm","siren"],
#  "playing":null,"muted":false}

curl -X POST "http://openpelletbridge.local/api/buzzer?volume=60&quiet=true&quietFrom=22:30&quietTo=07:00"
curl -X POST "http://openpelletbridge.local/api/buzzer?play=chime"
curl -X POST "http://openpelletbridge.local/api/buzzer?test=countdown"
curl -X POST "http://openpelletbridge.local/api/buzzer?stop=1"
```

| Parameter (POST) | Values |
|---|---|
| `enabled` | `true` / `false`: all sounds of the events on / off |
| `volume` | `0` – `100` (%; 100 = 50 % PWM duty, the loudest) |
| `quiet`, `quietFrom`, `quietTo` | Quiet hours on / off, from / to `HH:MM` local time (across midnight when from > to) |
| `quietAlarms` | `true`: alarm and countdown still sound in the quiet hours |
| `<event>` | `true` / `false` per event: `button`, `success`, `reserve`, `countdown`, `alarm` |
| `<event>Preset` | The sound of the event, one of `presets`, e.g. `alarmPreset=siren` |
| `play` | Play a preset now (a test: also when the buzzer is off or in quiet hours; a repeating preset 3 times) |
| `test` | Play the sound of an event (after the settings in the same request); `countdown` plays a 12 s countdown |
| `stop` | Stop the sound; a sounding error countdown / alarm stays silent until the error is gone |

Only the given parameters change; the answer is the state as with GET, plus `result`.

| Event | When | Default |
|---|---|---|
| `button` | Short press of the button on the board (while an error sounds: silences it instead) | `click` |
| `success` | A new firmware is confirmed after an update (2 min running with WiFi, [rollback](debugging.md)) | `rising` |
| `reserve` | The stove's pellet reserve alarm starts (`lowPalletAlarm`) | `two_tone` |
| `countdown` | The stove counts down to act on an error (RAM 0x30, ~55 s): a beep every second, every 0.5 s in the last 10 s, every 0.25 s in the last 5 s. Not for errors shorter than 5 s and not in the start-up check (state 1) | `tick` |
| `alarm` | Alarm state (8 – 10), a confirmed error, or pellets used up: repeats until it clears or is silenced | `alarm` |

A higher event interrupts a lower one (alarm > countdown > reserve > success > button); a test plays above all.

### `/api/wifi`

WiFi status, the WiFi network and the IP settings. Hostname and IP settings are stored in NVS (shared with the BLE and telnet menus) and take effect after a restart; a network change is done right away. Without any working network, hold the reset button 5 s for the WiFiManager portal.

```bash
curl http://openpelletbridge.local/api/wifi
# {"connected":true,"ssid":"Home","bssid":"…","channel":6,"rssi":-61,"quality":78,"ip":"192.168.50.222","netmask":"255.255.255.0",
#  "gateway":"192.168.50.1","dns":"192.168.50.1","mac":"…","hostname":"openpelletbridge","sleep":false,
#  "saved":{"hostname":"openpelletbridge","mode":"dhcp"},"restartRequired":false}

curl -X POST "http://openpelletbridge.local/api/wifi?mode=static&ip=192.168.50.30&netmask=255.255.255.0&gateway=192.168.50.1&dns=192.168.50.1&restart=true"
```

| Parameter (POST) | Values |
|---|---|
| `hostname` | 1 – 32 letters, digits or `-` |
| `mode` | `dhcp` or `static` (then `ip`, `netmask`, `gateway`, optional `dns`; checked: valid netmask, gateway in the same subnet) |
| `restart` | `true` to restart right away |
| `ssid`, `password` | Switch to another WiFi network now (password empty for an open network, else 8 – 63 characters). Uses DHCP on the new network. Handled on its own: other parameters in the same request are ignored |

`GET /api/wifi/scan` lists the networks in range: `{"scanning":true}` while the scan runs (a few seconds, poll again), then `{"scanning":false,"networks":[{"ssid":"Home","rssi":-61,"quality":78,"channel":6,"secure":true}]}`, strongest first per network. `?refresh=true` starts a new scan.

**Lock-out protection:** when the controller cannot connect to a new network within 25 s, or nobody reaches it there within 5 minutes, it restores the previous network and IP settings and restarts (`lastChange` tells what happened). After a restart with a new static address the controller waits up to 5 minutes for any HTTP request (opening the web app on the new address is enough; a telnet session counts too). Without one it switches back to DHCP and restarts. `confirmSecondsLeft` shows the remaining time.

### `/api/telnet`

Switches the telnet debug menu (port 23, IPv4 and IPv6, see [Telnet menu](mqtt.md#telnet-menu)) on or off. Applied immediately, no restart, and stored in NVS. Switching it off closes an open session.

```bash
curl -X POST "http://openpelletbridge.local/api/telnet?enabled=false"
# {"result":"success","enabled":false}
```

### `/api/leds`

Only on the OpenPelletBridge board (two WS2812B LEDs; the ESP32 DevKit answers `404`). Settings are applied immediately and stored in NVS. `brightness` is a percentage of the driver's cap: 100 % = 40 % PWM, so both LEDs at full white stay well within the USB fuse (F1) next to WiFi and the buzzer. Night off uses local time and needs the clock (NTP); `nightFrom` = `nightTo` means no night. `sys` / `stove` show the state each LED shows now (short overlays such as the button blink not included).

```bash
curl http://openpelletbridge.local/api/leds
# {"enabled":true,"brightness":25,"maxPwmPercent":40,"night":false,"nightFrom":22,"nightTo":7,"nightActive":false,
#  "sys":"connected","stove":"working","stoveWarning":false}

curl -X POST "http://openpelletbridge.local/api/leds?brightness=10&night=true&nightFrom=23&nightTo=6"
```

| Parameter (POST) | Values |
|---|---|
| `enabled` | `true` / `false` (default `true`) |
| `brightness` | 1 – 100 (default 25) |
| `night`, `nightFrom`, `nightTo` | LEDs off from `nightFrom` to `nightTo` (hours 0 – 23, default off, 22 – 7) |

Only the given parameters change. Colours and patterns (one table in src/statusLed.h; patterns repeat every 2 s):

| LED | State (`sys` / `stove`) | Colour | Pattern |
|---|---|---|---|
| LED1 system | `booting` | blue | solid, from the start until WiFi is known |
| | `connecting` | blue | 1 s on / 1 s off: WiFi (re)connecting |
| | `portal` | purple | double flash: config portal (access point) |
| | `connected` | green | solid |
| | `update` | cyan | 4 Hz: firmware update (GitHub, `/api/update`, espota, Bluetooth) |
| | `error` | red | double flash: the last restart was a crash, watchdog or brown-out, or an update was rolled back; a short press of the button clears it |
| | button pressed | white | 150 ms |
| | reconnect, full reset | amber | short flashes |
| LED2 stove | `off` | – | stove off, or not read yet |
| | `standby` | green | 125 ms blip every 2 s |
| | `starting` | orange | 1 s on / 1 s off: starting, pellet loading, ignition |
| | `working` | orange | solid |
| | `cleaning` | blue | solid: brazier cleaning |
| | `extinguishing` | blue | 1 s on / 1 s off: final cleaning |
| | `alarm` | red | 4 Hz: alarm state, a stove error (`currentError`), pellets used up |
| | `linkDown` | magenta | 1 s on / 1 s off: the stove does not answer |
| | warning (`stoveWarning`) | yellow | double flash in the first 0.5 s of every 2 s, over the state: pellet reserve running, an error counting down (`pendingError`), remote sensor not used |

### `/api/https`

HTTPS on port 443, next to HTTP on port 80. A task on the controller handles TLS and passes the requests on to the same web server, so every endpoint works over both. The certificate is **made on the controller**: its own ECDSA P-256 key (generated with the hardware random generator, kept in NVS, never sent anywhere; no seed in the public firmware), CN and subjectAltName = hostname, hostname.local and the IP address of the device, plus up to 8 extra names (`altNames`: host names, `*.` wildcards or IPv4 addresses), serverAuth, valid 820 days. It is re-issued with the same key when the hostname, the IP or the extra names change, or within 30 days of expiry. Self-signed: browsers warn once; compare the SHA-256 fingerprint, or install the certificate (`?cert=pem`) as trusted on your own devices.

**Redirect** (`redirect`, default off; on in firmware before 20/11/2026): the web app pages (`/`, `/app`, `/device`) answer HTTP with a 307 to `https://`; the API (`/api/…`, `/metrics`, the firmware upload) stays on HTTP, for Home Assistant, Prometheus, scripts and PlatformIO, which do not follow a redirect to a self-signed certificate. No redirect while HTTPS is off or not ready.

One TLS session at a time. Its two record buffers (~34 KB) are reserved once at start-up, while the heap is still in one piece, and reused for every connection; they are handed back only while the GitHub update runs (its own TLS connection) or while HTTPS is off. With HTTPS on, the automatic GitHub check therefore runs once at start-up, before the buffers are reserved (HTTPS connections wait for it, at most ~90 s), and not daily: after a check the heap was sometimes too fragmented to reserve the buffers again. The check button still works. If they cannot be reserved, connections wait and `stats.refusedLowMemory` counts the attempts; HTTP keeps working. Key exchange X25519 (P-256 as fallback), certificate ECDSA P-256; session resumption keeps later connections of the same browser fast (~60 ms instead of ~1.5 s).

```bash
curl -k https://openpelletbridge.local/api/https          # -k: self-signed
# {"enabled":true,"redirect":true,"altNames":[],"listening":true,"port":443,"url":"https://openpelletbridge.local/",
#  "certificate":{"subject":"CN=openpelletbridge","key":"ECDSA P-256","sha256":"3F:A2:…",
#   "validFrom":"2026-11-18T21:50:00Z","validUntil":"2029-02-15T21:50:00Z",
#   "san":["openpelletbridge","openpelletbridge.local","192.168.50.222"]},
#  "stats":{"sessions":12,"failedHandshakes":1,"refusedLowMemory":0,"lastHandshakeMs":1500,"maxHandshakeMs":1600,
#   "minFreeHeapDuringSession":26480,"freeHeap":61204,"largestBlock":32756,"stackFreeMin":3100},"problem":null}
curl -k -o openpelletbridge.crt "https://openpelletbridge.local/api/https?cert=pem"   # the certificate (PEM)
curl -X POST "http://openpelletbridge.local/api/https?enabled=false"                    # switch off / on (immediately)
curl -X POST "http://openpelletbridge.local/api/https?regenerate=true"                  # new key and certificate
curl -X POST "http://openpelletbridge.local/api/https?redirect=false"                   # no redirect of the web app pages
curl -X POST "http://openpelletbridge.local/api/https" --data-urlencode "altNames=pellet.thuis.lan,192.168.1.50"   # extra names (empty: none)
```

Log module `https` (set with `/api/loglevel`): certificate made / loaded / re-issued, listening, refused connections, slow handshakes; at debug level every connection (client, cipher suite, handshake time, bytes, why it ended).

### `GET /metrics`

Prometheus text format. Contains:

- ESP32: uptime, CPU frequency, heap, flash/sketch size, build (`esp32_build_info{…,board="opb-s3"}`)
- Board (OpenPelletBridge board only): power source `esp32_power_from_stove` (1 stove, 0 USB), `esp32_power_switchovers_total`, chip temperature `esp32_chip_temp_c`
- WiFi: connected, RSSI, channel, TX power
- HTTP: request counters per method and per endpoint
- Serial bus: read/write errors after retries, number of retries
- Stove: state, temperatures, wireless sensor setting (`stove_wireless_sensor_enabled`), flame power, fan speed, fume fan rpm, auger on-time (`stove_auger_speed`, s) and share of the time it runs (`stove_auger_percent`, %), energy saving, low pellet alarm, remote last seen, timers, stove/NTP time delta, firmware version, serial number (`stove_serial_number_info{serial="…"} 1`)

Example scrape config:

```yaml
scrape_configs:
  - job_name: openpelletbridge
    static_configs:
      - targets: ["openpelletbridge.local:80"]
```

### Legacy routes

Kept for backwards compatibility. Use the `/api/...` routes for new integrations. `/state`, `/flame`, `/fan` and `/get` share the handler (and allowed methods) of their `/api` counterpart; the other legacy routes accept any HTTP method.

| Route | Replacement / behaviour |
|---|---|
| `/on`, `/off` | `POST /api/power?state=on\|off`. Response nests the state under `stoveState`. |
| `/state` | `GET /api/state` |
| `/flame` | `/api/flame` |
| `/fan` | `/api/fan` |
| `/get` | `GET /api/dump` |
| `/set?type=ram\|eeprom&int=<addr>` | Reads **one** register (decimal, or `address=0x..`). Despite the name nothing is written. `Value_txt` is decoded like `/api/dump`. `400` on an invalid address, `500` on a serial error. |
| `/error?0=<id>&1=<id>…` | Write error memory by index 0 – 4. Use `POST /api/errors`. |
| `/cq?s=<raw>` | Write the raw combustion quality value. Use `POST /api/burn`. |
| `/display?menu=<id>` | Force the display to a menu id. |

## Reference tables

### Stove states

| `stoveState` | `state` | `poweredOn` |
|---|---|---|
| -1 | No answer (serial link down) | `null` (unknown) |
| 0 | Off | false |
| 1 | Starting | true |
| 2 | Pellet loading | true |
| 3 | Ignition | true |
| 4 | Working | true |
| 5 | Brazier cleaning | true |
| 6 | Final cleaning | false |
| 7 | Standby | true |
| 8 | Alarm State | false |
| 9 | Ignition failure | false |
| 10 | Alarm | false |

### Error codes

| id | Code | Description |
|---|---|---|
| 0 | `---` | No error |
| 1 | E8 | Fume sensor error |
| 2 | E4 | Fume temperature too high |
| 4 | E9 | Ignition temperature not reached within limit |
| 8 | E7 | Fume temperature too low |
| 16 | E3 | Temperature inside stove too high |
| 32 | E1 | Pellet loading door open too long, or wrong air pressure inside the stove |
| 64 | E2 | Pressure sensor error during ignition |
| 128 | E6 | Auger error, too many pellets dispatched |
| 129 | E12 | Fume fan error |
| 130 | E11 | Unknown, not described in the manual |
| 132 | E19 | External contact N.PEL / pellet is active |
| 136 | E13 | Unknown, not described in the manual |
| 255 | PowerError | Display shows power error |

The `desc` strings in API responses are returned as defined in the firmware and may differ slightly in wording from this table.
