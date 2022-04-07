RAM (0x00)

| ADDR (HEX) | ADDR (DEC) | Formula   | Unit   | Description                                 | Example or default value | Range      |
|------------|------------|-----------|--------|---------------------------------------------|--------------------------|------------|
| 0x01       | 1          | x/2       | °C     | Ambient temperature                         | 40 (== 20)               |            |
| 0x04       | 4          | x         | Bool   | Hot enough for fan?                         |                          |            |
| 0x0D       | 13         | x/10      | rpm    | Feed rate ???                               |                          |            |
| 0x13       | 19         |           |        | Countdown Counter ? 120 -> 0                |                          |            |
| 0x14       | 20         |           |        | Unknown Boolean value                       |                          |            | 
| 0x15       | 21         |           |        | Unknwon value                               |                          |            |
| 0x19       | 25         |           |        | Unknown Boolean value /Last changed value?  |                          |            |
| 0x21       | 33         | x         | state  | Stove State                                 | 0                        |            |
| 0x22       | 34         |           |        | Unknown Boolean value                       |                          |            |
| 0x23       | 35         |           |        | Countdown Counter 8 -> 0                    |                          |            |
| 0x2E       | 46         |           | Sec    | Countdown since last contact Remote Control |                          | 0 - 240    |
| 0x30       | 48         | x         | Sec    | Time till error code? becomse active        | 7                        |            |
| 0x31       | 49         | x         | sec    | Seconds countdown                           |                          | 0 - 59     |
| 0x33       | 51         | bitmask   | error  | Errorcode in a bitmask? 32 = E1 ?           | 32                       |            |
| 0x34       | 52         | x         | amount | Flame power                                 |                          |            |
| 0x37       | 55         | (x+25)*10 | rpm    | smoke fan RPM                               | 130 (== 1550 )           |            |
| 0x3E       | 62         | x         | °C     | Fumesmoke temperature                       | 105                      |            |
| 0x44       | 68         | x/2       | °C     | Stove room temp ??                          |                          |            |
| 0x48       | 72         | (x+25)*10 | rpm    | smoke fan RPM                               | 130 (== 1550 )           |            |
| 0x4B       | 75         | x         |        | Counter                                     |                          | 0 - 254    |
| 0x4D       | 77         | x         |        | Flip flop 0 / 1                             |                          | 0 - 1      |
| 0x4F       | 79         |           |        | Random ???                                  |                          |            |
| 0x50       | 80         | x         | Min    | Time remaining pallets when low             | 0                        |            |
| 0x55       | 85         |           |        | Looks random                                |                          |            |
| 0x56       | 86         |           |        | Looks random                                |                          |            |
| 0x57       | 87         |           |        | Random? Jumps every second                  |                          |            |
| 0x58       | 88         |           |        | Looks random                                |                          |            |
| 0x59       | 89         |           |        | Looks random                                |                          |            |
| 0x5A       | 90         |           |        | Looks random                                |                          |            |
| 0x5B       | 91         |           |        | Looks random                                |                          |            |
| 0x68       | 104        |           |        | Looks random                                |                          |            |
| 0x6B       | 107        |           | Sec    | Countdown since last contact Remote Control |                          | 0 - 240    |
| 0x6D       | 109        | x         | Sec    | Time till error code becomse active         | 20                       | 0 - 20     |
| 0x79       | 121        |           |        | Unknown Boolean value                       |                          |            |
| 0x7A       | 122        | x         | Sec    | Current time (sec)                          | 1                        | 0 - 59     |
| 0x7B       | 123        | x         | id     | Day of week 1=mon / 7=sun                   | 1                        | 1 - 7      |
| 0x7C       | 124        | x         | Hour   | Current time (hour)                         | 8                        | 0 - 23     |
| 0x7D       | 125        | x         | Min    | Current time (min)                          | 12                       | 0 - 59     |
| 0x7E       | 126        | x         | day    | Current Day                                 | 28                       | 1 - 31     |
| 0x7F       | 127        | x         | month  | Current Month                               | 1                        | 1 - 12     |
| 0x80       | 128        | x         | year   | Current year                                | 22                       | 0 - 99     |
| 0x81       | 129        |           |        | Counter up                                  |                          |            |
| 0x82       | 130        |           |        | Unknown counts up really slow ?             |                          |            |
| 0x8F       | 143        | x/2       | °C     | Remote control temp                         |                          |            |
| 0x96       | 150        |           |        | Unkown 84 - 89                              |                          |            |
| 0xAB       | 171        |           |        | Looks random                                |                          |            |
| 0xAF       | 175        |           |        | Unknown 50 / 51                             |                          |            |
| 0xC2       | 194        |           |        | Looks random (0 - 2 - 6 - 7 )               |                          |            |
| 0xC4       | 196        |           |        | Looks random                                |                          |            |
| 0xD0       | 208        | x/2       | °C     | Stove room temp ??                          |                          |            |
| 0xD2       | 210        |           |        | Unknown 58 / 57                             |                          |            |
| 0XD3       | 211        | x         | Bool   | Room fan is running                         |                          |            |
| 0xDF       | 223        | x/10      | rpm    | Feed rate ???                               |                          |            |


Low pallets changes ( To check when pallets are low again)

