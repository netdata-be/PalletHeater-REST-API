#pragma once
#include <constants.h>

// Forward declarations for functions defined in main.cpp
bool        setFlamePower(int power);
bool        setRoomFanSpeed(int speed);
bool        setEnergySavingStart(int start);
bool        setEnergySavingStop(int stop);
void        setCombustionQuality(int val);
void        setStoveDateTime();
void        getStoveState();
int         combustionQualityFromLabel(const String& label);
int         pelletQualityFromLabel(const String& label);
const char* getCombustionQualityLabel(int val);
const char* getRemoteMenuName(int id);
const char* getSelectedParameterName(int id);

// ─────────────────────────────────────────────
// State machine
// ─────────────────────────────────────────────

enum TelnetState {
    TS_NONE,
    TS_MAIN_MENU,
    TS_LIVE_SELECT,
    TS_LIVE_RUNNING,
    TS_CONFIG_MENU,
    TS_CONFIG_INPUT,
    TS_DEBUG_MENU,
    TS_DEBUG_RAW_READ,
    TS_DEBUG_RAW_WRITE,
    TS_WIFI_MENU,
    TS_WIFI_HOSTNAME_INPUT,
    TS_WIFI_IP_MENU,
    TS_WIFI_STATIC_IP,
    TS_WIFI_STATIC_NETMASK,
    TS_WIFI_STATIC_GW,
    TS_WIFI_STATIC_DNS,
    TS_WIFI_CONFIRM
};

enum TelnetConfigField {
    CF_NONE,
    CF_POWER,
    CF_FLAME_POWER,
    CF_FAN_SPEED,
    CF_ENERGY_SAVE_START,
    CF_ENERGY_SAVE_STOP,
    CF_COMBUSTION_QUALITY,
    CF_PELLET_QUALITY,
    CF_SYNC_TIME
};

static TelnetState       telnetState    = TS_NONE;
static TelnetConfigField telnetConfig   = CF_NONE;
static uint16_t          liveValuesMask = 0;
static unsigned long     lastLiveUpdate = 0;

// WiFi static IP wizard — accumulates entries across states
static String wifiWizardIP      = "";
static String wifiWizardNetmask = "";
static String wifiWizardGW      = "";

// Pending static IP confirmation — if not confirmed within 60s, reverts to DHCP
static bool          wifiPendingConfirm  = false;
static unsigned long wifiConfirmDeadline = 0;
static String        wifiPendingIP       = "";
static String        wifiPendingNetmask  = "";
static String        wifiPendingGW       = "";
static String        wifiPendingDNS      = "";

// ─────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────

static void tp(const String& s)  { telnet.print(s); }
static void tpl(const String& s) { telnet.println(s); }

// States where a single keypress should fire immediately (no Enter needed)
static bool isCharModeState(TelnetState s) {
    switch (s) {
        case TS_MAIN_MENU:
        case TS_CONFIG_MENU:
        case TS_DEBUG_MENU:
        case TS_WIFI_MENU:
        case TS_WIFI_IP_MENU:
        case TS_WIFI_CONFIRM:
        case TS_LIVE_RUNNING:
            return true;
        default:
            return false;
    }
}

static void setTelnetState(TelnetState s) {
    telnetState = s;
    telnet.setLineMode(!isCharModeState(s));
}

// ─────────────────────────────────────────────
// Menu printers
// ─────────────────────────────────────────────

static void telnetShowMainMenu();  // forward declaration
static void telnetShowWifiMenu();  // forward declaration

