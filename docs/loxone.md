# Loxone

[← README](../README.md)

Two ways, which can be combined. Settings in the web app (*Loxone* card, under the MQTT card) or with [`/api/loxone/config`](api.md#apiloxone). The source is [src/loxone.h](../src/loxone.h).

## 1. Virtual HTTP Input (poll)

`GET /api/loxone` returns one line with numbers only:

```
opb seq=12 state=4 power=1 burn=1 link=1 age=0 errId=0 pendId=0 pendSec=-1 memErr=0 warn=0 wRemote=0 room=20.5 setp=20 fume=74 tStove=21.0 tRemote=16.0 flame=3 flameAct=3 fan=1 fanRun=1 esOn=1 esStop=2 esStart=1 auger=38 kg=9.2 hNow=8.2 hAvg=27.2 alarmMin=1633 kgToday=2.6 kgDay=7.6 eurMonth=84 bags=11.0 lowPel=0 resMin=-1 resOff=0 brzMin=37 svcH=312 runH=5712 starts=1338 rssi=-70
```

Command recognition per Virtual HTTP Input Command: `errId=\v`, `kg=\v`, `wRemote=\v`, …

- Numbers only: true / false are `1` / `0`, unknown is `-1`. Digital values (`power`, `burn`, `link`, `fanRun`, `esOn`, `warn`, `wRemote`, `lowPel`, `resOff`) are `0` when unknown: a digital virtual input in Loxone sees anything but 0 as on. `link=0` tells that the stove does not answer.
- No key ends with another key, so `errId=` never matches inside another key.
- No timestamps: time left instead (`alarmMin`: minutes until the expected pellet alarm). Loxone counts in seconds since 2009 local time; converting a Unix time there is error-prone.
- `seq` goes up at every change of an event value and at least every heartbeat (default 60 s). A *Status* or *Monitor* block in Loxone can report when it stops changing.
- `errId`: the confirmed error (0 none, the stove's error id, `-1` no answer from the stove, `-2` pellets used up). A status block turns it into text (E1, E9, …). `pendId` / `pendSec`: an error the stove reports and counts down before it acts on it.

| Key | Meaning | Key | Meaning |
|---|---|---|---|
| `state` | stove state 0 – 10, −1 no answer | `kg` | pellets until the low pellet alarm |
| `power` | powered on (states 1 – 5, 7; an alarm is off) | `hNow` / `hAvg` | hours until the alarm at the current / 7 day average use |
| `burn` | fire (ignition, working, brazier cleaning) | `alarmMin` | minutes until the expected alarm |
| `link` | serial link to the stove up | `lowPel` | low pellet alarm of the stove |
| `age` | seconds since the last full read | `resMin` | pellet reserve: minutes until the reserve alarm |
| `memErr` | entries in the error memory | `resOff` | the stove switched itself off: pellets used up |
| `warn` / `wRemote` | any warning / the remote control's sensor is not used | `kgToday` / `kgDay` | use today / per day (7 days) |
| `room` `setp` `tStove` `tRemote` `fume` | temperatures | `eurMonth` / `bags` | cost per month / stock in bags |
| `flame` / `flameAct` | flame power setting / actual | `brzMin` | minutes to the next brazier cleaning |
| `fan` / `fanRun` | room fan setting / turning | `svcH` `runH` `starts` | service hours, working hours, starts |
| `esOn` `esStop` `esStart` | energy saving | `auger` / `rssi` | auger % / WiFi signal |

## 2. Web service push

The controller sets virtual inputs of the Miniserver itself, with `GET http://<miniserver>/jdev/sps/io/<input>/<value>` and Basic auth:

- **Map:** per key the name or UUID of a virtual input. The text keys `stateText`, `errText` (e.g. `E1 Pellet loading door open`), `warnText` and `warnShort` go to a *Virtual Text Input*: the error description as text in the app and in notifications. Language: as the remote control (NLD → Dutch, otherwise English, like the web app) or fixed Dutch / English (`lang`).
- **Switch:** the name or UUID of a switch that follows *powered on* (`On` / `Off`, once the state is stable for 5 s). Switched on or off with the remote control or the stove timer: the switch follows within a few seconds. Standby (energy saving) counts as on; final cleaning, off and the alarms (8, 9, 10) as off. The *Off* that Loxone then sends back (`POST /api/power?state=off`) writes nothing to the stove when it is off or in an alarm (answer 202): an alarm is never reset from Loxone.
- **No echo:** add the HTTP header `X-Source: loxone` to the virtual outputs that send commands (`POST /api/power`, `/api/fan`, `/api/flame`, `/api/setpoint`, `/api/energysaving`). Those changes are not pushed back. Switching on refused (409, pellets used up) or failed: the real state (Off) is pushed at once, which replaces an *OnDelay* / *AND* construction in Loxone.
- **When:** event values at once; slow values (temperatures, kg, hours) at most every 60 s and only after a real difference (0.5 °C, 0.1 kg, …); everything again every 5 min (a lost push is repaired). Every answer is checked: HTTP 200 and the code in the Miniserver's JSON answer. An input the Miniserver does not know is retried every 60 s without blocking the others. Counters and the last error: `GET /api/loxone/config` and the *Loxone* card.
- **Finding the inputs:** *Read inputs from the Miniserver* reads its structure file (`/data/LoxAPP3.json`): name, type, room and UUID of every control, kept on the controller. Then type part of a name, room or type (e.g. `InfoOnly`) in a field and pick. The UUID in that file is the one `/jdev/sps/io` takes. An input that is not in the list can be typed by name (names must be unique in the Miniserver).
- **Test:** logs in and pushes every mapped value once, with the answer per input. The switch is never pushed by the test, because in Loxone that would send a power command.
- **Security:** Basic auth over plain HTTP can be read on the network. Use a separate Loxone user with as few rights as possible. The password is stored on the controller (NVS) and never returned (`passwordSet`).

## Push reference

**The request.** For every push the controller sends one HTTP request to the Miniserver:

```
GET http://<miniserver>:<port>/jdev/sps/io/<input>/<value>
Authorization: Basic <base64 of user:password>
```

- `<input>` is the name or the UUID set for that value; `<value>` is the value as text. Both are URL-encoded: a space becomes `%20`, `/` `%2F`, `é` `%C3%A9`, … (Loxone decodes them).
- Numbers: whole numbers without decimals (`4`, `-1`), values with one decimal always with one (`20.5`, `11.0`), a dot as the decimal sign.
- The Miniserver answers with JSON; only HTTP 200 with `"Code":"200"` counts as accepted:
  ```json
  {"LL":{"control":"dev/sps/io/Kachel_State/4","value":"4","Code":"200"}}
  ```
  Anything else (wrong name: another code; 401 wrong login) is counted as refused and shown in the *Loxone* card.
- Order of a round: the switch first, then the values in the order of the table. After a refused input that input waits 60 s; after no connection or a refused login the whole round waits 15 s.
- Every 5 min all mapped values (and the switch) are sent again, also when nothing changed.

**In Loxone Config.** Per value a *Virtual Input* (*Virtueller Eingang*) under *Virtual Inputs*, for the values marked digital with *Use as digital input* ticked, and for the text values a *Virtual Text Input* (*Virtueller Texteingang*). The names below are only a suggestion: any unique name works, or the UUID from *Read inputs from the Miniserver*. Only the values you map are sent.

| Key | Input (suggested name) | Loxone object | Values | Sent | Example |
|---|---|---|---|---|---|
| *switch* | `Pelletkachel` (or its UUID) | Switch (the existing one) | `On` / `Off` | once *powered on* is stable for 5 s; not after its own command (`X-Source: loxone`); at once after a refused switch-on | `/jdev/sps/io/Pelletkachel/On` |
| `seq` | `Kachel_Seq` | Virtual Input | 0, 1, 2, … (+1 per change of an event value, at least every heartbeat) | at once | `/jdev/sps/io/Kachel_Seq/1834` |
| `state` | `Kachel_State` | Virtual Input | 0 – 10 (see [states](#stove-states-and-error-ids)), −1 no answer from the stove | at once | `/jdev/sps/io/Kachel_State/4` |
| `stateText` | `Kachel_StateText` | Virtual Text Input | state as text, e.g. `Kachel brandt` / `Working` | at once | `/jdev/sps/io/Kachel_StateText/Kachel%20brandt` |
| `power` | `Kachel_Aan` | Virtual Input (digital) | 1 on (states 1 – 5, 7), 0 off, alarm or unknown | at once | `/jdev/sps/io/Kachel_Aan/1` |
| `burn` | `Kachel_Vuur` | Virtual Input (digital) | 1 fire (ignition, working, brazier cleaning), 0 none or unknown | at once | `/jdev/sps/io/Kachel_Vuur/1` |
| `link` | `Kachel_Link` | Virtual Input (digital) | 1 the stove answers, 0 serial link down | at once | `/jdev/sps/io/Kachel_Link/1` |
| `age` | `Kachel_DataAge` | Virtual Input | seconds since the last full read of the stove, −1 never | ≥ 60 s and ≥ 5 s difference | `/jdev/sps/io/Kachel_DataAge/0` |
| `errId` | `Kachel_Error` | Virtual Input | 0 none, the error id ([table](#stove-states-and-error-ids)), −1 LINK, −2 PELLETS | at once | `/jdev/sps/io/Kachel_Error/32` |
| `errText` | `Kachel_ErrorText` | Virtual Text Input | `E1 Laadklep te lang open …`, `PELLETS Pellets op: …`, `geen` / `none` | at once | `/jdev/sps/io/Kachel_ErrorText/E1%20Laadklep%20te%20lang%20open%20of%20verkeerde%20luchtdruk%20in%20de%20kachel` |
| `pendId` | `Kachel_PendingError` | Virtual Input | error the stove reports before it counts (same ids), 0 none | at once | `/jdev/sps/io/Kachel_PendingError/32` |
| `pendSec` | `Kachel_PendingSec` | Virtual Input | seconds until the stove acts on it (≈ 55 → 0), −1 none | at most every 5 s | `/jdev/sps/io/Kachel_PendingSec/41` |
| `memErr` | `Kachel_ErrorMemory` | Virtual Input | entries in the error memory, 0 – 5 | at once | `/jdev/sps/io/Kachel_ErrorMemory/0` |
| `warn` | `Kachel_Warning` | Virtual Input (digital) | 1 any warning, 0 none | at once | `/jdev/sps/io/Kachel_Warning/1` |
| `warnText` | `Kachel_WarningText` | Virtual Text Input | the warning(s) as text, `geen` / `none` | at once | `/jdev/sps/io/Kachel_WarningText/De%20draadloze%20sensor%20staat%20aan%2C%20…` |
| `warnShort` | `Kachel_WarningShort` | Virtual Text Input | very short, for a tile: `Afstandsbed. niet gebruikt` / `Remote not used`, `Controller te warm` / `Controller too hot`, `OK` | at once | `/jdev/sps/io/Kachel_WarningShort/Afstandsbed.%20niet%20gebruikt` |
| `wRemote` | `Kachel_RemoteSensor` | Virtual Input (digital) | 1 the stove does not use the remote control's sensor | at once | `/jdev/sps/io/Kachel_RemoteSensor/1` |
| `room` | `Kachel_Room` | Virtual Input | °C, 1 decimal (the sensor the stove regulates on) | ≥ 60 s and ≥ 0.5 °C | `/jdev/sps/io/Kachel_Room/20.5` |
| `setp` | `Kachel_Setpoint` | Virtual Input | °C, whole degrees, 7 – 30 | at once | `/jdev/sps/io/Kachel_Setpoint/20` |
| `tStove` | `Kachel_TempStove` | Virtual Input | °C, 1 decimal | ≥ 60 s and ≥ 0.5 °C | `/jdev/sps/io/Kachel_TempStove/21.0` |
| `tRemote` | `Kachel_TempRemote` | Virtual Input | °C, 1 decimal | ≥ 60 s and ≥ 0.5 °C | `/jdev/sps/io/Kachel_TempRemote/16.0` |
| `fume` | `Kachel_Fume` | Virtual Input | °C, whole degrees | ≥ 60 s and ≥ 2 °C | `/jdev/sps/io/Kachel_Fume/74` |
| `flame` | `Kachel_Flame` | Virtual Input | flame power setting 1 – 4 | at once | `/jdev/sps/io/Kachel_Flame/3` |
| `flameAct` | `Kachel_FlameActual` | Virtual Input | flame power the stove runs at now, −1 unknown | at once | `/jdev/sps/io/Kachel_FlameActual/1` |
| `fan` | `Kachel_Fan` | Virtual Input | room fan setting 0 – 3 | at once | `/jdev/sps/io/Kachel_Fan/2` |
| `fanRun` | `Kachel_FanRunning` | Virtual Input (digital) | 1 turning, 0 not or unknown | at once | `/jdev/sps/io/Kachel_FanRunning/1` |
| `esOn` | `Kachel_EnergySaving` | Virtual Input (digital) | 1 energy saving on | at once | `/jdev/sps/io/Kachel_EnergySaving/1` |
| `esStop` | `Kachel_EsStop` | Virtual Input | stop at set point + 0 – 3 °C | at once | `/jdev/sps/io/Kachel_EsStop/2` |
| `esStart` | `Kachel_EsStart` | Virtual Input | restart at set point − 0 – 3 °C (0 = no restart) | at once | `/jdev/sps/io/Kachel_EsStart/1` |
| `auger` | `Kachel_Auger` | Virtual Input | auger, % of the time | ≥ 60 s and ≥ 1 % | `/jdev/sps/io/Kachel_Auger/38` |
| `brzMin` | `Kachel_BrazierMin` | Virtual Input | minutes to the next brazier cleaning, −1 when not working | ≥ 60 s and ≥ 1 min | `/jdev/sps/io/Kachel_BrazierMin/37` |
| `kg` | `Kachel_PelletsKg` | Virtual Input | kg until the low pellet alarm, 1 decimal, −1 not calibrated | ≥ 60 s and ≥ 0.1 kg | `/jdev/sps/io/Kachel_PelletsKg/9.2` |
| `hNow` | `Kachel_PelletsHoursNow` | Virtual Input | hours until the alarm at the current use, −1 while the auger stands still | ≥ 60 s and ≥ 0.5 h | `/jdev/sps/io/Kachel_PelletsHoursNow/8.2` |
| `hAvg` | `Kachel_PelletsHoursAvg` | Virtual Input | hours until the alarm at the 7 day average, −1 unknown | ≥ 60 s and ≥ 0.5 h | `/jdev/sps/io/Kachel_PelletsHoursAvg/27.2` |
| `alarmMin` | `Kachel_AlarmMin` | Virtual Input | minutes until the expected pellet alarm, −1 unknown | ≥ 60 s and ≥ 10 min | `/jdev/sps/io/Kachel_AlarmMin/1633` |
| `lowPel` | `Kachel_LowPellets` | Virtual Input (digital) | 1 low pellet alarm of the stove | at once | `/jdev/sps/io/Kachel_LowPellets/0` |
| `resMin` | `Kachel_ReserveMin` | Virtual Input | pellet reserve: minutes until the reserve alarm (20 → 0), −1 not in the reserve | at once | `/jdev/sps/io/Kachel_ReserveMin/12` |
| `resOff` | `Kachel_PelletsEmpty` | Virtual Input (digital) | 1 the stove switched itself off: pellets used up (switching on is refused) | at once | `/jdev/sps/io/Kachel_PelletsEmpty/0` |
| `kgToday` | `Kachel_KgToday` | Virtual Input | kg burnt today, 1 decimal | ≥ 60 s and ≥ 0.1 kg | `/jdev/sps/io/Kachel_KgToday/2.6` |
| `kgDay` | `Kachel_KgPerDay` | Virtual Input | kg per day (7 days), 1 decimal | ≥ 60 s and ≥ 0.1 kg | `/jdev/sps/io/Kachel_KgPerDay/7.6` |
| `eurMonth` | `Kachel_CostMonth` | Virtual Input | € per month, whole euros, −1 no price set | ≥ 60 s and ≥ € 1 | `/jdev/sps/io/Kachel_CostMonth/84` |
| `bags` | `Kachel_StockBags` | Virtual Input | stock in bags, 1 decimal, −1 not set | at once | `/jdev/sps/io/Kachel_StockBags/11.0` |
| `svcH` | `Kachel_ServiceHours` | Virtual Input | hours since the last service | at once | `/jdev/sps/io/Kachel_ServiceHours/312` |
| `runH` | `Kachel_WorkingHours` | Virtual Input | working hours | at once | `/jdev/sps/io/Kachel_WorkingHours/5712` |
| `starts` | `Kachel_Starts` | Virtual Input | number of starts | at once | `/jdev/sps/io/Kachel_Starts/1338` |
| `rssi` | `Kachel_WifiRssi` | Virtual Input | WiFi signal in dBm | ≥ 300 s and ≥ 3 dB | `/jdev/sps/io/Kachel_WifiRssi/-65` |

*Sent*: "at once" = within about a second after the controller reads the change (the stove is read every 2 s, most settings every 16 s). For the slow values both conditions apply: at least that long since the last push and at least that difference.

## Stove states and error ids

For a *Status* block on `Kachel_State` / `Kachel_Error`, or to compare with the texts.

| `state` | English | Nederlands | `power` |
|---|---|---|---|
| -1 | No answer (serial link down) | Geen antwoord (seriële verbinding weg) | -1 |
| 0 | Off | Uit | 0 |
| 1 | Starting | Start | 1 |
| 2 | Pellet loading | Pellets laden | 1 |
| 3 | Ignition | Ontsteking | 1 |
| 4 | Working | Kachel brandt | 1 |
| 5 | Brazier cleaning | Brander reinigen | 1 |
| 6 | Final cleaning | Eindreiniging | 0 |
| 7 | Standby | Stand-by | 1 |
| 8 | Alarm State | Alarm | 0 |
| 9 | Ignition failure | Ontsteking mislukt | 0 |
| 10 | Alarm | Alarm | 0 |

| `errId` | Code | `errText` (English) | `errText` (Nederlands) |
|---|---|---|---|
| -2 | PELLETS | Pellets used up: the stove switched itself off at the end of the pellet reserve (controller) | Pellets op: de kachel schakelde zichzelf uit op het einde van de pelletreserve |
| -1 | LINK | No answer from the stove (serial link down) (controller) | Geen antwoord van de kachel (seriële verbinding weg) |
| 0 | – | none | geen |
| 1 | E8 | Fume sensor error | Fout rookgassensor |
| 2 | E4 | Fume temperature too high | Rookgastemperatuur te hoog |
| 4 | E9 | Ignition temperature not reached within limit | Ontstekingstemperatuur niet bereikt binnen de tijd |
| 8 | E7 | Fume temperature too low | Rookgastemperatuur te laag |
| 16 | E3 | Temperature inside stove too high | Temperatuur in de kachel te hoog |
| 32 | E1 | Pellet loading door is open for too long or air pressure inside stove is wrong | Laadklep te lang open of verkeerde luchtdruk in de kachel |
| 64 | E2 | Pressure sensor error during ignition | Fout druksensor tijdens ontsteking |
| 128 | E6 | Auger error - Too many pellets dispatched | Fout schroef - te veel pellets gedoseerd |
| 129 | E12 | Fume fan error | Fout rookgasventilator |
| 130 | E11 | Unknown error - Not described in manual | Onbekende fout - niet beschreven in de handleiding |
| 132 | E19 | External contact N.PEL / Pellet is active | Extern contact N.PEL / pellets actief |
| 136 | E13 | Unknown error - Not described in manual | Onbekende fout - niet beschreven in de handleiding |
| 255 | PowerError | Display shows power error | Display toont stroomfout |

`pendId` uses the same ids as `errId` (without −1 and −2). The descriptions in Dutch are the ones of the web app; texts in the language set with `lang` (default: as the remote control).

Try a command manually in the browser first, e.g. `http://<miniserver>/jdev/sps/io/Kachel_State/4`. Not verified on a real Miniserver yet: whether newer Miniservers still accept Basic auth on `/jdev/…`, and whether virtual inputs that are not shown in the user interface appear in the structure file.
