# Discovered Values

My stove is a Piazzetta 220 M

RAM (0x00)

| ADDR (HEX) | ADDR (DEC) | Formula   | Unit   | Description                                 | Example or default value | Range      |
|------------|------------|-----------|--------|---------------------------------------------|--------------------------|------------|
| 0x01       | 1          | x/2       | °C     | Ambient temperature. Follows the remote control sensor while the remote is in contact, falls back to the stove sensor when it is not (manual §18: fallback after 4 min without contact) | 40 (== 20) | |
| 0x04       | 4          | x         | Bool   | Hot enough for the room fan (0 = no, 2 = yes) |                        |            |
| 0x0A       | 10         | x         |        | Room fan drive value: 0 off, 21 / 23 / 25 at speed 1 / 2 / 3 (verified 16/11/2026, stove working) | 23 | |
| 0x0B       | 11         |           |        | Unknown, 0 if off ~ 100 if on               |                          |            |
| 0x0D       | 13         | x/10      | rpm    | Auger speed                                 |                          |            |
| 0x11       | 17         | x         |        | Countdown counter 20 -> 0 / sec             |                          |            |
| 0x13       | 19         |           |        | Countdown Counter ? 120 -> 0                |                          |            |
| 0x14       | 20         |           |        | Unknown Boolean value                       |                          |            | 
| 0x15       | 21         | x         | id     | Last selected menu parameter (same ids as 0x18); set by the stove when a menu is opened | 0 | |
| 0x16       | 22         | x         | count  | Key repeat counter: 1 on a key press, +1 for every auto-repeat step while the key is held (reached 37 after ~9 s); each step also steps the edit value 0x19. Back to 0 about 1 s after release (verified 17/11/2026 with /api/keywatch) | 0 | |
| 0x18       | 24         | x         | id     | Parameter being edited on the RC (0 = not editing) - See lookup table. Writable, but clearing it does not confirm | 0 | |
| 0x19       | 25         | x         |        | Menu edit value: the value the RC shows for the parameter in 0x18 (e.g. INSTELL WIRELESS 0 = off, 1 = on). Writable, but has no effect until confirmed on the RC | | |
| 0x1A       | 26         | x         | id     | Menu on the RC - See lookup table. Writable: the RC follows | 0 | |
| 0x21       | 33         | x         | state  | Stove State                                 | 0                        |            |
| 0x22       | 34         |           |        | Unknown Boolean value                       |                          |            |
| 0x23       | 35         |           |        | Countdown Counter 8 -> 0                    |                          |            |
| 0x24       | 36         |           |        | Set to 3 – 7 on a key press and counts down to 0 within ~1 s after the last key event; 0x08 / 0x16 are cleared when it reaches 0. Looks like the key-release timeout of the radio link | 0 | 7 - 0 |
| 0x08       | 8          | bits      | key    | Key currently pressed on the remote: 1 = ▼, 2 = ▲, 4 = ESC, 8 = OK (verified 17/11/2026 with /api/keywatch). ⏻ not tested (224 seen once on 16/11, meaning unknown). Stays set ~0.7 – 1.2 s, until 0x24 reaches 0 | 0 | |
| 0x2E       | 46         |           | Sec    | Countdown since last contact Remote Control |                          |   0 - 240  |
| 0x30       | 48         | x         | Sec    | Time till error code? becomse active        | 7                        |            |
| 0x31       | 49         | x         | sec    | Seconds countdown                           |                          |   0 -  59  |
| 0x32       | 50         | x         | Min    | Countdown when to start brazer cleaning     | 45                       |  45 -   0  | 
| 0x33       | 51         | bitmask   | error  | Errorcode in a bitmask? 32 = E1 ?           | 32                       |            |
| 0x34       | 52         | x         | amount | Flame power the stove actually runs at (modulates to 1 at the set point; setting is EEPROM 0x7F). Seen 1 with setting 2 in standby after reaching the set point | | 1 - 4 |
| 0x37       | 55         | (x+25)*10 | rpm    | smoke fan RPM                               | 130 (== 1550 )           |            |
| 0x3E       | 62         | x         | °C     | Fume temperature                       | 105                      |            |
| 0x44       | 68         | x/2       | °C     | Stove room temp ??                          |                          |            |
| 0x48       | 72         | (x+25)*10 | rpm    | smoke fan RPM                               | 130 (== 1550 )           |            |
| 0x4B       | 75         | x         |        | Counter                                     |                          |   0 - 254  |
| 0x4C       | 76         |           |        | Flips between 0 and 255                     |                          |            |
| 0x4D       | 77         | x         |        | Flip flop 0 / 1                             |                          |   0 -   1  |
| 0x4F       | 79         |           |        | Random ???                                  |                          |            |
| 0x50       | 80         | x         | Min    | Time remaining pellets when low             | 0                        |            |
| 0x55       | 85         |           |        | Looks random                                |                          |            |
| 0x56       | 86         |           |        | Looks random                                |                          |            |
| 0x57       | 87         |           |        | Random? Jumps every second                  |                          |            |
| 0x58       | 88         |           |        | Looks random                                |                          |            |
| 0x59       | 89         |           |        | Looks random                                |                          |            |
| 0x5A       | 90         |           |        | Looks random                                |                          |            |
| 0x5B       | 91         |           |        | Looks random                                |                          |            |
| 0x68       | 104        |           |        | Looks random                                |                          |            |
| 0x6B       | 107        |           | Sec    | Countdown since last contact Remote Control |                          |   0 - 240  |
| 0x6D       | 109        | x         | Sec    | Time till error code becomse active         | 20                       |   0 -  20  |
| 0x79       | 121        |           |        | Unknown Boolean value                       |                          |            |
| 0x7A       | 122        | x         | Sec    | Current time (sec)                          | 1                        |   0 -  59  |
| 0x7B       | 123        | x         | id     | Day of week 1=mon / 7=sun                   | 1                        |   1 -   7  |
| 0x7C       | 124        | x         | Hour   | Current time (hour)                         | 8                        |   0 -  23  |
| 0x7D       | 125        | x         | Min    | Current time (min)                          | 12                       |   0 -  59  |
| 0x7E       | 126        | x         | day    | Current Day                                 | 28                       |   1 -  31  |
| 0x7F       | 127        | x         | month  | Current Month                               | 1                        |   1 -  12  |
| 0x80       | 128        | x         | year   | Current year                                | 22                       |   0 -  99  |
| 0x81       | 129        | x         |        | Low byte of the stove clock as seconds since midnight / 2 (16-bit LE with 0x82; +1 every 2 s). Verified 17/11/2026: 0x82/0x81 = 4/247 -> 1271 × 2 s = 00:42:22, stove clock 00:42:22. Also matches the 16/11 16:32 dump (116/46 -> 16:31:24) | | 0 - 255 |
| 0x82       | 130        | x         |        | High byte of the seconds-since-midnight / 2 counter (see 0x81); 0 – 168, back to 0 at midnight | | 0 - 168 |
| 0x89       | 137        | x         | Min    | Countdown till low pellet alarm starts (hypothesis). 16/11/2026, while working: dropped 20 -> 19 and back after ~35 s, first at 19:02, then about every 9.5 – 13.5 min until shutdown; never while off. Possibly the hopper level sensor briefly seeing no pellets (unconfirmed; 0xA4 did not change in the log) | 20 | 20 - 0 |
| 0x8F       | 143        | x/2       | °C     | Remote control temp                         |                          |            |
| 0x96       | 150        |           |        | Unknown 84 - 89                              |                          |            |
| 0xAB       | 171        |           |        | Looks random                                |                          |            |
| 0xAF       | 175        |           |        | Unknown. 47 – 48 off (stove cold, 17/11/2026), 68 working. Possibly a board / electronics temperature in x/2 (23.5 °C vs 34 °C) - unconfirmed | | |
| 0xB6       | 182        | x         | bits   | Active "INSTELL WIRELESS" (external wireless temperature sensor, manual §23.9). Bit 0 = 1: off, bit 0 = 0: on. Bit 1 unknown (seen 1, 3 = off, 2 = on). Read-only: restored by the stove right after a write | 1 | |
| 0xC2       | 194        |           |        | Looks random (0 - 2 - 6 - 7 )               |                          |            |
| 0xC4       | 196        |           |        | Looks random                                |                          |            |
| 0xC6       | 198        | x         |        | Counter 0-255 fast                          |                          |   0 - 255  |
| 0xD0       | 208        | x/2       | °C     | Stove room temp ??                          |                          |            |
| 0xD2       | 210        |           |        | Unknown 58 / 57                             |                          |            |
| 0xA7       | 167        | x         |        | Second room fan drive value: 0 off, 86 / 82 / 78 at speed 1 / 2 / 3 (meaning unknown, possibly a triac delay) | 82 | |
| 0XD3       | 211        | x         | Bool   | Room fan is running (0 = stopped, 2 = running) |                       |            |
| 0xDF       | 223        | x/10      | rpm    | Feed rate ???                               |                          |            |


