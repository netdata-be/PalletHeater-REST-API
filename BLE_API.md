# PalletHeater BLE API

BLE can be switched off via `POST /api/bluetooth?enabled=false` or the web app (applied after a restart); the GATT server described here is then not started.

The ESP32 firmware exposes a GATT server over NimBLE-Arduino. It advertises under the name **PalletHeater** and contains two services: the stove control service and an OTA firmware-update service.

## Connection parameters

| Parameter | Value |
|---|---|
| Device name | `PalletHeater` |
| MTU | 185 bytes |
| TX power | +9 dBm |
| Advertising interval | 40 – 80 ms |
| Re-advertise after disconnect | 1 s backoff |
| Max simultaneous clients | 1 |

## Subscribe-push behaviour

Every characteristic that supports notifications implements **subscribe-push**: the moment the client enables the CCCD (subscribes), the firmware immediately sends a notification with the current value. This means the client does not need to issue a separate `readValue` after subscribing — subscribing is sufficient to receive the initial state.

On the first connection the firmware waits until all characteristics have been subscribed (approximately 4 s) before issuing a bulk force-notify of every characteristic.

Values are refreshed every 2 s. Only characteristics whose underlying value has changed since the last cycle are notified.

---

## Stove Service

**Service UUID:** `A1B20001-1234-5678-ABCD-EF0123456789`

Properties: **R** = Read, **W** = Write (with response), **N** = Notify, **WNR** = Write (no response).

### Stove State

| Field | Value |
|---|---|
| UUID | `A1B20002-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | `uint8` |

Read-only. Updated by the sensor task from the stove UART.

| Value | Meaning |
|---|---|
| 0 | Off |
| 1 | Starting |
| 2 | Pallet loading |
| 3 | Ignition |
| 4 | Working |
| 5 | Cleaning |
| 6 | Final cooling |
| 7 | Standby |
| 8 | Alarm |

---

### Power

| Field | Value |
|---|---|
| UUID | `A1B20003-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8` — `0` = off, `1` = on |

Write `1` to turn the stove on (only accepted when state is Off).  
Write `0` to turn it off (accepted when Ignition, Working, or Alarm; sends a soft-off when Starting/Loading).

Writes are applied asynchronously from `bleLoop()` via the write queue. There is a 150 ms minimum gap between consecutive stove UART operations.

---

### Temperature Setpoint

| Field | Value |
|---|---|
| UUID | `A1B20004-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8`, range 7 – 30 (°C) |

Values outside 7 – 30 are silently rejected. Stored in stove EEPROM.

---

### Fan Speed

| Field | Value |
|---|---|
| UUID | `A1B20005-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8`, range 0 – 3 |

| Value | Meaning |
|---|---|
| 0 | Off / auto |
| 1 | Low |
| 2 | Medium |
| 3 | High |

---

### Flame Power

| Field | Value |
|---|---|
| UUID | `A1B20006-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8`, range 1 – 4 |

Values outside 1 – 4 are rejected.

---

### Room Temperature

| Field | Value |
|---|---|
| UUID | `A1B20007-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | `int16` little-endian, value × 10 (e.g. `215` = 21.5 °C) |

Read-only sensor value.

---

### Fume Temperature

| Field | Value |
|---|---|
| UUID | `A1B20008-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | `uint8` (°C) |

Read-only sensor value.

---

### Beep

| Field | Value |
|---|---|
| UUID | `A1B20009-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8` — `0` = disabled, `1` = enabled |

Stored in stove EEPROM.

---

### Low Pallet Alarm

| Field | Value |
|---|---|
| UUID | `A1B2000A-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | `uint8` — `0` = OK, `1` = alarm |

Read-only.

---

### Fume Fan RPM