static void telnetPrintWifiInfo()
{
    tpl("\r\n=== WiFi Info ===");

    wl_status_t status = WiFi.status();
    const char* statusStr =
        status == WL_CONNECTED       ? "Connected" :
        status == WL_DISCONNECTED    ? "Disconnected" :
        status == WL_CONNECTION_LOST ? "Connection lost" :
        status == WL_CONNECT_FAILED  ? "Connect failed" :
        status == WL_NO_SSID_AVAIL   ? "SSID not found" :
        status == WL_IDLE_STATUS     ? "Idle" : "Unknown";

    tpl("Status:       " + String(statusStr));
    tpl("SSID:         " + WiFi.SSID());
    tpl("BSSID (AP):   " + WiFi.BSSIDstr());
    tpl("MAC:          " + WiFi.macAddress());
    tpl("Hostname:     " + String(WiFi.getHostname()));
    tpl("IP:           " + WiFi.localIP().toString());
    tpl("Gateway:      " + WiFi.gatewayIP().toString());
    tpl("Subnet:       " + WiFi.subnetMask().toString());
    tpl("DNS:          " + WiFi.dnsIP().toString());
    tpl("Channel:      " + String(WiFi.channel()));

    int rssi = WiFi.RSSI();
    const char* rssiLabel =
        rssi >= -50 ? "Excellent" :
        rssi >= -60 ? "Good" :
        rssi >= -70 ? "Fair" :
        rssi >= -80 ? "Weak" : "Very weak";
    char rssiLine[48];
    snprintf(rssiLine, sizeof(rssiLine), "%d dBm (%s)", rssi, rssiLabel);
    tpl("RSSI:         " + String(rssiLine));

    int8_t txPower = WiFi.getTxPower();
    tpl("TX power:     " + String(txPower * 0.25f, 2) + " dBm (raw: " + String(txPower) + ")");

    tpl("=================");
}

static void telnetShowWifiMenu()
{
    tpl("\r\n=== WiFi ===");
    tpl("  1) Show info");
    tpl("  2) Set hostname       current: " + String(WiFi.getHostname()));
    tpl("  3) Configure IP       current: " + WiFi.localIP().toString());
    tpl("  9) Back");
    tp("> ");
    setTelnetState(TS_WIFI_MENU);
}

static void telnetShowWifiIpMenu()
{
    tpl("\r\n=== IP Configuration ===");
    tpl("  1) DHCP (auto)");
    tpl("  2) Static IP");
    tpl("  9) Back");
    tp("> ");
    setTelnetState(TS_WIFI_IP_MENU);
}

static void telnetHandleWifiMenu(const String& input)
{
    if (input == "9") { telnetShowMainMenu(); return; }

    if (input == "1") {
        telnetPrintWifiInfo();
        telnetShowWifiMenu();
        return;
    }

    if (input == "2") {
        tp("New hostname (current: " + String(WiFi.getHostname()) + "): ");
        setTelnetState(TS_WIFI_HOSTNAME_INPUT);
        return;
    }

    if (input == "3") {
        telnetShowWifiIpMenu();
        return;
    }

    tpl("Invalid option.");
    telnetShowWifiMenu();
}

static void telnetHandleWifiHostname(const String& input)
{
    if (input.length() == 0 || input.length() > 32) {
        tpl("Invalid hostname (1-32 chars).");
        telnetShowWifiMenu();
        return;
    }
    WiFi.setHostname(input.c_str());
    WiFi.reconnect();
    tpl("Hostname set to '" + input + "'. Reconnecting WiFi...");
    telnetShowWifiMenu();
}

static void telnetHandleWifiIpMenu(const String& input)
{
    if (input == "9") { telnetShowWifiMenu(); return; }

    if (input == "1") {
        // Reset to DHCP
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.reconnect();
        tpl("Switched to DHCP. Reconnecting WiFi...");
        telnetShowWifiMenu();
        return;
    }

    if (input == "2") {
        tpl("Current IP:      " + WiFi.localIP().toString());
        tpl("Current netmask: " + WiFi.subnetMask().toString());
        tpl("Current gateway: " + WiFi.gatewayIP().toString());
        tpl("Current DNS:     " + WiFi.dnsIP().toString());
        tp("IP address: ");
        wifiWizardIP = wifiWizardNetmask = wifiWizardGW = "";
        setTelnetState(TS_WIFI_STATIC_IP);
        return;
    }

    tpl("Invalid option.");
    telnetShowWifiIpMenu();
}