Low pellets changes ( To check when pellets are low again)

0x97 (151) =   0 =>  3  ?? |  0 == No Pellets /  3 == All normal => Verified, looks correct
0xC7 (199) =   2 =>  0  ?? |  2 == No Pellets /  0 == All normal?
0xBB (187) =   0 => 30     |  0 == No Pellets / 30 == All Normal?
0xA4 (164) =   5 =>  0     |  5 == No Pellets /  0 == All normal  ======= !!!!
0x9E (158) =  76 => 77     | 76 == No pellets / 77 == All normal 


EEPROM (0x20)

| ADDR (HEX) | ADDR (DEC) | Formula | Unit   | Description                            | Example or default value | Range      |
|------------|------------|---------|--------|----------------------------------------|--------------------------|------------|
| 0x02       |            | x       | Min    | Interval between braizer cleanings     | 45                       |            |
| 0x04       | 4          | x/10    |        | Auger value at ignition (by analogy; earlier listed as kg/h) | 27 == 2.7 |            |
| 0x05       | 5          | x/10    |        | Auger value at power 1 (RAM 0x0D shows 3.0 at power 1, verified 16/11/2026; earlier listed as kg/h) | 30 == 3.0 |            |
| 0x06       | 6          | x/10    |        | Auger value at power 2 (RAM 0x0D shows 4.6 at power 2, verified) | 46 == 4.6 |            |   
| 0x07       | 7          | x/10    |        | Auger value at power 3 (RAM 0x0D shows 6.2 at power 3, verified 17/11/2026) | 62 == 6.2 |            | 
| 0x08       | 8          | x/10    |        | Auger value at power 4 (by analogy)    | 78 == 7.8                |            | 
| 0x09       | 9          | x/10    |        | Auger value at power 5 (unused, the stove has 4 levels) | 78 == 7.8 |            |
| 0x3C       | 60         | x       | bool   | Timer PR1 enabled                      | 0                        | 0 - 1      |
| 0x3D       | 61         | val×10min | HH:MM | Timer PR1 on time (144 = off)       | 144                      | 0 - 144    |
| 0x3E       | 62         | val×10min | HH:MM | Timer PR1 off time (144 = off)      | 144                      | 0 - 144    |
| 0x3F       | 63         | x       | °C     | Timer PR1 setpoint                     | 21                       | 7 - 30     |
| 0x40       | 64         | x       | level  | Timer PR1 flame power                  | 2                        | 1 - 5      |
| 0x41       | 65         | x       | speed  | Timer PR1 room fan speed               | 0                        | 0 - 3      |
| 0x43       | 67         | bitmask | days   | Timer PR1 days of week                 |                          | see below  |
| 0x48       | 72         | x       | bool   | Ext Thermostat Modulate = 0 / Stop = 1 | 0                        | 0 - 1      |
| 0x49       | 73         | x       | bool   | Ext Thermostat N.O. = 0 / N.C. = 1     | 0                        | 0 - 1      |
| 0x4B       | 75         | x       | bool   | Beep is enabled                        | 1                        | 0 - 1      |
| 0x4F       | 79         | x       | id     | Language remote, see lookup table      | 1                        | 1 - 8      |
| 0x50       | 80         | x       | bool   | Timer PR2 enabled                      | 0                        | 0 - 1      |
| 0x51       | 81         | val×10min | HH:MM | Timer PR2 on time (144 = off)       | 144                      | 0 - 144    |
| 0x52       | 82         | val×10min | HH:MM | Timer PR2 off time (144 = off)      | 144                      | 0 - 144    |
| 0x53       | 83         | x       | °C     | Timer PR2 setpoint                     |                          | 7 - 30     |
| 0x54       | 84         | x       | level  | Timer PR2 flame power                  |                          | 1 - 5      |
| 0x55       | 85         | x       | speed  | Timer PR2 room fan speed               |                          | 0 - 3      |
| 0x57       | 87         | bitmask | days   | Timer PR2 days of week                 |                          | see below  |
| 0x58       | 88         | x       | bool   | Timer PR3 enabled                      | 0                        | 0 - 1      |
| 0x59       | 89         | val×10min | HH:MM | Timer PR3 on time (144 = off)       | 144                      | 0 - 144    |
| 0x5A       | 90         | val×10min | HH:MM | Timer PR3 off time (144 = off)      | 144                      | 0 - 144    |
| 0x5B       | 91         | x       | °C     | Timer PR3 setpoint                     |                          | 7 - 30     |
| 0x5C       | 92         | x       | level  | Timer PR3 flame power                  |                          | 1 - 5      |
| 0x5D       | 93         | x       | speed  | Timer PR3 room fan speed               |                          | 0 - 3      |
| 0x5F       | 95         | bitmask | days   | Timer PR3 days of week                 |                          | see below  |
| 0x60       | 96         | x       | bool   | Timer PR4 enabled                      | 0                        | 0 - 1      |
| 0x61       | 97         | val×10min | HH:MM | Timer PR4 on time (144 = off)       | 144                      | 0 - 144    |
| 0x62       | 98         | val×10min | HH:MM | Timer PR4 off time (144 = off)      | 144                      | 0 - 144    |
| 0x63       | 99         | x       | °C     | Timer PR4 setpoint                     |                          | 7 - 30     |
| 0x64       | 100        | x       | level  | Timer PR4 flame power                  |                          | 1 - 5      |
| 0x65       | 101        | x       | speed  | Timer PR4 room fan speed               |                          | 0 - 3      |
| 0x67       | 103        | bitmask | days   | Timer PR4 days of week                 |                          | see below  |
| 0x68       | 104        | x       | bool   | Timer PR5 enabled                      | 0                        | 0 - 1      |
| 0x69       | 105        | val×10min | HH:MM | Timer PR5 on time (144 = off)       | 144                      | 0 - 144    |
| 0x6A       | 106        | val×10min | HH:MM | Timer PR5 off time (144 = off)      | 144                      | 0 - 144    |
| 0x6B       | 107        | x       | °C     | Timer PR5 setpoint                     |                          | 7 - 30     |
| 0x6C       | 108        | x       | level  | Timer PR5 flame power                  |                          | 1 - 5      |
| 0x6D       | 109        | x       | speed  | Timer PR5 room fan speed               |                          | 0 - 3      |
| 0x6F       | 111        | bitmask | days   | Timer PR5 days of week                 |                          | see below  |
| 0x70       | 112        | x       | bool   | Timer PR6 enabled                      | 0                        | 0 - 1      |
| 0x71       | 113        | val×10min | HH:MM | Timer PR6 on time (144 = off)       | 144                      | 0 - 144    |
| 0x72       | 114        | val×10min | HH:MM | Timer PR6 off time (144 = off)      | 144                      | 0 - 144    |
| 0x73       | 115        | x       | °C     | Timer PR6 setpoint                     |                          | 7 - 30     |
| 0x74       | 116        | x       | level  | Timer PR6 flame power                  |                          | 1 - 5      |
| 0x75       | 117        | x       | speed  | Timer PR6 room fan speed               |                          | 0 - 3      |
| 0x77       | 119        | bitmask | days   | Timer PR6 days of week                 |                          | see below  |
| 0x7D       | 125        | x       | °C     | Setpoint Room Temperature              | 23                       |            |
| 0x7F       | 127        | x       | amount | Flame power level                      |                          | 1 - 4      |
| 0x81       | 129        | x       | speed  | Room fan speed                         | 0                        | 0 - 3      |
| 0x90       | 144        | x       | °C     | Energy saving Stop offset              |                          | 0 - 3      |
| 0x91       | 145        | x * -1  | °C     | Energy saving START negative offset    |                          | 0 - 3      |
| 0xC0       | 192        | x       | id     | Combustion Quality, see lookup         | 76                       |            |
| 0xC6       | 198        | x       | bool   | Low pellets monitoring                 | 1                        | 0 - 1      |
| 0xC4       | 196        | x       | bool   | Pellet contact N.O. = 0 / N.C. = 1     | 0                        | 0 - 1      |
| 0xE0       | 224        | x       | error  | Memory Error - Position 1 - See table  |                          |            |
| 0xE1       | 225        | x       | error  | Memory Error - Position 2 - See table  |                          |            |
| 0xE2       | 226        | x       | error  | Memory Error - Position 3 - See table  |                          |            |
| 0xE3       | 227        | x       | error  | Memory Error - Position 4 - See table  |                          |            |
| 0xE4       | 228        | x       | error  | Memory Error - Position 5 - See table  |                          |            |
| 0x8A       | 138        | ASCII   |        | Firmware variant char 0                | A                        |            |
| 0x8B       | 139        | ASCII   |        | Firmware variant char 1                | R                        |            |
| 0x8C       | 140        | x       |        | Firmware product number                | 1                        |            |
| 0x8D       | 141        | x       |        | Firmware major version                 | 31                       |            |
| 0x8E       | 142        | x       |        | Firmware minor version                 | 1                        |            |
| 0x8F       | 143        | x       |        | Firmware patch version                 | 0                        |            |
| 0xBC       | 188        | x       | id     | Pellet quality, see combustion quality lookup |                   |            |
| 0xE7       | 231        | x       | hours  | Hours since last service               |                          |            |
| 0xEA       | 234        | x       | hours  | Total runtime low byte (16-bit LE)     |                          |            |
| 0xEB       | 235        | x       | hours  | Total runtime high byte (16-bit LE)    |                          |            |
| 0xEC       | 236        | x       | hours  | Working hours ???                      |                          |            |
| 0xEE       | 238        | x       | amount | Amount of starts low byte (16-bit LE)  | 1                        |            |
| 0xEF       | 239        | x       | amount | Amount of starts high byte (16-bit LE) |                          |            |
| 0xFA       | 250        |         |        | Unknown                                |                          |            |

