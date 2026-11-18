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
| `fume_fan_rpm`, `auger` | Fume fan rpm, auger speed |
| `brazier_minutes` | Minutes to the next brazier cleaning (empty when the stove is not working) |
| `error_active`, `error` | `ON` / `OFF`, the active error (`E9 Ignition …`) or `none` |
| `low_pellets` | `ON` / `OFF` |
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

## Other interfaces

| Interface | Details |
|---|---|
| BLE | GATT server `OpenPelletBridge`, see [BLE_API.md](../BLE_API.md). Also supports firmware updates over BLE ([ble_ota.py](../ble_ota.py)). |
| Telnet | Port 23, interactive menu for reading and writing values. |
| ArduinoOTA | Hostname `Pelletheater`, port 3232, password protected. Slow; prefer [`/api/update`](debugging.md#post-apiupdate). |
| mDNS | `openpelletbridge.local`, service `_http._tcp`. |