static void telnetHandleWifiStaticIP(const String& input)
{
    IPAddress addr;
    if (!addr.fromString(input)) {
        tpl("Invalid IP address.");
        tp("IP address: ");
        return;
    }
    wifiWizardIP = input;
    tp("Netmask: ");
    setTelnetState(TS_WIFI_STATIC_NETMASK);
}

static void telnetHandleWifiStaticNetmask(const String& input)
{
    IPAddress addr;
    if (!addr.fromString(input)) {
        tpl("Invalid netmask.");
        tp("Netmask: ");
        return;
    }
    wifiWizardNetmask = input;
    tp("Gateway: ");
    setTelnetState(TS_WIFI_STATIC_GW);
}

static void telnetHandleWifiStaticGW(const String& input)
{
    IPAddress addr;
    if (!addr.fromString(input)) {
        tpl("Invalid gateway.");
        tp("Gateway: ");
        return;
    }
    wifiWizardGW = input;
    tp("DNS server: ");
    setTelnetState(TS_WIFI_STATIC_DNS);
}

static void telnetHandleWifiStaticDNS(const String& input)
{
    IPAddress dns;
    if (!dns.fromString(input)) {
        tpl("Invalid DNS address.");
        tp("DNS server: ");
        return;
    }

    // Save pending config for rollback
    wifiPendingIP      = wifiWizardIP;
    wifiPendingNetmask = wifiWizardNetmask;
    wifiPendingGW      = wifiWizardGW;
    wifiPendingDNS     = input;

    // Apply and reconnect — this session will likely drop
    IPAddress ip, netmask, gw;
    ip.fromString(wifiWizardIP);
    netmask.fromString(wifiWizardNetmask);
    gw.fromString(wifiWizardGW);

    WiFi.config(ip, gw, netmask, dns);
    WiFi.reconnect();

    tpl("Applying static IP: " + wifiWizardIP);
    tpl("Telnet to " + wifiWizardIP + " within 60s to confirm.");
    tpl("No confirmation = auto-revert to DHCP.");

    wifiPendingConfirm  = true;
    wifiConfirmDeadline = millis() + 60000;
    // current session will disconnect as IP changes
}

static void telnetShowWifiConfirm()
{
    int secsLeft = max(0L, (long)(wifiConfirmDeadline - millis()) / 1000);
    tpl("\r\n*** Static IP confirmation required ***");
    tpl("New IP:   " + wifiPendingIP);
    tpl("Netmask:  " + wifiPendingNetmask);
    tpl("Gateway:  " + wifiPendingGW);
    tpl("DNS:      " + wifiPendingDNS);
    tpl("Auto-reverts to DHCP in " + String(secsLeft) + " seconds.");
    tpl("");
    tpl("  1) Confirm — keep this IP");
    tpl("  2) Revert  — switch back to DHCP now");
    tp("> ");
    setTelnetState(TS_WIFI_CONFIRM);
}

static void telnetHandleWifiConfirm(const String& input)
{
    if (input == "1") {
        wifiPendingConfirm = false;
        tpl("Static IP confirmed. Settings kept.");
        telnetShowMainMenu();
    } else {
        wifiPendingConfirm = false;
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.reconnect();
        tpl("Reverting to DHCP. Reconnecting...");
        telnetShowMainMenu();
    }
}

static void telnetShowMainMenu()
{
    // Current date/time from NTP
    char timeBuf[24] = "??:??:??";
    char dateBuf[16] = "????-??-??";
    struct tm now;
    if (getLocalTime(&now, 100)) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &now);
        strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &now);
    }

    tpl("______ _                   _   _        ");
    tpl("| ___ (_)                 | | | |       ");
    tpl("| |_/ /_  __ _ ___________| |_| |_ __ _ ");
    tpl("|  __/| |/ _` |_  /_  / _ \ __| __/ _` |");
    tpl("| |   | | (_| |/ / / /  __/ |_| || (_| |");
    tpl("\_|   |_|\__,_/___/___\___|\__|\__\__,_|");
    tpl("   PelletHeater  by  N E T D A T A");
    tpl("");
    tpl("  Firmware: " + cachedFirmwareVersion);
    tpl("  Date:     " + String(dateBuf));
    tpl("  Time:     " + String(timeBuf));
    tpl("");
    tpl("  1) Show live values");
    tpl("  2) Configure");
    tpl("  3) WiFi info");
    tpl("  4) Debug");
    tpl("  5) Reboot");
    tpl("  6) Exit");
    tp("> ");
    setTelnetState(TS_MAIN_MENU);
}