# Clock (EEPROM 0xF8 - 0xFD)

Written as BCD (e.g. 16 -> 0x16): 0xF8 day of week (1 = Monday ... 7 = Sunday), 0xF9 hour, 0xFA minute, 0xFB day, 0xFC month, 0xFD year (2 digits). Older firmware wrote the week number (`%W`) into 0xF8, which put the chrono timers on the wrong days.

# Timer encoding

Each timer (PR1–PR6) occupies an 8-byte block in EEPROM. PR1 starts at 0x3C; PR2–PR6 follow at 0x50, 0x58, 0x60, 0x68, 0x70.

**Time value encoding** (on/off time fields):

| Value | Time  |
|-------|-------|
| 0     | 00:00 |
| 1     | 00:10 |
| 6     | 01:00 |
| 142   | 23:40 |
| 143   | 23:50 |
| 144   | off   |

Formula: `minutes = value × 10`, times are in 10-minute steps only.

**Day-of-week bitmask** (offset +7):

| Bit | Value | Day       |
|-----|-------|-----------|
| 0   | 1     | Monday    |
| 1   | 2     | Tuesday   |
| 2   | 4     | Wednesday |
| 3   | 8     | Thursday  |
| 4   | 16    | Friday    |
| 5   | 32    | Saturday  |
| 6   | 64    | Sunday    |