0x97 (151) =   0 =>  3  ?? |  0 == No Pallets /  3 == All normal => Verified, looks correct
0xC7 (199) =   2 =>  0  ?? |  2 == No Pallets /  0 == All normal?
0xBB (187) =   0 => 30     |  0 == No Pallets / 30 == All Normal?
0xA4 (164) =   5 =>  0     |  5 == No Pallets /  0 == All normal  ======= !!!!
0x9E (158) =  76 => 77     | 76 == No pallets / 77 == All normal 


0x1F (31) & 0x20 (32) == Temperature sensed by Remote Control ?
Addr 31 | dec 30 == 00011110 => Remote Control Temp
Addr 31 | dec 14 == 00001110 => Stove Temp
Addr 32 | dec 8  == 00001000 => Stove Temp
Addr 31 | dec 0  == 00000000 => Remote control Temp

0x1F (31) & 0x20 (32) == Temperature sensed by Remote Control ? => When Stove off
Addr 31 | dec 18 == 00010010
Addr 32 | dec 0  == 00000000


0x1F (31) & 0x20 (32) == Temperature sensed by Remote Control ? => When Stove off & remote offline?
Addr 31 | dec 16 == 00010000
Addr 32 | dec 0  == 00000000


0xD0 temp stove?
0x44 temp stove?
0xAF temp stove?

0x2F temp stove?
0x35 temp stove
0x46
0x6D
0x89


EEPROM (0x20)

| ADDR (HEX) | ADDR (DEC) | Formula | Unit   | Description                            | Example or default value | Range      |
|------------|------------|---------|--------|----------------------------------------|--------------------------|------------|
| 0x04       | 4          | x/10    | kg/h   | Pellets on ignition                    | 27 == 2,7                |            |
| 0x05       | 5          | x/10    | kg/h   | Pellets on level 1                     | 30 == 3                  |            |
| 0x06       | 6          | x/10    | kg/h   | Pellets on level 2                     | 46 == 4,6                |            |   
| 0x07       | 7          | x/10    | kg/h   | Pellets on level 3                     | 62 == 6,2                |            | 
| 0x08       | 8          | x/10    | kg/h   | Pellets on level 4                     | 78 == 7,8                |            | 
| 0x09       | 9          | x/10    | kg/h   | Pellets on level 5                     | 78 == 7,8                |            | 
| 0x48       | 72         | x       | bool   | Ext Thermostat Modulate = 0 / Stop = 1 | 0                        | 0 - 1      |
| 0x49       | 73         | x       | bool   | Ext Thermostat N.O. = 0 / N.C. = 1     | 0                        | 0 - 1      |
| 0x4B       | 75         | x       | bool   | Beep is enabled                        | 1                        | 0 - 1      |
| 0x4F       | 79         | x       | id     | Language remote, see lookup table      | 1                        | 1 - 8      |
| 0x7D       | 125        | x       | °C     | Setpoint Room Temperature              | 23                       |            |
| 0x7F       | 127        | x       | amount | Flame power level                      |                          | 1 - 4      |
| 0x81       | 129        | x       | speed  | Room fan speed                         | 0                        | 0 - 3      |
| 0xC0       | 192        | x       | id     | Combustion Quality, see lookup         | 76                       |            |
| 0xC6       | 198        | x       | bool   | Low pallets monitoring                 | 1                        | 0 - 1      |
| 0xC4       | 196        | x       | bool   | Pallet contact N.O. = 0 / N.C. = 1     | 0                        | 0 - 1      |
| 0xE0       | 224        | x       | error  | Memory Error - Position 1 - See table  |                          |            |
| 0xE1       | 225        | x       | error  | Memory Error - Position 2 - See table  |                          |            |
| 0xE2       | 226        | x       | error  | Memory Error - Position 3 - See table  |                          |            |
| 0xE3       | 227        | x       | error  | Memory Error - Position 4 - See table  |                          |            |
| 0xE4       | 228        | x       | error  | Memory Error - Position 5 - See table  |                          |            |
| 0xE7       | 231        | x       | hours  | Working hours ???                      |                          |            |
| 0xEA       | 234        | x       | hours  | Working hours ???                      |                          |            |
| 0xEC       | 236        | x       | hours  | Working hours ???                      |                          |            |
| 0xEE       | 238        | x       | amount | Amount of starts                       | 1                        | ???        |
| 0xFA       | 250        |         |        | Unknown                                |                          |            |


# Error codes

| Val   | Binary   | Error Code  | Description                                                                   |
|-------|----------|-------------|-------------------------------------------------------------------------------|
|   0   | 00000000 | -           | No Error                                                                      |
|   1   | 00000001 | E8          | Fume sensor error                                                             |
|   2   | 00000010 | E4          | Fume temperature to high                                                      |
|   4   | 00000100 | E9          | Ignition temperature not reached within limit                                 |
|   8   | 00001000 | E7          | Fume temperatuer to low                                                       |
|  16   | 00010000 | E3          | Temperature inside stove to high                                              |
|  32   | 00100000 | E1          | Pallet loading door is open for to long or air pressure inside stove is wrong |
|  64   | 01000000 | E2          | Pressure sensor error during ignition                                         |
| 128   | 10000000 | E6          | Auger error - To much pallets are dispatched                                  |
| 129   | 10000001 | E12         | Fume fan error                                                                |
| 130   | 10000010 | E11         | Unknown error - Not described in manual                                       |
| 132   | 10000100 | E19         | External contact N.PEL / Pallet is active                                     |
| 136   | 10001000 | E13         | Unknown error - Not described in manual                                       |
| 255   | 11111111 | Power Error | Display shows power error                          

Coundn't reverse the following error codes :
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