static void telnetShowDebugMenu()
{
    String status = stove.isDebugEnabled() ? "ON " : "OFF";
    tpl("\r\n=== Debug (serial debug: " + status + ") ===");
    tpl("  1) Toggle serial debug (currently: " + status + ")");
    tpl("  2) Show debug log");
    tpl("  3) Clear debug log");
    tpl("  4) Raw read   (hex: type address  e.g. '00 1a')");
    tpl("  5) Raw write  (hex: type address value  e.g. '00 21 01')");
    tpl("  9) Back");
    tp("> ");
    setTelnetState(TS_DEBUG_MENU);
}

static void telnetShowLiveSelect()
{
    tpl("\r\nSelect values to monitor (comma-separated, e.g. 1,3 or 'all'):");
    tpl("  1) Stove state");
    tpl("  2) Temperatures");
    tpl("  3) Flame power");
    tpl("  4) Fan speeds");
    tpl("  5) Fume fan RPM");
    tpl("  6) Auger speed");
    tpl("  7) Pallet alarm");
    tpl("  8) Energy saving");
    tpl("  9) Remote control");
    tpl("  0) Back");
    tp("> ");
    setTelnetState(TS_LIVE_SELECT);
}

static void telnetShowConfigMenu()
{
    tpl("\r\n=== Configure ===");
    tpl("  1) Power state (on/off)           current: " + String(StoveIsOn ? "on" : "off"));
    tpl("  2) Flame power       [1-4]         current: " + String(flamePower));
    tpl("  3) Room fan speed    [0-3]         current: " + String(roomFanSpeed));
    tpl("  4) Energy save start [0,-1,-2,-3]  current: " + String(energySavingStart));
    tpl("  5) Energy save stop  [0,1,2,3]     current: " + String(energySavingStop));
    tpl("  6) Combustion quality              current: " + String(getCombustionQualityLabel(cachedCombustionQuality)));
    tpl("  7) Pellet quality                  current: " + String(getCombustionQualityLabel(cachedPelletQuality)));
    tpl("  8) Sync NTP time to stove");
    tpl("  9) Back");
    tp("> ");
    setTelnetState(TS_CONFIG_MENU);
}

// ─────────────────────────────────────────────
// Live values printer
// ─────────────────────────────────────────────

static void telnetPrintLiveValues()
{
    tpl("\r\n--- Live ---");
    if (liveValuesMask & (1 << 0)) {
        tpl("State:        " + String(stoveState) + " (" + StoveStateStr + ")");
    }
    if (liveValuesMask & (1 << 1)) {
        tpl("Temp room:    " + String(ambTemp)             + " C   setpoint: " + String(setPointTemp) + " C");
        tpl("Temp stove:   " + String(ambTempStove)        + " C   remote:   " + String(ambTempRemoteControl) + " C");
        tpl("Fume temp:    " + String(fumesTemp)           + " C");
    }
    if (liveValuesMask & (1 << 2)) {
        tpl("Flame power:  " + String(flamePower));
    }
    if (liveValuesMask & (1 << 3)) {
        tpl("Room fan:     " + String(roomFanSpeed));
    }
    if (liveValuesMask & (1 << 4)) {
        tpl("Fume fan RPM: " + String(cachedFumeFanRPM));
    }
    if (liveValuesMask & (1 << 5)) {
        tpl("Auger speed:  " + String(cachedAugerSpeed));
    }
    if (liveValuesMask & (1 << 6)) {
        tpl("Low pallet:   " + String(cachedLowPallet ? "YES" : "no"));
    }
    if (liveValuesMask & (1 << 7)) {
        tpl("Energy save:  start=" + String(energySavingStart) + "  stop=" + String(energySavingStop));
    }
    if (liveValuesMask & (1 << 8)) {
        int menuId  = stove.read(ram, displayMenuAddr);
        int paramId = stove.read(ram, selectedParamAddr);
        const char* param = (paramId >= 0) ? getSelectedParameterName(paramId) : "?";
        tpl("Remote:       last seen " + String(cachedRemoteControlLastSeen) + "s ago");
        tpl("  menu:       " + String(menuId  >= 0 ? getRemoteMenuName(menuId) : "?"));
        tpl("  selected:   " + String(param ? param : "none"));
    }
    tp("[any key to stop]");
}

