# MQTT, Home Assistant and other interfaces

[← README](../README.md)

MQTT with Home Assistant discovery, Prometheus metrics, Bluetooth and telnet.

## MQTT and Home Assistant

The controller can publish the stove values to an MQTT broker and take commands from it. Set it up in the web app (*Expert* → Device → MQTT) or with [`/api/mqtt`](api.md#apimqtt). The connection is retried every 15 s while the broker is unreachable.

Values are plain text (no JSON), retained, under `<prefix>/state/<name>` (prefix default `openpelletbridge`). They are published when they change (checked every 5 s) and all again every 5 minutes. `<prefix>/status` is `online`, or `offline` (last will) when the controller drops off.

| State topic | Value |
|---|---|
| `state`, `state_code` | Stove state text (`Working`, `Standby`, …) and code (see [Stove states](api.md#stove-states)) |
| `power` | `ON` / `OFF` |
| `mode` | `heat` / `off` (for the Home Assistant thermostat) |
| `room_temp`, `stove_temp`, `remote_temp`, `fume_temp` | °C |
| `setpoint` | °C, 7 – 30 |
| `flame_power`, `flame_actual` | Set and actual flame power, 1 – 4 |
| `fan_speed`, `fan_running`, `fan_power` | Room fan speed 0 – 3, `ON` / `OFF`, raw power (RAM 0x0A) |
| `energy_saving`, `es_stop`, `es_start` | `ON` / `OFF`, stop above and restart below the set point in °C (0 – 3) |
| `fume_fan_rpm`, `auger`, `auger_percent` | Fume fan rpm, auger on-time per cycle (s), share of the time the auger runs (0 – 100 %) |
| `brazier_minutes` | Minutes to the next brazier cleaning (empty when the stove is not working) |
| `error_active`, `error` | `ON` / `OFF`, the active error (`E9 Ignition …`) or `none` |
| `low_pellets` | `ON` / `OFF` |
| `warning_active`, `warning` | `ON` / `OFF`, the active warning (`REMOTE_SENSOR_NOT_USED …`) or `none`: the stove works, but regulates on its own sensor although the wireless sensor is on (remote control not heard) |
| `room_source` | The sensor the stove regulates on: `stove` or `remote` |
| `pellets_kg`, `pellets_hours` | Pellets left until the low pellet alarm (kg) and hours at the average use (empty until calibrated) |
| `pellets_kg_today`, `pellets_kg_per_day`, `pellets_cost_month`, `pellets_stock_bags` | Pellets burned today (kg), per day over the last 7 days, € per month (needs a price per bag) and bags in stock (empty when not known / not kept) |
| `hours_service`, `total_hours`, `starts`, `wifi_rssi` | Counters and WiFi signal (dBm) |

Commands go to `<prefix>/set/<name>`. The result is published (not retained) on `<prefix>/result`, e.g. `setpoint 21: ok`.

| Command topic | Payload |
|---|---|
| `power` | `ON` / `OFF` (also `1` / `0`, `true` / `false`) |
| `mode` | `heat` / `off` |
| `setpoint` | 7 – 30 (`21` or `21.0`) |
| `flame_power` | 1 – 4 |
| `fan_speed` | `off` or 0 – 3 |
| `energy_saving` | `ON` / `OFF`. Off sets both offsets to 0 (the stove has no separate switch); on restores the last offsets (default +1 / −1) |
| `es_stop`, `es_start` | 0 – 3 °C |

```bash
mosquitto_sub -h broker -t 'openpelletbridge/#' -v
mosquitto_pub -h broker -t openpelletbridge/set/setpoint -m 21
```

**Home Assistant.** With *discovery* on (default), the controller also publishes retained configuration messages under `homeassistant/<component>/<client id>/<entity>/config`. Home Assistant (MQTT integration) then creates a device *OpenPelletBridge* with a thermostat (on/off and set point, current room temperature), switches for power and energy saving, numbers for set point, flame power and the energy saving offsets, a select for the fan and sensors / binary sensors for the values above. Nothing else is Home Assistant specific.

**Loxone and others.** The state and command topics are plain MQTT and work with any client. Loxone needs an MQTT gateway for this (for example LoxBerry with the MQTT Gateway plugin) unless your Miniserver supports MQTT itself; check that for your Loxone version. Discovery can be switched off when Home Assistant is not used. Alternatively Loxone can call the REST API directly with virtual HTTP inputs and outputs.

The Home Assistant device carries the stove firmware as `hw_version` and the stove serial number (MATR) as `serial_number`, once they have been read.

## Other interfaces

| Interface | Details |
|---|---|
| BLE | GATT server `OpenPelletBridge`, see [BLE_API.md](../BLE_API.md). Also supports firmware updates over BLE ([ble_ota.py](../ble_ota.py)). |
| Telnet | Port 23 (IPv4 and IPv6), interactive menu for reading and writing values, see [Telnet menu](#telnet-menu) below. |
| ArduinoOTA | Hostname `Pelletheater`, port 3232, password protected. Slow; prefer [`/api/update`](debugging.md#post-apiupdate). |
| mDNS | `openpelletbridge.local`, service `_http._tcp`. |

## Telnet menu

A text menu on port 23 for diagnostics and control without a browser:

```bash
telnet openpelletbridge.local        # or the IPv4 / IPv6 address
telnet fe80::1234:5678:9abc:def0%en0 # link-local IPv6 (with the interface)
```

The controller listens on IPv4 and IPv6 (one dual-stack socket; if the network stack does not cover IPv4 with it, a second IPv4 socket is used, the log line `telnet running on port 23 (...)` says which). Use a real telnet client (macOS / Linux `telnet`, PuTTY in *Telnet* mode): the menu switches the client to character mode and echoes itself.

Every screen starts with a status header: stove state, room temperature and set point, flame, fan, the pellet estimate and warning lines for an error (also the controller-side `PELLETS` and `LINK` errors), an error the stove reports before it counts (with its countdown), low pellets / pellet reserve and a running firmware update.

| Menu | What it offers |
|---|---|
| Status | Everything at a glance, refreshes every 2 s: state, errors, temperatures, flame, room fan, fume fan, auger (s and %), energy saving, brazier cleaning, pellet estimate and reserve, remote control, stove clock, counters |
| Chart | The history as a line chart in the terminal (Unicode braille dots and ANSI colours, needs a UTF-8 terminal of at least 80 × 24): `1`-`5` temperatures (room, set point, stove sensor), fumes, auger %, flame power and room fan level, fume fan rpm; `+` / `-` zoom (3 h, 6 h, 12 h, 24 h from the 2-minute samples, 7 days from the hourly averages), left / right move in time, `n` back to now, `v` a taller chart. Under the chart: a strip with the stove state, the pellet events (L low, R refilled) and errors (E), and per series now / min / max / average in the period. Refreshes every 10 s |
| Live values | Chosen groups printed every 2 s (scrolls), e.g. to watch a start-up |
| Control | Power (asks for confirmation; refused while the pellets are used up, as on the API), set point, flame power, room fan, energy saving stop / restart, combustion and pellet quality, beep, stove clock to NTP |
| Timers | PR1 - PR6: enabled, on / off time, set point, flame, fan, days |
| Pellets | Estimate until the low pellet alarm, use today / per day / € per month, calibration cycles, pellets added by hand, refill amount, reset calibration, price per bag, bag weight, bags in stock (the same data as [`/api/pellets`](api.md#apipellets)) |
| Errors and history | Error memory (and clearing it), the event log, the last 3 hours of history |
| Network | WiFi, IPv4 and IPv6 addresses, drops, network watchdog; hostname and IP settings (saved like in the web app, used after a restart, with the same 5 minute confirmation: opening the web app *or* a telnet session on the new address confirms it) |
| Device | GitHub firmware update (check, install, automatic check); services: MQTT (on / off, broker, port, user, password, topic prefix, Home Assistant discovery and its prefix), HTTPS (on / off, redirect, extra certificate names, new key and certificate), Bluetooth (on / off, applies after a restart), API key (required or not, show it, make a new one), switching telnet off, and an overview with the counters; system (restarts, memory, bus counters), log levels per module, restart |
| Debug | Firmware log (live), bus frame debug and its buffer, raw register read / write |

Every menu item shows what it does, with a legend under the menu: `▸` opens a submenu, `≡` a screen, `↕` changes a value, `•` switches something on or off, `»` runs an action, `!` asks for confirmation first. On / yes / required are green, off / no grey, values you can change yellow. Service settings use the same code as the web app (the MQTT password is typed as `*`; showing the API key asks first, because telnet is not encrypted).

Keys: arrows (or tab) to move, Enter / right to open, `1`-`9` and `a`-`h` to open an item directly, Esc / left / backspace / `q` to go back (`q` in the main menu exits), `r` to redraw, `?` for help. A value is chosen with the arrow keys and only written to the stove on Enter; power, clearing the error memory, raw writes, IP settings and restarts ask for confirmation (`y`). Every write is logged with the source `telnet menu (<client address>)`.

One session at a time. Who connects while the menu is in use is told who uses it and can take that session over (`t`; the other client is told and disconnected) or leave; a third client gets a short message. A session without a key for 30 minutes is closed; a client that disappeared is found by TCP keepalive.

Telnet is not encrypted and **not protected by the API key**. Switch it off in the web app (Device card) or with [`/api/telnet`](api.md#apitelnet) when the network is not trusted.