| Field | Value |
|---|---|
| UUID | `A1B2000B-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | `uint16` little-endian |

Read-only sensor value.

---

### Combustion Quality

| Field | Value |
|---|---|
| UUID | `A1B2000C-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8` raw stove register value |

| Raw | Adjustment |
|---|---|
| 2 | −3 |
| 3 | −2 |
| 4 | −1 |
| 5 | 0 (neutral) |
| 6 | +1 |
| 7 | +2 |
| 8 | +3 |
| 76 | +G (gas mode) |

---

### Pellet Quality

| Field | Value |
|---|---|
| UUID | `A1B2000D-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8`, range 0 – 10 |

| Raw | Adjustment |
|---|---|
| 0 | −5 |
| 1 | −4 |
| 2 | −3 |
| 3 | −2 |
| 4 | −1 |
| 5 | 0 (neutral) |
| 6 | +1 |
| 7 | +2 |
| 8 | +3 |
| 9 | +4 |
| 10 | +5 |

Stored in stove EEPROM.

---

### IP Address

| Field | Value |
|---|---|
| UUID | `A1B2000E-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | UTF-8 dotted-decimal string, e.g. `"192.168.1.42"` |

Read-only. Notified when the IP changes.

---

### Info (cached stove info)

| Field | Value |
|---|---|
| UUID | `A1B2000F-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | 28-byte packed binary |

| Bytes | Type | Field |
|---|---|---|
| 0 – 10 | UTF-8, null-padded | Firmware version (max 11 bytes) |
| 11 – 12 | `uint16` LE | Total runtime (hours) |
| 13 – 14 | `uint16` LE | Number of starts |
| 15 – 19 | `uint8[5]` | Previous error codes |
| 20 – 21 | `uint16` LE | Hours since last service |
| 22 – 23 | `int16` LE | Remote control temperature × 2 (`INT16_MIN` = not available) |
| 24 – 27 | `uint32` LE | Stove internal timestamp (Unix epoch) |

Notified whenever any of these fields changes.

---

### WiFi Info

| Field | Value |
|---|---|
| UUID | `A1B20013-1234-5678-ABCD-EF0123456789` |
| Properties | R, N |
| Format | variable-length binary |

| Bytes | Type | Field |
|---|---|---|
| 0 | `int8` | WiFi RSSI (dBm). `0` = not connected |
| 1 … null | UTF-8 | IP address, null-terminated |
| after null | UTF-8 | SSID (no null terminator) |

Example for IP `192.168.1.42`, SSID `HomeNet`, RSSI −55:
```
C9 31 39 32 2E 31 36 38 2E 31 2E 34 32 00 48 6F 6D 65 4E 65 74
^^  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^  ^^^^^^^^^^^^^^^^^^^
-55  "192.168.1.42"                     \0  "HomeNet"
```

Notified when RSSI, IP, or SSID changes.

---

### Energy Saving Start Offset

| Field | Value |
|---|---|
| UUID | `A1B20014-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8` (interpret as `int8`), range −3 – 0 |

Hour offset applied to the energy-saving start time.

---

### Energy Saving Stop Offset

| Field | Value |
|---|---|
| UUID | `A1B20015-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | `uint8`, range 0 – 3 |

Hour offset applied to the energy-saving stop time.

---

### Restart

| Field | Value |
|---|---|
| UUID | `A1B20016-1234-5678-ABCD-EF0123456789` |
| Properties | WNR (Write No Response) |
| Format | any single byte |

Write any value to trigger `esp_restart()`. The device reboots after 200 ms. No response is sent.

---

### Hostname

| Field | Value |
|---|---|
| UUID | `A1B20017-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | UTF-8 string, max 32 printable ASCII characters |

The current hostname is returned on read. Writing a new value saves it to NVS (namespace `net`, key `hostname`). The updated value is echoed back via the characteristic so the client can confirm the save. Takes effect after restart.

Invalid characters (non-printable, non-ASCII) are stripped. If the result is empty the write is rejected.

---

### Network Config

| Field | Value |
|---|---|
| UUID | `A1B20018-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |
| Format | ASCII CSV string |

**DHCP mode:**
```
dhcp
```

**Static IP mode:**
```
static,<ip>,<gateway>,<subnet>,<dns>
```

Example:
```
static,192.168.1.100,192.168.1.1,255.255.255.0,8.8.8.8
```