// ─────────────────────────────────────────────
// Input handlers
// ─────────────────────────────────────────────

static void telnetHandleMainMenu(const String& input)
{
    if      (input == "1") { telnetShowLiveSelect();  }
    else if (input == "2") { telnetShowConfigMenu();  }
    else if (input == "3") { telnetShowWifiMenu();    }
    else if (input == "4") { telnetShowDebugMenu();   }
    else if (input == "5") { tpl("Rebooting..."); delay(500); ESP.restart(); }
    else if (input == "6") { tpl("Goodbye."); telnet.disconnectClient(); telnetState = TS_NONE; }
    else                   { tpl("Invalid option."); telnetShowMainMenu(); }
}

static void telnetHandleDebugMenu(const String& input)
{
    if (input == "9") { telnetShowMainMenu(); return; }

    if (input == "1") {
        bool next = !stove.isDebugEnabled();
        stove.setDebug(next);
        tpl("Serial debug: " + String(next ? "ON" : "OFF"));
        telnetShowDebugMenu();
        return;
    }

    if (input == "2") {
        if (stoveDebugBuffer.length() == 0) {
            tpl("(log is empty)");
        } else {
            tpl("--- debug log ---");
            tpl(stoveDebugBuffer);
            tpl("--- end ---");
        }
        telnetShowDebugMenu();
        return;
    }

    if (input == "3") {
        stoveDebugBuffer = "";
        tpl("Log cleared.");
        telnetShowDebugMenu();
        return;
    }

    if (input == "4") {
        tp("type address (hex, e.g. '00 1a'): ");
        setTelnetState(TS_DEBUG_RAW_READ);
        return;
    }

    if (input == "5") {
        tp("type address value (hex, e.g. '00 21 01'): ");
        setTelnetState(TS_DEBUG_RAW_WRITE);
        return;
    }

    tpl("Invalid option.");
    telnetShowDebugMenu();
}

static void telnetHandleDebugRawRead(const String& input)
{
    unsigned int type = 0, address = 0;
    if (sscanf(input.c_str(), "%x %x", &type, &address) != 2) {
        tpl("Invalid format. Use hex: '00 1a'");
        telnetShowDebugMenu();
        return;
    }
    bool prevDebug = stove.isDebugEnabled();
    stove.setDebug(true);
    stoveDebugBuffer = "";

    int val = stove.read((byte)type, (byte)address);

    tpl(stoveDebugBuffer);
    if (val >= 0) {
        char result[48];
        snprintf(result, sizeof(result), "Result: 0x%02X (%d)", (uint8_t)val, val);
        tpl(result);
    }
    stove.setDebug(prevDebug);
    telnetShowDebugMenu();
}

static void telnetHandleDebugRawWrite(const String& input)
{
    unsigned int type = 0, address = 0, value = 0;
    if (sscanf(input.c_str(), "%x %x %x", &type, &address, &value) != 3) {
        tpl("Invalid format. Use hex: '00 21 01'");
        telnetShowDebugMenu();
        return;
    }
    bool prevDebug = stove.isDebugEnabled();
    stove.setDebug(true);
    stoveDebugBuffer = "";

    bool ok = stove.write((byte)type, (byte)address, (byte)value);

    tpl(stoveDebugBuffer);
    tpl(ok ? "Result: SUCCESS" : "Result: FAILED");
    stove.setDebug(prevDebug);
    telnetShowDebugMenu();
}

