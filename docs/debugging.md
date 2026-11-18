# Diagnostics and firmware updates

[← README](../README.md)

Tools to find out what the controller and the stove do: the serial link state, the live logs, crash information, and the firmware updates.

## Serial link to the stove

The Stove library keeps track of the link: after 2 transactions in a row that failed after all 3 attempts the link is **down**. Then every read and write fails at once without touching the bus (web routes answer `503 The stove does not answer`, `/api/health` `500`), and only the sensor task probes the state register every 5 s. One answer brings the link up again. `/api/state` → `serial.linkUp`, `serial.skipped`, `serial.transactions`; metrics `stove_serial_link_up`, `stove_serial_skipped_total`, `stove_serial_transactions_total`. Losing and regaining the stove are logged (`stove`: error / info).

## Live debugging

Two cursor based routes behind the web app's *Live debugging* card (Debug mode). Start with `since=0` and pass the `next` of each answer as the next `since`. `lost: true` means the cursor was older than the buffer and entries were overwritten; a cursor from before a restart starts again at the oldest entry.

`GET /api/log` returns the firmware log: everything the firmware writes to its serial port (`Serial`, UART0) (also ESP-IDF log output), each line prefixed with the time, from an 8 KB ring buffer in RAM. Output that precompiled libraries (WiFiManager, NimBLE) write to the UART themselves is not included. At most 4 KB per answer.

```bash
curl "http://openpelletbridge.local/api/log?since=0"
# {"from":0,"next":1834,"lost":false,"text":"14:02:11 Ready - firmware v1.1.0 for Piazzetta P220 M\n…"}
```

`/api/busmon` is the stove bus monitor: every read and write between the controller and the stove. It only records while it is watched: `POST ?enabled=true` starts it, every `GET` keeps it running for another 60 s, and it stops by itself 60 s after the last `GET` (or with `POST ?enabled=false`). At most 250 events per answer, 600 kept (about half a minute of traffic).

```bash
curl -X POST "http://openpelletbridge.local/api/busmon?enabled=true"
curl "http://openpelletbridge.local/api/busmon?since=0"
# {"active":true,"now":5023112,"from":0,"next":42,"lost":false,
#  "fields":["ms","write","eeprom","address","value","ok","attempts","text","tx","rx"],
#  "events":[[5022050,0,0,33,4,1,1,"Working","0021","00212504"],[5022101,1,1,125,21,1,1,"21 °C (setpoint room temp)","A07D1532","A07D15323215"], …]}
```

`ms` is the controller uptime (`now` is the uptime at the answer, to convert to clock time), `write` 1 = write, `eeprom` 1 = EEPROM (0 = RAM), `value` the value read or written, `ok` 0 = failed after all `attempts`, `text` what the value means (the same description as `Value_txt` in `/api/dump`; empty for an unknown register or a failed read), `tx` / `rx` the raw bytes of the (last) attempt as hex: `tx` sent (read `0021` = RAM, address 0x21; write `A07D1532` = 0x80 + EEPROM, address, value, checksum), `rx` received: the echo of `tx` followed by the stove's checksum and value (`00212504`). A shorter `rx` means a timeout.

**Log levels.** Firmware messages are tagged per module and level: `I [mqtt] connected to …`, `W [wifi] connection lost: BEACON_TIMEOUT (200)`. Each module prints up to its level (`off`, `error`, `warn`, `info`, `debug`; default `info`, `bus` `warn`), kept in NVS:

```bash
curl http://openpelletbridge.local/api/loglevel
# {"levels":{"sys":"info","wifi":"info","mqtt":"info","stove":"info","bus":"warn","update":"info","http":"info","history":"info"},"modules":[…]}
curl -X POST "http://openpelletbridge.local/api/loglevel?wifi=debug&bus=debug"
```

| Module | Messages |
|---|---|
| `sys` | start-up, restart reasons, SPIFFS use, clock (daylight saving changes, NTP lost / back; `debug`: every NTP sync), log level changes, telnet. Every 15 min a **health** line: uptime, free heap (lowest, largest block), WiFi signal, stove bus transactions / retries / failures, sensor cycle times and the longest wait for the bus; warnings for low memory, a noisy or failing bus and a weak WiFi signal |
| `wifi` | connection, IP, drops with the reason (`debug`: every failed reconnect attempt) |
| `mqtt` | connection with the broker's answer in words (wrong password, not reachable, …), connection lost, the command topic, Home Assistant discovery (number of entities), commands received, failed publishes. `debug`: every published topic and value |
| `stove` | every **write** to the stove with what it means and **who** did it (web/API with IP and route, MQTT command, Bluetooth app, telnet, or the controller itself); state changes with their duration and **probable cause** (power command and its source, timer program, energy saving with the room temperature, brazier cleaning, error); errors and when they clear; setting changes; low pellets; stove clock more than 60 s off NTP; remote control not heard for 30 min; slow sensor cycles and long waits for the bus. `debug`: measured values when they change (old -> new): temperatures, fumes (steps of 5 °C), actual flame power and room fan, fume fan start / stop / speed, auger, brazier cleaning countdown |
| `bus` | stove reads / writes that failed after all attempts (`debug`: failed attempts that were retried, and every frame while the telnet bus debug is on) |
| `update` | firmware updates (GitHub, upload, ArduinoOTA) |
| `http` | web server; requests refused for a missing API key (401), stove bus busy (503) and slow requests (> 5 s). `debug`: access log (method, path, parameters with secrets masked, client IP) and the duration of each request; the live log's own polling is left out |
| `history` | history files: what was found at start-up, failed writes |
| `ble` | Bluetooth: connections, commands received, BLE OTA (`debug`: subscriptions and notifications) |