Example: Monday + Wednesday + Friday = 1 + 4 + 16 = 21


# menu Item on remote  (RAM 0x1A)

Not fully completed yet

| int  | Selected Item         |
|------|-----------------------|
| 0    | Main screen           |
| 1    | Multifuoco            |
| 2    | Chrono                |
| 3    | Language              |
| 5    | Clock                 |
| 7    | Multicomfort          |
| 8    | Multi Comfort Plus    |
| 9    | Energy Saving        |
| 11   | Beep                  |
| 12   | Display               |
| 13   | Installer             |
| 39   | -- FILL AUGER         |
| 40   | -- Pellet reserve     |
| 41   | -- Pellet contact     |
| 42   | -- External Thermo    |
| 43   | -- Memory             |
| 51   | ---- Total Hours      |
| 52   | ---- Interval hours   |
| 53   | ---- Amount of starts |
| 54   | ---- Error Memory     |
| 44   | -- Pellet quality     |
| 46   | -- Instell wireless   |
| 47   | -- Link wireless      |
| 49   | -- Wifi               |
| 64   | ---- WPS              |
| 65   | ---- Wifi Reset       |
| 14   | State Stove           |
| 50   | -- Parameters         |
| 15   | exit                  |


# Selected in menu (RAM 0x18)

| ID  | Selected Item         |
|-----|-----------------------|
|   0 | nothing               |
| 217 | Instell wireless      |
|  71 | Multi Comfort         |
|  76 | Beep                  |
|  79 | Display settings      |
|  80 | Language              |
| 123 | State Stove           |
| 130 | MultiFuoco            |
| 145 | Energy Saving - Stop  |
| 146 | Energy Saving - Start |
| 200 | Multi Comfort Plus    |
| 248 | Day of week           |
| 249 | Hour                  |
| 250 | min                   | 
| 251 | Day                   |
| 252 | Month                 |
| 253 | year                  |