static void telnetHandleLiveSelect(const String& input)
{
    if (input == "0") { telnetShowMainMenu(); return; }

    if (input == "all") {
        liveValuesMask = 0x1FF;
    } else {
        liveValuesMask = 0;
        String csv = input + ",";
        int start = 0;
        for (int i = 0; i < (int)csv.length(); i++) {
            if (csv[i] == ',') {
                int n = csv.substring(start, i).toInt();
                if (n >= 1 && n <= 9) liveValuesMask |= (1 << (n - 1));
                start = i + 1;
            }
        }
    }

    if (liveValuesMask == 0) {
        tpl("No valid selection. Try again.");
        telnetShowLiveSelect();
        return;
    }
    tpl("Live mode started. Press any key to stop.");
    setTelnetState(TS_LIVE_RUNNING);
    lastLiveUpdate = 0;
}

static void telnetHandleConfigMenu(const String& input)
{
    if (input == "9") { telnetShowMainMenu(); return; }

    struct { const char* key; const char* prompt; TelnetConfigField field; } opts[] = {
        { "1", "Enter 'on' or 'off': ",                                  CF_POWER              },
        { "2", "Flame power [1-4]: ",                                    CF_FLAME_POWER        },
        { "3", "Fan speed [0-3]: ",                                      CF_FAN_SPEED          },
        { "4", "Energy saving start [0,-1,-2,-3]: ",                     CF_ENERGY_SAVE_START  },
        { "5", "Energy saving stop [0,1,2,3]: ",                         CF_ENERGY_SAVE_STOP   },
        { "6", "Combustion quality [+G,-3,-2,-1,0,+1,+2,+3]: ",         CF_COMBUSTION_QUALITY },
        { "7", "Pellet quality [+G,-3,-2,-1,0,+1,+2,+3]: ",             CF_PELLET_QUALITY     },
    };

    for (auto& o : opts) {
        if (input == o.key) {
            tp(o.prompt);
            telnetConfig = o.field;
            setTelnetState(TS_CONFIG_INPUT);
            return;
        }
    }

    if (input == "8") {
        setStoveDateTime();
        tpl("Time synced to NTP.");
        telnetShowConfigMenu();
        return;
    }

    tpl("Invalid option.");
    telnetShowConfigMenu();
}

static void telnetHandleConfigInput(const String& input)
{
    bool ok = false;

    switch (telnetConfig) {
        case CF_POWER:
            if (input == "on") {
                getStoveState();
                ok = (stoveState == OFF)
                    ? stove.write(ram, stoveStateAddr, 1)
                    : (tpl("Already on."), true);
            } else if (input == "off") {
                getStoveState();
                if (stoveState == IGNITION || stoveState == WORKING || stoveState == ALARM)
                    ok = stove.write(ram, stoveStateAddr, 6);
                else if (stoveState == STARTING || stoveState == PALLET_LOADING)
                    ok = stove.write(ram, stoveStateAddr, 0);
                else { tpl("Already off."); ok = true; }
            } else {
                tpl("Invalid. Enter 'on' or 'off'.");
                tp("> "); return;
            }
            break;

        case CF_FLAME_POWER:
            ok = setFlamePower(input.toInt());
            if (!ok) { tpl("Invalid. Must be 1-4."); tp("> "); return; }
            break;

        case CF_FAN_SPEED:
            ok = setRoomFanSpeed(input.toInt());
            if (!ok) { tpl("Invalid. Must be 0-3."); tp("> "); return; }
            break;

        case CF_ENERGY_SAVE_START: {
            int v = input.toInt() * -1;
            ok = setEnergySavingStart(v);
            if (!ok) { tpl("Invalid. Must be 0,-1,-2,-3."); tp("> "); return; }
            break;
        }

        case CF_ENERGY_SAVE_STOP:
            ok = setEnergySavingStop(input.toInt());
            if (!ok) { tpl("Invalid. Must be 0,1,2,3."); tp("> "); return; }
            break;

        case CF_COMBUSTION_QUALITY: {
            int v = combustionQualityFromLabel(input);
            if (v != -1) { setCombustionQuality(v); cachedCombustionQuality = v; ok = true; }
            else          { tpl("Invalid. Use +G,-3,-2,-1,0,+1,+2,+3."); tp("> "); return; }
            break;
        }

        case CF_PELLET_QUALITY: {
            int v = pelletQualityFromLabel(input);
            if (v != -1) { stove.write(eeprom, pelletQualityAddr, v); cachedPelletQuality = v; ok = true; }
            else          { tpl("Invalid. Use +G,-3,-2,-1,0,+1,+2,+3."); tp("> "); return; }
            break;
        }

        default: break;
    }

    tpl(ok ? "Done." : "Failed (RS232 error).");
    telnetConfig = CF_NONE;
    telnetShowConfigMenu();
}