**Log of the previous run.** The last 2 KB of the log are also kept in RTC memory, which survives a restart, a crash and a watchdog reset (not a power cut). After such a restart the live log starts with `===== log before the restart (<reason>) =====` and those lines, so you can see what happened just before.

**Core dump.** After a crash or task watchdog reset the ESP-IDF stores a core dump in flash (64 KB partition). At the next start the log shows `core dump of the crash: task …, PC 0x…, cause …, backtrace 0x… 0x…`. `GET /api/coredump` returns that summary, `?download=true` the raw dump, `DELETE` erases it. Decode with the `firmware.elf` of the same build (CI artifact `openpelletbridge-elf-<version>`), step by step in [Finding a bug with the core dump](#finding-a-bug-with-the-core-dump):

```bash
curl -H "X-API-Key: <key>" -o coredump.bin "http://openpelletbridge.local/api/coredump?download=true"
esp-coredump info_corefile -t raw -c coredump.bin firmware.elf
xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf 0x400d1234 0x400d5678   # only the backtrace
```

**Large answers.** The web app, the spec, `/api/state`, `/api/history`, `/api/events`, `/metrics` and the logs are sent in pieces; before each piece the controller checks that the client still takes data. A client that stops reading for 3 s (closed tab, weak WiFi) gets its connection closed (`W [http] …: the client took no data for 3 s - answer stopped`) instead of blocking the controller. The task watchdog (10 s) resets the controller when the main loop hangs anyway.

**Restart reason.** `/api/state` → `controller.resetReason` says why the controller last started: the ESP32 reset reason (`power on`, `software restart`, `crash (panic)`, `task watchdog`, `brown-out (supply voltage dropped)`, …) plus, for a restart the firmware did itself, what caused it (`firmware update from GitHub`, `restart requested (web app / API)`, `WiFi switch reverted`, …). After a crash or watchdog reset it also says where the loop task was (a breadcrumb in RTC memory: the step of the main loop, or the web request being handled) and, for the task watchdog, how long that step had been running and which task was running on each CPU, e.g. `task watchdog – loop task was in: http GET /api/dump (for 5.2 s); running: CPU0 IDLE0, CPU1 loopTask`. `controller.bootCount` and `controller.crashCount` (starts after a crash, watchdog or brown-out) are kept in NVS. Metrics: `esp32_boot_count`, `esp32_crash_count`, `esp32_reset_info{reason="…"}`, `esp32_wifi_disconnects_total`. `/api/wifi` has `disconnects` (drops of a working connection since the start) and `lastDisconnect` (`reason`, `code`, `secondsAgo`).

## Finding a bug with the core dump

The summary in the log already names the crashing task and the program counters of its call stack; the ELF file of the same build turns those into function names and source lines.

1. **Note the crash.** After the restart the live log shows, for example:
   `E [sys] core dump of the crash: task loopTask, PC 0x400d8a1c, cause 28, backtrace 0x400d8a19 0x400d9b2e 0x400e1f04`.
   `GET /api/coredump` returns the same, plus `thisBuild`: `true` when the dump comes from the firmware that is running now.
2. **Get `firmware.elf` of exactly that build.** For a release: the CI artifact `openpelletbridge-elf-<version>` of the run that built the tag (`gh run download <run-id> -n openpelletbridge-elf-v1.1.7`). For your own build: `.pio/build/<env>/firmware.elf`, as long as you did not rebuild after flashing. Another build gives wrong function names.
3. **Quick look: the backtrace.**
   ```bash
   ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf 0x400d8a19 0x400d9b2e 0x400e1f04
   # example output (one line per address):
   # 0x400d8a19: pelletsJson(ArduinoJson::JsonObject, bool) at src/pellets.h:112
   # 0x400d9b2e: getStatus() at src/status.h:101
   ```
   The first line is where it crashed, the next ones who called it.
4. **Full analysis: all tasks, registers, variables.**
   ```bash
   curl -H "X-API-Key: <key>" -o coredump.bin "http://openpelletbridge.local/api/coredump?download=true"
   pip install esp-coredump
   esp-coredump info_corefile -t raw -c coredump.bin firmware.elf   # every task with its call stack
   esp-coredump dbg_corefile  -t raw -c coredump.bin firmware.elf   # gdb: bt, frame <n>, info locals, p <variable>
   ```
5. **Read the cause** (`cause`, Xtensa exception): 28 *LoadProhibited* and 29 *StoreProhibited* = reading or writing through a bad pointer (often null: look for a pointer or an index in the crashing line); 0 *IllegalInstruction* = a jump to a bad address (corrupted stack or function pointer); 3 *LoadStoreError* = wrong access to flash / IRAM. A task watchdog reset is not an exception: the dump then shows the task that was running when the watchdog fired (often `IDLE`), so use `info_corefile` to see where `loopTask` was waiting, together with the breadcrumb in `resetReason`.
6. **Erase it** when done (`DELETE /api/coredump`), so the next crash is not mixed up with this one. A new crash overwrites the dump anyway; a brown-out or power cut writes none.

## `POST /api/update`

Installs new firmware over HTTP. The image is streamed in one request and written to the OTA partition while it arrives, which is several times faster than ArduinoOTA (espota sends 1 KB at a time and waits for an acknowledgement after every block). The controller restarts after a successful update; the stove is not affected.

Send the `firmware.bin` as multipart field `firmware`. The request always needs the API key (`X-API-Key` / `Authorization: Bearer`), also when the key is not required for other routes. The fixed OTA password of older firmware (`X-OTA-Password`, in `platformio.ini`) is gone: it was public in this repository. ArduinoOTA and the Bluetooth OTA also use the API key as password.

```bash
curl -H "X-API-Key: <key>" -F "firmware=@.pio/build/esp32doit-devkit-v1-http/firmware.bin" http://openpelletbridge.local/api/update
# {"result":"success","bytes":1608157,"restarting":true}
```

Errors: `401` (no valid key), `500` with `result` describing the failure (nothing is activated, the current firmware keeps running). The web app has an *Upload OpenPelletBridge firmware* button under *Device* (expert mode).

## `/api/firmware`

New versions are published as [GitHub releases](https://github.com/netdata-be/PalletHeater-REST-API/releases) (see [CI and releases](development.md#ci-and-releases)). The controller looks up the latest release (`releases/latest`: no drafts or pre-releases) 2 minutes after start-up and then once a day (`autoCheck`, on by default), or on request. It **never installs on its own**: `action=install` downloads the asset `openpelletbridge-firmware.bin` over HTTPS, the ESP-IDF verifies the image before it is made bootable, and the controller restarts. Settings (NVS) are kept; the stove is not affected.

HTTPS to GitHub is verified against the Mozilla root CAs in [certs/x509_crt_bundle.bin](../certs/x509_crt_bundle.bin) (regenerate with [scripts/gen_ca_bundle.py](../scripts/gen_ca_bundle.py)). The check needs a valid clock (NTP).

```bash
curl http://openpelletbridge.local/api/firmware
# {"current":"v1.0.0","devBuild":false,"repo":"netdata-be/PalletHeater-REST-API","state":"idle","autoCheck":true,
#  "latest":"v1.1.0","published":"2026-11-20T10:00:00Z","releaseUrl":"https://github.com/…/releases/tag/v1.1.0",
#  "assetAvailable":true,"updateAvailable":true,"checkedSecondsAgo":42,"lastError":null,"lastInstall":null}

curl -X POST "http://openpelletbridge.local/api/firmware?action=check"     # {"result":"started",…}
curl -X POST "http://openpelletbridge.local/api/firmware?action=install"   # {"result":"started",…}
curl -X POST "http://openpelletbridge.local/api/firmware?autoCheck=false"
```

| Field | Meaning |
|---|---|
| `current` | Running version (`git describe` at build time) |
| `devBuild` | The running version is not exactly a release tag (commits after the tag, uncommitted changes or no tag at all). A build without a tag is never offered an update; install a release over it with `action=install&force=true`. |
| `state` | `idle`, `checking`, `downloading`, `restarting` or `error` (`lastError`) |
| `progress` | `{done,total}` bytes while downloading |
| `updateAvailable` | `latest` is newer than `current` and has the firmware asset |
| `install` | What installing `latest` would do: `update` (newer), `reinstall` (same version), `downgrade` (older than this build, e.g. a development build made after that release: the web app then offers *Back to vX*), `unknown` (this build has no release version) |
| `ahead`, `dirty` | For a version from `git describe`: commits after the release tag, and uncommitted changes in the build |
| `lastInstall` | Last update through this route, e.g. `v1.0.0 -> v1.1.0` |
| `lastAttempt` | Last step of an install that did not finish (the controller restarted or was powered off during it) or failed (`failed: …`), with free heap, largest heap block, WiFi signal and uptime at that moment. Kept in NVS, so it survives a power cut; removed when an install succeeds. Logged at the next start. |
| `freeHeap`, `largestBlock` | Memory now. HTTPS needs about 45 KB free with a 20 KB block; with less, check and install are refused ("restart the controller and try again") instead of risking a hang. |

`action=install` without `force` is refused (`400`) when there is no newer release; `409` while a check or update is already running. Poll `GET` to follow the progress; the controller is unreachable for about a minute while it restarts. The web app shows this under *Device* → *OpenPelletBridge firmware*.