# Error codes

| Val   | Binary   | Error Code  | Description                                                                   |
|-------|----------|-------------|-------------------------------------------------------------------------------|
|   0   | 00000000 | -           | No Error                                                                      |
|   1   | 00000001 | E8          | Fume sensor error                                                             |
|   2   | 00000010 | E4          | Fume temperature to high                                                      |
|   4   | 00000100 | E9          | Ignition temperature not reached within limit                                 |
|   8   | 00001000 | E7          | Fume temperatuer to low                                                       |
|  16   | 00010000 | E3          | Temperature inside stove to high                                              |
|  32   | 00100000 | E1          | Pellet loading door is open for too long or air pressure inside stove is wrong |
|  64   | 01000000 | E2          | Pressure sensor error during ignition                                         |
| 128   | 10000000 | E6          | Auger error - Too many pellets dispatched                                  |
| 129   | 10000001 | E12         | Fume fan error                                                                |
| 130   | 10000010 | E11         | Unknown error - Not described in manual                                       |
| 132   | 10000100 | E19         | External contact N.PEL / Pellet is active                                     |
| 136   | 10001000 | E13         | Unknown error - Not described in manual                                       |
| 255   | 11111111 | Power Error | Display shows power error                          

Couldn't reverse the following error codes :
- E10
- E14


# Language

| Val | Language |
|-----|----------|
| 1   | ENG      |
| 2   | FRA      |
| 3   | DEU      |
| 4   | NLD      |
| 5   | POR      |
| 6   | SPA      |
| 7   | HRV      |
| 8   | DAN      |


# Combustion Quality

| Val | Setting |
|-----|---------|
| 76  | +G      |
|  2  | -3      |
|  3  | -2      |
|  4  | -1      |
|  5  |  0      |
|  6  | +1      |
|  7  | +2      |
|  8  | +3      |


# Pellet Quality

| Val | Setting |
|-----|---------|
|  0  | -5      |
|  1  | -4      |
|  2  | -3      |
|  3  | -2      |
|  4  | -1      |
|  5  |  0      |
|  6  |  1      |
|  7  |  2      |
|  8  |  3      |
|  9  |  4      |
|  10 |  5      |
# INSTELL WIRELESS (wireless temperature sensor)

Installer menu *INSTELL WIRELESS* (menu 46 in RAM 0x1A, parameter 217 in RAM 0x18) enables or disables the optional external wireless temperature sensor (manual §23.9). It was first assumed to select remote vs. stove sensor; according to the manual that choice is made in the **MULTICOMFORT** menu (AFSTAND. / KACHEL / WIRELESS, §18), whose register is not known yet. Without contact with the remote / wireless sensor for more than 4 minutes the stove uses its own sensor, which matches RAM 0x01 following the stove sensor when the remote batteries were removed.

Observed sequence when changing it on the remote control:

1. Opening the menu: 0x1A = 46, the stove sets 0x15 = 217.
2. Starting to edit: 0x18 = 217, 0x19 = current value.
3. Changing the value (not confirmed): 0x19 changes, 0x16 = 1 briefly, 0xB6 unchanged.
4. Confirming: 0x18 = 0, 0xB6 changes (bit 0).

Nothing changes in EEPROM (bank 0x20, 0x00 - 0xFF) when the setting is confirmed. Banks 0x40 and 0x60 do not answer.

Write attempts over the serial bus, none of which changed the active setting:

| Attempt | Result |
|---|---|
| RAM 0x19 = 1 | overwritten by the stove |
| RAM 0xB6 = 2 / = 1 (with and without the remote present) | restored by the stove immediately |
| 0x18 = 217, 0x19 = new value, 0x18 = 0 | writes stick, no confirm |
| same, with menu opened first (0x1A = 46) | no confirm |
| same, plus 0x16 = 1 | no confirm |

The confirm trigger is most likely a key event from the remote control. RAM 0x08 turned out to hold the pressed key (see the key watch section below); whether writing it makes the stove act on a key is not tested.

# Hypotheses from the 16/11/2026 analysis (stove working, flame 2, fan 1)