// ─────────────────────────────────────────────
// Public interface — called from main.cpp
// ─────────────────────────────────────────────

void telnetHandleInput(const String& rawInput)
{
    String input = rawInput;
    input.trim();
    if (input.length() == 0) return;

    switch (telnetState) {
        case TS_MAIN_MENU:    telnetHandleMainMenu(input);   break;
        case TS_LIVE_SELECT:  telnetHandleLiveSelect(input); break;
        case TS_LIVE_RUNNING: tpl("Live mode stopped."); telnetShowMainMenu(); break;
        case TS_CONFIG_MENU:   telnetHandleConfigMenu(input);    break;
        case TS_CONFIG_INPUT:  telnetHandleConfigInput(input);  break;
        case TS_DEBUG_MENU:       telnetHandleDebugMenu(input);      break;
        case TS_DEBUG_RAW_READ:   telnetHandleDebugRawRead(input);   break;
        case TS_DEBUG_RAW_WRITE:  telnetHandleDebugRawWrite(input);  break;
        case TS_WIFI_MENU:        telnetHandleWifiMenu(input);       break;
        case TS_WIFI_HOSTNAME_INPUT: telnetHandleWifiHostname(input); break;
        case TS_WIFI_IP_MENU:     telnetHandleWifiIpMenu(input);     break;
        case TS_WIFI_STATIC_IP:      telnetHandleWifiStaticIP(input);      break;
        case TS_WIFI_STATIC_NETMASK: telnetHandleWifiStaticNetmask(input); break;
        case TS_WIFI_STATIC_GW:      telnetHandleWifiStaticGW(input);      break;
        case TS_WIFI_STATIC_DNS:     telnetHandleWifiStaticDNS(input);     break;
        case TS_WIFI_CONFIRM:        telnetHandleWifiConfirm(input);       break;
        default:                     telnetShowMainMenu();                  break;
    }
}

void telnetOnConnect(const String& ip)
{
    Serial.println("- Telnet: " + ip + " connected");
    tpl("\r\nConnected from " + ip);
    tpl("Firmware: " + cachedFirmwareVersion);
    if (wifiPendingConfirm) {
        telnetShowWifiConfirm();
    } else {
        telnetShowMainMenu();
    }
}

void telnetMenuLoop()
{
    // Auto-revert static IP if not confirmed in time
    if (wifiPendingConfirm && millis() > wifiConfirmDeadline) {
        wifiPendingConfirm = false;
        Serial.println("WiFi confirm timeout — reverting to DHCP");
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.reconnect();
    }

    if (telnetState != TS_LIVE_RUNNING) return;
    unsigned long now_ms = millis();
    if (now_ms - lastLiveUpdate >= 2000) {
        telnetPrintLiveValues();
        lastLiveUpdate = now_ms;
    }
}