The firmware validates all four IP addresses using `IPAddress.fromString()`. Malformed input is silently rejected. Saved to NVS key `netcfg`. Takes effect after restart.

---

### Timers (PR1 – PR6)

| Timer | UUID |
|---|---|
| PR1 | `A1B20020-1234-5678-ABCD-EF0123456789` |
| PR2 | `A1B20021-1234-5678-ABCD-EF0123456789` |
| PR3 | `A1B20022-1234-5678-ABCD-EF0123456789` |
| PR4 | `A1B20023-1234-5678-ABCD-EF0123456789` |
| PR5 | `A1B20024-1234-5678-ABCD-EF0123456789` |
| PR6 | `A1B20025-1234-5678-ABCD-EF0123456789` |

**Properties:** R, W, N  
**Format:** 7-byte packed binary

| Byte | Field | Notes |
|---|---|---|
| 0 | Enabled | `0` = disabled, `1` = enabled |
| 1 | On time (raw) | `value × 10` = minutes since 00:00. `144` = disabled |
| 2 | Off time (raw) | Same encoding as on time |
| 3 | Setpoint (°C) | Temperature to hold when timer is active |
| 4 | Flame power | 1 – 4 |
| 5 | Fan speed | 0 – 3 |
| 6 | Day-of-week mask | Bitmask: Mon=1, Tue=2, Wed=4, Thu=8, Fri=16, Sat=32, Sun=64 |

**Time encoding example:** on time = 08:30 → `8 * 60 + 30 = 510` minutes → raw byte = `51` (510 ÷ 10).

Writes are applied to stove EEPROM asynchronously from `bleLoop()`. The in-memory cache is updated immediately so a read before the EEPROM write completes still returns the new value.

---

## OTA Service

**Service UUID:** `A1B20010-1234-5678-ABCD-EF0123456789`

Used for over-the-air firmware updates over BLE.

### OTA Control

| Field | Value |
|---|---|
| UUID | `A1B20011-1234-5678-ABCD-EF0123456789` |
| Properties | R, W, N |

Commands written by the client:

| Byte 0 | Bytes 1 – 4 | Action |
|---|---|---|
| `0x01` | `uint32` LE — firmware size | Begin OTA. Firmware size is optional (send 0 if unknown) |
| `0x02` | — | Commit OTA. Device reboots into new firmware after 1 s |
| `0x03` | — | Abort OTA |

Notifications sent by the device:

| Value | Meaning |
|---|---|
| `0x01` | Ready — OTA session started successfully |
| `0x02` | Success — firmware written, device rebooting |
| `0x03` | Aborted |
| `0x50 <pct>` | Progress — 2-byte notification, byte 1 is completion percentage (0 – 100). Sent every 4 KB written |
| `0xFF` | Error |

### OTA Data

| Field | Value |
|---|---|
| UUID | `A1B20012-1234-5678-ABCD-EF0123456789` |
| Properties | W, WNR |

Stream raw firmware binary chunks here after receiving the `0x01` ready notification from OTA Control. Use Write No Response for maximum throughput. The firmware writes each chunk to the OTA partition via `esp_ota_write()`.

---

## Write queue

All stove control writes (Power, Setpoint, Fan, Flame, Beep, Combustion, Pellet, Energy Saving) are handled via an internal queue (capacity 12 entries). The BLE callback thread only enqueues work; `bleLoop()` applies one operation per cycle with a 150 ms minimum gap between consecutive stove UART frames to prevent overlapping serial transactions.

Timer writes follow a separate path and are also applied asynchronously, with 20 ms delays between each of the 7 EEPROM fields. Writes with an out-of-range setpoint (7 – 30), flame power (1 – 4) or fan speed (0 – 3) are ignored.

---

## NVS storage

Settings that survive restart are stored in ESP32 NVS under namespace `net`:

| Key | Default | Description |
|---|---|---|
| `hostname` | `palletheater` | mDNS / DHCP hostname |
| `netcfg` | `dhcp` | IP configuration string (same format as Net Config characteristic) |