Not verified with a controlled change unless stated. Sources: a full RAM + EEPROM dump, 10 samples of the unknown RAM addresses over 3 minutes, and public Micronova maps ([eni23/micronova-controller](https://github.com/eni23/micronova-controller), [RidiculousLab](https://www.ridiculouslab.com/arguments/iot/stufa/micronova_en.php), [k3a.me](https://k3a.me/ir-controller-for-pellet-stove-with-micronova-controller-stufe-e-pellet-aria-ir-telecomando/)). Maps differ per board; only 0x01, 0x0D, 0x21, 0x34, 0x37 and 0x3E match ours.

| Address | Observation | Hypothesis |
|---|---|---|
| RAM 0x0A | 0 / 21 / 23 / 25 at fan off / 1 / 2 / 3 with flame 2, 9 / 14 / 19 with flame 1 (verified) | Room fan power; eni23 lists it as "hot air outgoing fan power" |
| RAM 0x02, 0x27 (copy) | Always exactly fumes temperature (0x3E) − 30, in 36 samples over three sessions (working and standby) | Confirmed: derived from the fume temperature (offset 30); not a separate sensor |
| RAM 0x07 | Counts down 4 → 1, one step per minute, then restarts while working; stays 1 in standby | Minute countdown of a 4-minute cycle that only runs while burning; what the cycle does is unknown |
| RAM 0x81 | +1 about every 2 s, wraps at 255 | Free-running counter |
| RAM 0x0B, 0x4E (copy) | 104–105 while working (doc: 0 when off) | Unknown, follows on/off |
| RAM 0x36 | 154 while 0x37 was 155 (1800 rpm) and later 154 (1790 rpm); in standby 234 while 0x37 was 234–235 (2590–2600 rpm) | Fume fan target speed, 0x37 the measured speed (same encoding) |
| RAM 0x61–0x64 | 80 / 95 / 110 / 120 = EEPROM 0x86–0x89 | RAM copy of a per-power-level parameter (4 levels) |
| RAM 0x6D, 0x79, 0x89 | 20 = set point | **Refuted 17/11/2026 (stove off):** set point changed 20 → 21 on the remote (EEPROM 0x7D = 21), all three stayed 20, and no other stable RAM register changed (full dump). The set point is only kept in EEPROM 0x7D, at least while the stove is off; the 20 is a coincidence. Not checked while working |
| RAM 0xE0–0xFF | All 0x20 (spaces) except E8 / EA / FD | Looks like a text buffer, empty on the main screen (test: dump while a menu or message is shown) |
| EEPROM 0x92–0xB9 | Rows of 5 values, the 5th equal to the 4th (stove has 4 power levels), 4 rows repeated twice | Installer table per power level (auger time / fume fan); which row is which is unknown |
| RAM 0x96, 0x9E, 0xAF | Change by ±1–2 between reads; 0xAF rose 68 → 75 in an hour | Raw sensor (ADC) values; meaning unknown |
| RAM 0x33 (start-up) | 17/11/2026 12:31: shows 32 (E1) for ~45 s during the start-up check (state 1), then clears; not stored in the error memory | Pre-warning of the start-up check, not an error. The firmware now waits 3 min before counting an error in the transitional states (1, 2, 3, 5, 6), 30 s otherwise, and at once in an alarm state |
| RAM 0x33 | Shows 129 (E12, fume fan) for 1–5 s when the stove enters and leaves brazier cleaning, while the stove keeps running and nothing is stored in the error memory | Short pre-warning, not an error. The firmware only counts an error after 30 s or when the stove is in an alarm state (8, 9, 10) |
| RAM 0x32 | 37 at 16:06, 0 at 16:43 (brazier cleaning started), 35 at 16:54 | Confirmed: minutes until the next brazier cleaning (cycle ≈ 45 min at flame 2) |

# PELLET RESERVE (installer menu 40, parameter 199)

Verified 16/11/2026 on the remote (menu shows ON): RAM 0x1A = 40, 0x18 = 199, 0x19 = 1. The setting is stored in **EEPROM 0xC6** (1 = ON).

Parameter ids in RAM 0x18 map to EEPROM address = id − 1 for all settings checked so far (CHRONO verified 16/11/2026: PR1 on/off = parameter 61 = EEPROM 0x3C, so program n field f = parameter timerBase[n] + f + 1, i.e. PR1 61–68, PR2 81–88, PR3 89–96, PR4 97–104, PR5 105–112, PR6 113–120): 76 beep → 0x4B, 80 language → 0x4F, 128 flame → 0x7F, 130 MultiFuoco → 0x81, 145 / 146 energy saving → 0x90 / 0x91, 193 combustion quality → 0xC0, 199 pellet reserve → 0xC6. Exception: 198 pellet quality is read from 0xBC (0xC5 holds 137, meaning unknown).

`lowPalletAlarm` is computed from RAM 0xA4 (5 = reserve sensor active) and RAM 0x89 (≤ 1 = alarm). With the hopper not empty: 0xA4 = 0, 0x89 = 20. Whether 0x89 is a remaining-time counter that starts when the sensor triggers is not verified. It is not a copy of the set point: it stayed 20 when the set point was changed to 21 (17/11/2026).
| Room fan in standby (superseded: power 9 is the reduced power, see "Modulation when the set point is reached") | At 17:27, 13 min after standby started, with fumes at 108 °C: 0x0A 21 → 9, 0xA7 86 → 104, 0xD3 2 → 1, 0x04 2 → 1, 0x66 21 → 5 | The stove keeps the room fan running at a low cooling power (9) after the flame is out. 0xD3 = 1 / 0x04 = 1 probably mean "cooling run" (2 = normal run). The app shows this as "running at low power" |
| RAM 0x09, 0x43 | 10 while working, 0 in standby | Follow the burning phase |
| RAM 0x39 | 16 while working, 80 in standby | Unknown, changes with the phase |

Update 16/11/2026 ~17:50 (the "cooling" name was later replaced by "reduced", see below): in standby with the fan stopped (0x0A = 0, 0xA7 = 0, 0x04 = 0) RAM 0xD3 still reads 1, so 0xD3 = 1 does not mean "cooling run". The firmware derives `roomFan.mode` from the power 0x0A instead (0 = off, 21/23/25 = normal, other = low-power run after the flame is out) and the web app shows a cooling run as "Afkoelen" in the status and in the chart (drawn at 0.5 in the fan lane when the fan has power but no mapped speed).

# DISPLAY – backlight (menu 12, parameter 79)

Verified 16/11/2026: the screen "SET ACHTERGRONDVERLICHTING ON 10 s" is menu 12, parameter 79 (EEPROM 0x4E = 1). RAM 0x19 stays 1 while the value on the remote is changed (10 s → 9 s), so the backlight time is kept in the remote itself and is not visible over the stove bus. The screen "SET POWER ON 31 s" in the same menu also shows parameter 79 with 0x19 = 1: all DISPLAY screens look the same on the bus, so it cannot be told which one is open.

# STAAT KACHEL (menu 14, parameter 123)

Verified 16/11/2026: menu 14 without a parameter is the menu screen (stove icon + "STAAT KACHEL"); after OK the parameter is 123 and RAM 0x19 is the page:

| 0x19 | Page (texts on the remote) |
|---|---|
| 0 | TERM EXT=OFF, DATA BANK=08, FW-AR-01-V-31-01-00 |
| 1 | ROOKG=084°, RPM/ASP=1800, SCHROEF=04.6, KACHEL=24°, AFSTAND.=20°< (`<` marks the sensor used for the room temperature; temperatures truncated) |
| 2 | SENSOR 1=DISAB, ID SENSOR 1=DISAB |

The registers behind TERM EXT, DATA BANK and SENSOR 1 / ID SENSOR 1 are not known yet.

# Modulation when the set point is reached (verified 16/11/2026 18:53)

Room 20.0 → 20.5 °C with set point 20, flame set to 2, fan set to 1. The stove turned down at once:

| | Before | After |
|---|---|---|
| Actual flame power (RAM 0x34) | 2 | 1 |
| Auger (RAM 0x0D) | 4.6 (= EEPROM 0x06) | 3.0 (= EEPROM 0x05) |
| Fume fan target / actual (RAM 0x36 / 0x37) | 154 / 154 (1800 rpm) | 134 / 134 (1590 rpm) |
| Room fan power (RAM 0x0A) | 21 | 9 |
| RAM 0xA7 | 86 | 104 |
| RAM 0xD3 / 0x04 | 2 / 2 | 1 / 1 |

Follow-up the same evening (stove still at flame 1): changing the fan speed gave 0x0A = 9 / 14 / 19 for speed 1 / 2 / 3 (0xA7 = 104 / 97 / 89). Together with 21 / 23 / 25 at flame 2 this shows that the room fan power depends on the selected speed **and** the actual flame power; there is no separate reduced mode. 0xA7 ≈ 117 − 1.5 × 0x0A, consistent with a triac delay derived from the power. The firmware now reports `roomFan.actual` = selected speed while the power is > 0 and `roomFan.mode` = off / on. The main screen of the remote shows the room temperature inverted (white on black) in this situation.

# Off vs. working (17/11/2026)

Stove off for 2.5 h (state 0, room 17 °C, stove sensor 20 °C, fume 22 °C), compared with the working dump of 16/11 16:32 (flame 2, fume 93 °C). Registers that differ and stay stable while off; meaning not known yet:

| RAM  | Working | Off | Note |
|------|---------|-----|------|
| 0x0B | 104 | 0 | Already known as "0 off, ~100 on" |
| 0x0E | 2 | 1 | |
| 0x12 | 103 | 110 | |
| 0x14 | 0 | 1 | Listed as boolean |
| 0x1F | 158 | 144 | |
| 0x20 | 0 | 8 | |
| 0x22 | 51 | 1 | Listed as boolean, but 51 while working |
| 0x2F | 10 | 20 | |
| 0x38 | 26 | 30 | |
| 0x3A | 0 | 2 | |
| 0x4E | 104 | 0 | Same value as 0x0B while working |
| 0xA1 | 214 | 0 | |
| 0xAF | 68 | 47 – 48 | See table above |
| 0xB7 | 230 | 234 | |
| 0xC6 | 143 | 75 | Listed as fast counter, but stable while off |
| 0xC7 | 0 | 2 | |
| 0xC8 | 147 | 89 | |
| 0xFA | 32 | 69 (`E`) | Rest of 0xE0 – 0xFF stays 32 |

Free-running while off (sampled every ~8 s for 2 min): 0x00, 0x13 (wraps at 120), 0x4B (~5 per second), 0x4F, 0x55 – 0x5B, 0x68, 0xAB, 0xC2, 0xC4 – look like counters or noise. 0x2E / 0x6B count down and reset about every 68 s (remote contact, as documented). 0x23 creeps up 1 → 7. 0x83 is 200 with short dips to 193 – 198 and 0x96 moves between 36 and 42: possibly analog readings.

# Brazier cleaning at flame 3 (17/11/2026 09:28)

Stove working at flame 3 (actual 3), fan speed 1, fumes 136 °C. 41 RAM addresses sampled every ~7 s from 09:28:05 to 09:30:47; the brazier cleaning (state 5) ran from about 09:28:41 to 09:29:10.

| RAM | Before | During cleaning | After | Conclusion |
|---|---|---|---|---|
| 0x21 | 4 | 5 | 4 | State |
| 0x2F | 10 | 25 → 18 → 11 → 3 (~1 per second) | 10 | Hypothesis: countdown in seconds of the current phase. The cleanings in the log lasted 24 – 26 s. 20 while off |
| 0x32 | 1 | 3 | 45 → 44 → 43 | Minutes to the next cleaning (confirmed). Shows 3 during the cleaning itself (0 on 16/11) |
| 0x36 / 0x37 | 178 / 179 | 238 / 238 – 239 | 178 | Fume fan target / actual: the fan speeds up to ~2630 rpm for the cleaning |
| 0x0D | 62 (6.2) | 33 (3.3) | 62 | Auger: 6.2 = EEPROM 0x07, so the flame 3 value is verified. 3.3 during cleaning matches no EEPROM auger value |
| 0x09 / 0x43 | 10 / 10 | 3 / 2 → 0 / 0 | 10 / 10 | Drop during cleaning (also 0 in standby) |
| 0x0B / 0x4E / 0x12 | 105 / 105 / 104 | 102 / 102 / 101 | 105 / 105 / 105 | Move together |
| 0x07 | 1 | 4 | 4 → 3 → 2 (per minute) | The 4-minute cycle restarted at the cleaning |

Other findings at flame 3:

- Room fan power RAM 0x0A = 27 at speed 1 (0xA7 = 74). Speed 2 / 3 at flame 3 not measured yet.
- RAM 0xD3 = 3, RAM 0x04 = 3, RAM 0x0E = 3. With the earlier values (0xD3 / 0x04 = 1 at flame 1, 2 at flame 2; 0x0E = 2 at flame 2, 1 off) all three follow the actual flame power. 0xD3 stays at least 1 (also with the fan stopped, which is why it looked unreliable); 0x04 is 0 while the stove is too cold for the fan.
- RAM 0x36 = 178 vs EEPROM 0x13 = 180 (and 134 / 154 vs 0x11 / 0x12 = 135 / 155): EEPROM 0x10 – 0x15 is very likely the fume fan target per power level. RAM is 1 – 2 lower, possibly because combustion quality is set to −1.
- RAM 0xA1 = 238 (214 at flame 2, 0 off): depends on the power.
- RAM 0xAF = 92 – 93 (68 – 75 at flame 2, 47 – 48 cold): keeps rising while burning, consistent with a temperature in x/2.
- RAM 0xC6 / 0xC8 = 75 / 89, 0x14 = 1, 0x22 = 1, 0x3A = 3, 0xC7 = 2: the same as (or close to) the values with the stove off. These do **not** follow on / off as the 16/11 comparison suggested.
- RAM 0x89 kept dipping 20 → 19 for ~37 s while working (08:20, 08:34).
- EEPROM 0x0A – 0x0F = 55 / 25 / 17 / 155 / 35 / 150, meaning unknown.

# Remote control keys (17/11/2026, /api/keywatch)

The GRF remote talks to the stove board over radio; that traffic is not on the serial bus. RAM 0x08, 0x15, 0x16, 0x18, 0x19, 0x1A and 0x24 were read back to back (19.9 reads per second, ~0.35 s per address, no read errors) for 2 minutes while the keys were pressed on the remote (stove working).

| Key | RAM 0x08 | Effect seen |
|---|---|---|
| ▼ | 1 | Main screen: opens the set point (0x18 = 126, 0x19 = set point); in an edit: 0x19 − 1 |
| ▲ | 2 | Main screen: opens the set point; in an edit: 0x19 + 1 |
| ESC | 4 | Leaves the menu (0x1A → 0) |
| OK | 8 | Main screen: opens the menu (0x1A = 1, 0x15 / 0x18 = 130 MULTIFUOCO); in a menu enters / leaves the parameter (0x18 130 ↔ 0); in the set point screen confirms and closes it (0x18 → 0) |
| ⏻ | ? | Not pressed |

- 0x08 is set at the key press and cleared together with 0x16 when 0x24 counts down to 0, ~0.7 – 1.2 s later. The values look like bits, so a combination of keys may give the sum (not tested).
- 0x16 counts the key events of one press: 1 on the press, then +1 for every auto-repeat step while held (37 after holding ▼ ~9 s). Every step changes 0x19 by one; during a long hold the stepping accelerates and the value wraps within 7 – 30.
- 0x24 jumps to 3 – 7 at every key event and counts down to 0; the key is released when it reaches 0.
- Order within one sample round: 0x24, 0x08, 0x16 change first, then 0x18 / 0x19 / 0x1A, so the stove handles the key right after storing it.

## Writing a key from the serial bus (17/11/2026, remote on the main screen)

| Write | Read back (first read ~1 s later) | Effect |
|---|---|---|
| RAM 0x08 = 2 (▲) | 0x08 = 224, 0x16 = 1, 0x24 = 0; all 0 a second later | None: no set point screen (0x18 stayed 0) |
| RAM 0x24 = 7, then 0x08 = 2 (60 ms later) | 0x08 = 16, 0x16 = 1, 0x24 = 2 → 0; all 0 two seconds later | None: no set point screen, stove state unchanged |

The stove accepts the writes and counts a key event (0x16 = 1), but rewrites 0x08 to another value (224 / 16, meaning unknown) and nothing happens on the remote (checked on the remote itself: no screen change, no beep). Hypothesis: the menus run in the remote itself (it also keeps the display settings, see DISPLAY above); 0x08 / 0x16 / 0x24 only report the key to the stove, so writing them cannot drive the remote. Emulating keys this way does not work; a virtual remote would need the radio link. Settings are better written directly (set point, flame, fan, ... as the API does).
