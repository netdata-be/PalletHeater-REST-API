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
    TS_WIFI_CONFIRM,
    TS_WIFI_INFO,
    TS_DEBUG_LOG,
    TS_TIMERS_MENU,
    TS_TIMER_EDIT_MENU,
    TS_TIMER_EDIT_INPUT,
    TS_TIMER_STEP,
    TS_TIMER_DOW_SELECT
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
    CF_BEEP,
    CF_SYNC_TIME
};

enum TelnetTimerField {
    TF_NONE,
    TF_ENABLED,
    TF_ONTIME,
    TF_OFFTIME,
    TF_SETPOINT,
    TF_FLAME,
    TF_FAN,
    TF_DOW
};

static TelnetState       telnetState       = TS_NONE;
static TelnetConfigField telnetConfig      = CF_NONE;
static TelnetTimerField  currentTimerField = TF_NONE;
static int               selectedTimerIdx  = 0;
static int               timerEditReturnCursor = 0;

// Timer stepper state
static int  timerStepValue  = 0;
static int  timerStepMin    = 0;
static int  timerStepMax    = 0;
static byte timerStepOffset = 0;
static bool timerStepIsTime = false;

// Timer DOW select state
static byte timerDowWorking = 0;
static uint16_t          liveValuesMask    = 0;
static unsigned long     lastLiveUpdate    = 0;
static int               menuCursor        = 0;
static int               menuItemCount     = 0;
static uint8_t           escState          = 0;   // 0=normal 1=got ESC 2=got ESC[

// WiFi static IP wizard
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
static void cls()                { tp("\x1B[2J\x1B[H"); }   // clear screen + cursor home

static bool isCharModeState(TelnetState s) {
    switch (s) {
        case TS_MAIN_MENU:
        case TS_CONFIG_MENU:
        case TS_DEBUG_MENU:
        case TS_WIFI_MENU:
        case TS_WIFI_IP_MENU:
        case TS_WIFI_CONFIRM:
        case TS_WIFI_INFO:
        case TS_DEBUG_LOG:
        case TS_LIVE_SELECT:
        case TS_LIVE_RUNNING:
        case TS_TIMERS_MENU:
        case TS_TIMER_EDIT_MENU:
        case TS_TIMER_STEP:
        case TS_TIMER_DOW_SELECT:
            return true;
        default:
            return false;
    }
}

static void setTelnetState(TelnetState s) {
    bool entering_char = isCharModeState(s);
    bool was_char      = isCharModeState(telnetState);
    telnetState = s;
    telnet.setLineMode(!entering_char);

    if (entering_char && !was_char) {
        // Tell the client to stop buffering and suppress local echo:
        //   IAC WILL ECHO        — server will echo; client must suppress its own echo
        //   IAC WILL SUPPRESS-GA — needed for char-at-a-time mode
        //   IAC DO   SUPPRESS-GA — ask client to do the same
        const uint8_t seq[] = {
            0xFF, 0xFB, 0x01,   // IAC WILL ECHO
            0xFF, 0xFB, 0x03,   // IAC WILL SGA
            0xFF, 0xFD, 0x03    // IAC DO  SGA
        };
        telnet.write(seq, sizeof(seq));
    } else if (!entering_char && was_char) {
        // Restore line mode: undo all three char-mode options so the client
        // re-enables its line buffer and interprets \r\n correctly again.
        //   IAC WONT ECHO  — client resumes its own echo
        //   IAC WONT SGA   — re-enable go-ahead (restores line-at-a-time)
        //   IAC DONT SGA   — ask client to do the same
        const uint8_t seq[] = {
            0xFF, 0xFC, 0x01,   // IAC WONT ECHO
            0xFF, 0xFC, 0x03,   // IAC WONT SGA
            0xFF, 0xFE, 0x03    // IAC DONT SGA
        };
        telnet.write(seq, sizeof(seq));
    }
}

// ─────────────────────────────────────────────
// Arrow-key menu rendering helpers
// ─────────────────────────────────────────────

// Print a single menu row — highlighted when selected
static void menuRow(const String& label, bool selected) {
    if (selected) tp(" \x1B[7m " + label + " \x1B[0m\x1B[K\r\n");
    else          tp("   " + label + "\x1B[K\r\n");
}

static void menuMoveUp()   { menuCursor = (menuCursor - 1 + menuItemCount) % menuItemCount; }
static void menuMoveDown() { menuCursor = (menuCursor + 1) % menuItemCount; }

// ─────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────

static void telnetShowMainMenu(bool full = true);
static void telnetShowConfigMenu(bool full = true);
static void telnetShowDebugMenu(bool full = true);
static void telnetShowWifiMenu(bool full = true);
static void telnetShowWifiIpMenu(bool full = true);
static void telnetShowWifiConfirm(bool full = true);
static void telnetShowLiveSelect(bool full = true);
static void telnetShowTimersMenu(bool full = true);
static void telnetShowTimerEditMenu(bool full = true);
static void telnetShowTimerStep(bool full = true);
static void telnetShowTimerDowSelect(bool full = true);

// ─────────────────────────────────────────────
// WiFi info (not a menu — just prints then returns)
// ─────────────────────────────────────────────

static void telnetShowWifiInfo()
{
    cls();
    tpl("=== WiFi Info ===\r\n");

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
    tpl("");
    tp("  \x1B[2m[any key] back\x1B[0m\r\n");
    setTelnetState(TS_WIFI_INFO);
}

// ─────────────────────────────────────────────
// Menu: Main
// ─────────────────────────────────────────────

static void telnetDrawMainMenu()
{
    const char* items[] = {
        "Show live values",
        "Configure",
        "WiFi info",
        "Debug",
        "Reboot",
        "Exit"
    };
    for (int i = 0; i < 6; i++) menuRow(items[i], i == menuCursor);
}

static void telnetShowMainMenu(bool full)
{
    if (full) {
        char timeBuf[24] = "00:00:00";
        char dateBuf[16] = "0000-00-00";
        struct tm now;
        if (getLocalTime(&now, 100)) {
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &now);
            strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &now);
        }

        cls();
        tpl("______ _                   _   _        ");
        tpl("| ___ (_)                 | | | |       ");
        tpl("| |_/ /_  __ _ ___________| |_| |_ __ _ ");
        tpl("|  __/| |/ _` |_  /_  / _ \\ __| __/ _` |");
        tpl("| |   | | (_| |/ / / /  __/ |_| || (_| |");
        tpl("\\_|   |_|\\__,_/___/___\\___|\\__|\\__\\__,_|");
        tpl("   PelletHeater  by  N E T D A T A");
        tpl("");
        {
            String fwLine = "  Firmware: ";
            fwLine += cachedFirmwareVersion;
            tpl(fwLine);
        }
        tpl(String("  Date:     ") + dateBuf);
        tpl(String("  Time:     ") + timeBuf);
        tpl("");
        if (cachedLowPallet) {
            tpl("\x1B[41;97m  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!  \x1B[0m");
            tpl("\x1B[41;97m  !!                                     !!  \x1B[0m");
            tpl("\x1B[41;97m  !!    PALLET ALARM — REFILL PELLETS!   !!  \x1B[0m");
            tpl("\x1B[41;97m  !!                                     !!  \x1B[0m");
            tpl("\x1B[41;97m  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!  \x1B[0m");
            tpl("");
        }
        tp("\x1B[s");
        menuItemCount = 6;
    } else {
        tp("\x1B[u");
    }
    telnetDrawMainMenu();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] select\x1B[0m\r\n");
        setTelnetState(TS_MAIN_MENU);
    }
}

static void telnetHandleMainMenu(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowMainMenu(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowMainMenu(false); return; }
    if (key != "ENTER") return;

    int saved = menuCursor;
    menuCursor = 0;
    switch (saved) {
        case 0: telnetShowLiveSelect();  break;
        case 1: telnetShowConfigMenu();  break;
        case 2:
            menuCursor = 0;
            telnetShowWifiMenu();
            break;
        case 3: telnetShowDebugMenu();   break;
        case 4: tpl("Rebooting..."); delay(500); ESP.restart(); break;
        case 5: tpl("Goodbye."); telnet.disconnectClient(); telnetState = TS_NONE; break;
    }
}

// ─────────────────────────────────────────────
// Menu: WiFi
// ─────────────────────────────────────────────

static void telnetDrawWifiMenu()
{
    String items[3] = {
        "Show info",
        "Set hostname       current: " + String(WiFi.getHostname()),
        "Configure IP       current: " + WiFi.localIP().toString()
    };
    for (int i = 0; i < 3; i++) menuRow(items[i], i == menuCursor);
    menuRow("Back", 3 == menuCursor);
}

static void telnetShowWifiMenu(bool full)
{
    if (full) { cls(); tpl("=== WiFi ===\r\n"); tp("\x1B[s"); }
    else      { tp("\x1B[u"); }
    telnetDrawWifiMenu();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] select\x1B[0m\r\n");
        menuItemCount = 4;
        setTelnetState(TS_WIFI_MENU);
    }
}

static void telnetHandleWifiMenu(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowWifiMenu(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowWifiMenu(false); return; }
    if (key != "ENTER") return;

    int saved = menuCursor;
    menuCursor = 0;
    switch (saved) {
        case 0:
            telnetShowWifiInfo();
            break;
        case 1:
            tp("New hostname (current: " + String(WiFi.getHostname()) + "): ");
            setTelnetState(TS_WIFI_HOSTNAME_INPUT);
            break;
        case 2: telnetShowWifiIpMenu(); break;
        case 3: telnetShowMainMenu(); break;
    }
}

static void telnetHandleWifiHostname(const String& input)
{
    if (input.length() == 0 || input.length() > 32) {
        tpl("Invalid hostname (1-32 chars).");
        menuCursor = 1;
        telnetShowWifiMenu();
        return;
    }
    WiFi.setHostname(input.c_str());
    WiFi.reconnect();
    tpl("Hostname set to '" + input + "'. Reconnecting WiFi...");
    menuCursor = 0;
    telnetShowWifiMenu();
}

// ─────────────────────────────────────────────
// Menu: WiFi IP
// ─────────────────────────────────────────────

static void telnetDrawWifiIpMenu()
{
    const char* items[] = { "DHCP (auto)", "Static IP", "Back" };
    for (int i = 0; i < 3; i++) menuRow(items[i], i == menuCursor);
}

static void telnetShowWifiIpMenu(bool full)
{
    if (full) { cls(); tpl("=== IP Configuration ===\r\n"); tp("\x1B[s"); }
    else      { tp("\x1B[u"); }
    telnetDrawWifiIpMenu();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] select\x1B[0m\r\n");
        menuItemCount = 3;
        setTelnetState(TS_WIFI_IP_MENU);
    }
}

static void telnetHandleWifiIpMenu(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowWifiIpMenu(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowWifiIpMenu(false); return; }
    if (key != "ENTER") return;

    int saved = menuCursor;
    menuCursor = 0;
    switch (saved) {
        case 0:
            WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
            WiFi.reconnect();
            tpl("Switched to DHCP. Reconnecting WiFi...");
            telnetShowWifiMenu();
            break;
        case 1:
            tpl("Current IP:      " + WiFi.localIP().toString());
            tpl("Current netmask: " + WiFi.subnetMask().toString());
            tpl("Current gateway: " + WiFi.gatewayIP().toString());
            tpl("Current DNS:     " + WiFi.dnsIP().toString());
            tp("IP address: ");
            wifiWizardIP = wifiWizardNetmask = wifiWizardGW = "";
            setTelnetState(TS_WIFI_STATIC_IP);
            break;
        case 2: telnetShowWifiMenu(); break;
    }
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

    wifiPendingIP      = wifiWizardIP;
    wifiPendingNetmask = wifiWizardNetmask;
    wifiPendingGW      = wifiWizardGW;
    wifiPendingDNS     = input;

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
}

// ─────────────────────────────────────────────
// Menu: WiFi Confirm
// ─────────────────────────────────────────────

static void telnetDrawWifiConfirm()
{
    const char* items[] = { "Confirm — keep this IP", "Revert — switch back to DHCP" };
    for (int i = 0; i < 2; i++) menuRow(items[i], i == menuCursor);
}

static void telnetShowWifiConfirm(bool full)
{
    if (full) {
        int secsLeft = max(0L, (long)(wifiConfirmDeadline - millis()) / 1000);
        cls();
        tpl("*** Static IP confirmation required ***\r\n");
        tpl("New IP:   " + wifiPendingIP);
        tpl("Netmask:  " + wifiPendingNetmask);
        tpl("Gateway:  " + wifiPendingGW);
        tpl("DNS:      " + wifiPendingDNS);
        tpl("Auto-reverts to DHCP in " + String(secsLeft) + " seconds.\r\n");
        tp("\x1B[s");
    } else {
        tp("\x1B[u");
    }
    telnetDrawWifiConfirm();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] select\x1B[0m\r\n");
        menuItemCount = 2;
        setTelnetState(TS_WIFI_CONFIRM);
    }
}

static void telnetHandleWifiConfirm(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowWifiConfirm(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowWifiConfirm(false); return; }
    if (key != "ENTER") return;

    wifiPendingConfirm = false;
    if (menuCursor == 0) {
        tpl("Static IP confirmed. Settings kept.");
    } else {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.reconnect();
        tpl("Reverting to DHCP. Reconnecting...");
    }
    menuCursor = 0;
    telnetShowMainMenu();
}

// ─────────────────────────────────────────────
// Menu: Debug
// ─────────────────────────────────────────────

static void telnetDrawDebugMenu()
{
    String status = stove.isDebugEnabled() ? "ON " : "OFF";
    String items[6] = {
        "Toggle serial debug           currently: " + status,
        "Show debug log",
        "Clear debug log",
        "Raw read   (hex: type addr   e.g. '00 1a')",
        "Raw write  (hex: type addr val  e.g. '00 21 01')",
        "Back"
    };
    for (int i = 0; i < 6; i++) menuRow(items[i], i == menuCursor);
}

static void telnetShowDebugMenu(bool full)
{
    if (full) { cls(); tpl("=== Debug ===\r\n"); tp("\x1B[s"); }
    else      { tp("\x1B[u"); }
    telnetDrawDebugMenu();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] select\x1B[0m\r\n");
        menuItemCount = 6;
        setTelnetState(TS_DEBUG_MENU);
    }
}

static void telnetHandleDebugMenu(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowDebugMenu(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowDebugMenu(false); return; }
    if (key != "ENTER") return;

    int saved = menuCursor;
    menuCursor = 0;
    switch (saved) {
        case 0:
            stove.setDebug(!stove.isDebugEnabled());
            tpl("Serial debug: " + String(stove.isDebugEnabled() ? "ON" : "OFF"));
            telnetShowDebugMenu();
            break;
        case 1:
            cls();
            if (stoveDebugBuffer.length() == 0) tpl("(log is empty)");
            else {
                tpl("--- debug log ---");
                String log = stoveDebugBuffer;
                log.replace("\r\n", "\n");
                log.replace("\n", "\r\n");
                tp(log);
                tpl("--- end ---");
            }
            tpl("");
            tp("  \x1B[2m[any key] back\x1B[0m\r\n");
            setTelnetState(TS_DEBUG_LOG);
            break;
        case 2:
            stoveDebugBuffer = "";
            tpl("Log cleared.");
            telnetShowDebugMenu();
            break;
        case 3:
            tp("type address (hex, e.g. '00 1a'): ");
            setTelnetState(TS_DEBUG_RAW_READ);
            break;
        case 4:
            tp("type address value (hex, e.g. '00 21 01'): ");
            setTelnetState(TS_DEBUG_RAW_WRITE);
            break;
        case 5:
            telnetShowMainMenu();
            break;
    }
}

static void telnetHandleDebugRawRead(const String& input)
{
    unsigned int type = 0, address = 0;
    if (sscanf(input.c_str(), "%x %x", &type, &address) != 2) {
        tpl("Invalid format. Use hex: '00 1a'");
        menuCursor = 0;
        telnetShowDebugMenu();
        return;
    }
    bool prevDebug = stove.isDebugEnabled();
    stove.setDebug(true);
    stoveDebugBuffer = "";

    int val = stove.read((byte)type, (byte)address);

    { String _log = stoveDebugBuffer; _log.replace("\r\n", "\n"); _log.replace("\n", "\r\n"); tp(_log); }
    if (val >= 0) {
        char result[48];
        snprintf(result, sizeof(result), "Result: 0x%02X (%d)", (uint8_t)val, val);
        tpl(result);
    }
    stove.setDebug(prevDebug);
    menuCursor = 0;
    telnetShowDebugMenu();
}

static void telnetHandleDebugRawWrite(const String& input)
{
    unsigned int type = 0, address = 0, value = 0;
    if (sscanf(input.c_str(), "%x %x %x", &type, &address, &value) != 3) {
        tpl("Invalid format. Use hex: '00 21 01'");
        menuCursor = 0;
        telnetShowDebugMenu();
        return;
    }
    bool prevDebug = stove.isDebugEnabled();
    stove.setDebug(true);
    stoveDebugBuffer = "";

    bool ok = stove.write((byte)type, (byte)address, (byte)value);

    { String _log = stoveDebugBuffer; _log.replace("\r\n", "\n"); _log.replace("\n", "\r\n"); tp(_log); }
    tpl(ok ? "Result: SUCCESS" : "Result: FAILED");
    stove.setDebug(prevDebug);
    menuCursor = 0;
    telnetShowDebugMenu();
}

// ─────────────────────────────────────────────
// Menu: Live select (checkbox style)
// ─────────────────────────────────────────────

static void telnetDrawLiveSelect()
{
    const char* valueLabels[] = {
        "Stove state",
        "Temperatures",
        "Flame power",
        "Fan speeds",
        "Fume fan RPM",
        "Auger speed",
        "Pallet alarm",
        "Energy saving",
        "Remote control"
    };
    for (int i = 0; i < 9; i++) {
        bool checked = (liveValuesMask & (1 << i)) != 0;
        String label = String(checked ? "[x] " : "[ ] ") + valueLabels[i];
        menuRow(label, i == menuCursor);
    }
    menuRow("[x] All / None", 9  == menuCursor);
    menuRow("Start",          10 == menuCursor);
    menuRow("Back",           11 == menuCursor);
}

static void telnetShowLiveSelect(bool full)
{
    if (full) { cls(); tpl("=== Live Values ===\r\n"); tpl("  Select which values to monitor:\r\n"); tp("\x1B[s"); }
    else      { tp("\x1B[u"); }
    telnetDrawLiveSelect();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter/space] toggle   [enter] on Start\x1B[0m\r\n");
        menuItemCount = 12;
        setTelnetState(TS_LIVE_SELECT);
    }
}

static void telnetHandleLiveSelect(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowLiveSelect(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowLiveSelect(false); return; }

    if (key == "ENTER" || key == "SPACE") {
        if (menuCursor <= 8) {
            // Toggle individual value
            liveValuesMask ^= (1 << menuCursor);
            telnetShowLiveSelect(false);
            return;
        }
        if (menuCursor == 9) {
            // Toggle all / none
            liveValuesMask = (liveValuesMask == 0x1FF) ? 0 : 0x1FF;
            telnetShowLiveSelect(false);
            return;
        }
        if (menuCursor == 10) {
            // Start
            if (liveValuesMask == 0) {
                tpl("  \x1B[31mNo values selected.\x1B[0m");
                return;
            }
            tpl("\r\nLive mode started. Press any key to stop.\r\n");
            setTelnetState(TS_LIVE_RUNNING);
            lastLiveUpdate = 0;
            return;
        }
        if (menuCursor == 11) {
            // Back
            menuCursor = 0;
            telnetShowMainMenu();
            return;
        }
    }
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
        tpl("Temp room:    " + String(ambTemp)          + " C   setpoint: " + String(setPointTemp) + " C");
        tpl("Temp stove:   " + String(ambTempStove)     + " C   remote:   " + String(ambTempRemoteControl) + " C");
        tpl("Fume temp:    " + String(fumesTemp)        + " C");
    }
    if (liveValuesMask & (1 << 2)) tpl("Flame power:  " + String(flamePower));
    if (liveValuesMask & (1 << 3)) tpl("Room fan:     " + String(roomFanSpeed));
    if (liveValuesMask & (1 << 4)) tpl("Fume fan RPM: " + String(cachedFumeFanRPM));
    if (liveValuesMask & (1 << 5)) tpl("Auger speed:  " + String(cachedAugerSpeed));
    if (liveValuesMask & (1 << 6)) tpl("Low pallet:   " + String(cachedLowPallet ? "YES" : "no"));
    if (liveValuesMask & (1 << 7)) tpl("Energy save:  start=" + String(energySavingStart) + "  stop=" + String(energySavingStop));
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
// Menu: Config
// ─────────────────────────────────────────────

static void telnetDrawConfigMenu()
{
    String items[11] = {
        "Power state (on/off)             current: " + String(StoveIsOn ? "on" : "off"),
        "Flame power       [1-4]          current: " + String(flamePower),
        "Room fan speed    [0-3]          current: " + String(roomFanSpeed),
        "Energy save start [0,-1,-2,-3]   current: " + String(energySavingStart),
        "Energy save stop  [0,1,2,3]      current: " + String(energySavingStop),
        "Combustion quality               current: " + String(getCombustionQualityLabel(cachedCombustionQuality)),
        "Pellet quality                   current: " + String(getCombustionQualityLabel(cachedPelletQuality)),
        "Beep                             current: " + String(cachedBeepEnabled ? "enabled" : "disabled"),
        "Sync NTP time to stove",
        "View timers (PR1-PR6)",
        "Back"
    };
    for (int i = 0; i < 11; i++) menuRow(items[i], i == menuCursor);
}

static void telnetShowConfigMenu(bool full)
{
    if (full) { cls(); tpl("=== Configure ===\r\n"); tp("\x1B[s"); }
    else      { tp("\x1B[u"); }
    telnetDrawConfigMenu();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] select\x1B[0m\r\n");
        menuItemCount = 11;
        setTelnetState(TS_CONFIG_MENU);
    }
}

static void telnetHandleConfigMenu(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowConfigMenu(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowConfigMenu(false); return; }
    if (key != "ENTER") return;

    struct { const char* prompt; TelnetConfigField field; } opts[] = {
        { "Enter 'on' or 'off': ",                                CF_POWER              },
        { "Flame power [1-4]: ",                                  CF_FLAME_POWER        },
        { "Fan speed [0-3]: ",                                    CF_FAN_SPEED          },
        { "Energy saving start [0,-1,-2,-3]: ",                   CF_ENERGY_SAVE_START  },
        { "Energy saving stop [0,1,2,3]: ",                       CF_ENERGY_SAVE_STOP   },
        { "Combustion quality [+G,-3,-2,-1,0,+1,+2,+3]: ",       CF_COMBUSTION_QUALITY },
        { "Pellet quality [+G,-3,-2,-1,0,+1,+2,+3]: ",           CF_PELLET_QUALITY     },
        { "Beep [on/off]: ",                                      CF_BEEP               },
    };

    if (menuCursor <= 7) {
        tp(opts[menuCursor].prompt);
        telnetConfig = opts[menuCursor].field;
        setTelnetState(TS_CONFIG_INPUT);
        return;
    }
    if (menuCursor == 8) {
        setStoveDateTime();
        tpl("Time synced to NTP.");
        telnetShowConfigMenu();
        return;
    }
    if (menuCursor == 9) {
        menuCursor = 0;
        telnetShowTimersMenu();
        return;
    }
    if (menuCursor == 10) {
        menuCursor = 0;
        telnetShowMainMenu();
        return;
    }
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

        case CF_FLAME_POWER: {
            int v = input.toInt();
            if (v < 1 || v > 4) { tpl("Invalid. Must be 1-4."); tp("> "); return; }
            ok = setFlamePower(v);
            break;
        }

        case CF_FAN_SPEED: {
            int v = input.toInt();
            if (v < 0 || v > 3) { tpl("Invalid. Must be 0-3."); tp("> "); return; }
            ok = setRoomFanSpeed(v);
            break;
        }

        case CF_ENERGY_SAVE_START: {
            int v = input.toInt() * -1;
            if (v < 0 || v > 3) { tpl("Invalid. Must be 0,-1,-2,-3."); tp("> "); return; }
            ok = setEnergySavingStart(v);
            break;
        }

        case CF_ENERGY_SAVE_STOP: {
            int v = input.toInt();
            if (v < 0 || v > 3) { tpl("Invalid. Must be 0,1,2,3."); tp("> "); return; }
            ok = setEnergySavingStop(v);
            break;
        }

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

        case CF_BEEP: {
            String val = input;
            val.toLowerCase();
            if (val == "on" || val == "1" || val == "true") {
                ok = stove.write(eeprom, beepEnabledAddr, 1);
                if (ok) cachedBeepEnabled = true;
            } else if (val == "off" || val == "0" || val == "false") {
                ok = stove.write(eeprom, beepEnabledAddr, 0);
                if (ok) cachedBeepEnabled = false;
            } else {
                tpl("Invalid. Enter on or off.");
                tp("> "); return;
            }
            break;
        }

        default: break;
    }

    tpl(ok ? "Done." : "Failed (RS232 error).");
    telnetConfig = CF_NONE;
    telnetShowConfigMenu();
}

// ─────────────────────────────────────────────
// Timer helpers
// ─────────────────────────────────────────────

static String dowToString(int dow)
{
    const char* labels[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    const byte  bits[7]   = { 1, 2, 4, 8, 16, 32, 64 };
    String s = "";
    for (int d = 0; d < 7; d++) {
        if (dow & bits[d]) {
            if (s.length() > 0) s += " ";
            s += labels[d];
        }
    }
    return s.length() > 0 ? s : "none";
}

static byte parseDOWString(const String& input)
{
    if (input == "none" || input == "0" || input.length() == 0) return 0;
    String up = input;
    up.toUpperCase();
    byte mask = 0;
    if (up.indexOf("MON") >= 0) mask |= 1;
    if (up.indexOf("TUE") >= 0) mask |= 2;
    if (up.indexOf("WED") >= 0) mask |= 4;
    if (up.indexOf("THU") >= 0) mask |= 8;
    if (up.indexOf("FRI") >= 0) mask |= 16;
    if (up.indexOf("SAT") >= 0) mask |= 32;
    if (up.indexOf("SUN") >= 0) mask |= 64;
    return mask;
}

// ─────────────────────────────────────────────
// Menu: Timers list
// ─────────────────────────────────────────────

static void telnetDrawTimersMenu()
{
    const char* prNames[6] = { "PR1", "PR2", "PR3", "PR4", "PR5", "PR6" };
    for (int i = 0; i < 6; i++) {
        byte base    = timerBases[i];
        int  enabled = stove.read(eeprom, base + TIMER_ONOFF_OFFSET);
        String label;
        if (enabled <= 0) {
            label = String(prNames[i]) + ": disabled";
        } else {
            int onVal  = stove.read(eeprom, base + TIMER_ONTIME_OFFSET);
            int offVal = stove.read(eeprom, base + TIMER_OFFTIME_OFFSET);
            int dow    = stove.read(eeprom, base + TIMER_DOW_OFFSET);
            label = String(prNames[i]) + ": "
                + timerValueToTimeStr(onVal) + " -> " + timerValueToTimeStr(offVal)
                + "  [" + dowToString(dow) + "]";
        }
        menuRow(label, i == menuCursor);
    }
    menuRow("Back", 6 == menuCursor);
}

static void telnetShowTimersMenu(bool full)
{
    if (full) { cls(); tpl("=== Timers ===\r\n"); tp("\x1B[s"); }
    else      { tp("\x1B[u"); }
    telnetDrawTimersMenu();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] select\x1B[0m\r\n");
        menuItemCount = 7;
        setTelnetState(TS_TIMERS_MENU);
    }
}

static void telnetHandleTimersMenu(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowTimersMenu(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowTimersMenu(false); return; }
    if (key != "ENTER") return;

    if (menuCursor == 6) { menuCursor = 9; telnetShowConfigMenu(); return; }
    selectedTimerIdx = menuCursor;
    menuCursor = 0;
    telnetShowTimerEditMenu();
}

// ─────────────────────────────────────────────
// Menu: Timer edit
// ─────────────────────────────────────────────

static void telnetDrawTimerEditMenu()
{
    byte base    = timerBases[selectedTimerIdx];
    int  enabled = stove.read(eeprom, base + TIMER_ONOFF_OFFSET);
    int  onVal   = stove.read(eeprom, base + TIMER_ONTIME_OFFSET);
    int  offVal  = stove.read(eeprom, base + TIMER_OFFTIME_OFFSET);
    int  sp      = stove.read(eeprom, base + TIMER_SETPOINT_OFFSET);
    int  flame   = stove.read(eeprom, base + TIMER_FLAME_OFFSET);
    int  fan     = stove.read(eeprom, base + TIMER_FAN_OFFSET);
    int  dow     = stove.read(eeprom, base + TIMER_DOW_OFFSET);

    String items[8] = {
        "Enabled:     " + String(enabled > 0 ? "yes" : "no"),
        "On time:     " + timerValueToTimeStr(onVal)  + "  (HH:MM, 10-min steps)",
        "Off time:    " + timerValueToTimeStr(offVal) + "  (HH:MM, 10-min steps)",
        "Setpoint:    " + String(sp)    + " C",
        "Flame power: " + String(flame),
        "Fan speed:   " + String(fan),
        "Days:        " + dowToString(dow),
        "Back"
    };
    for (int i = 0; i < 8; i++) menuRow(items[i], i == menuCursor);
}

static void telnetShowTimerEditMenu(bool full)
{
    if (full) {
        const char* prNames[6] = { "PR1", "PR2", "PR3", "PR4", "PR5", "PR6" };
        cls();
        tpl("=== " + String(prNames[selectedTimerIdx]) + " ===\r\n");
        tp("\x1B[s");
    } else {
        tp("\x1B[u");
    }
    telnetDrawTimerEditMenu();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter] edit\x1B[0m\r\n");
        menuItemCount = 8;
        setTelnetState(TS_TIMER_EDIT_MENU);
    }
}

static void telnetHandleTimerEditMenu(const String& key)
{
    if (key == "UP")   { menuMoveUp();   telnetShowTimerEditMenu(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowTimerEditMenu(false); return; }
    if (key != "ENTER") return;

    if (menuCursor == 7) { menuCursor = selectedTimerIdx; telnetShowTimersMenu(); return; }

    timerEditReturnCursor = menuCursor;
    byte base = timerBases[selectedTimerIdx];

    switch (menuCursor) {
        case 0: { // Enabled — toggle directly
            int cur = stove.read(eeprom, base + TIMER_ONOFF_OFFSET);
            stove.write(eeprom, base + TIMER_ONOFF_OFFSET, cur > 0 ? 0 : 1);
            telnetShowTimerEditMenu(false);
            break;
        }
        case 1: { // On time stepper
            timerStepIsTime = true;
            timerStepOffset = TIMER_ONTIME_OFFSET;
            timerStepMin = 0; timerStepMax = 144;
            int v = stove.read(eeprom, base + TIMER_ONTIME_OFFSET);
            timerStepValue = (v < 0 || v > 144) ? 144 : v;
            menuCursor = 0;
            telnetShowTimerStep();
            break;
        }
        case 2: { // Off time stepper
            timerStepIsTime = true;
            timerStepOffset = TIMER_OFFTIME_OFFSET;
            timerStepMin = 0; timerStepMax = 144;
            int v = stove.read(eeprom, base + TIMER_OFFTIME_OFFSET);
            timerStepValue = (v < 0 || v > 144) ? 144 : v;
            menuCursor = 0;
            telnetShowTimerStep();
            break;
        }
        case 3: { // Setpoint — text input
            tp("Setpoint [7-30] C: ");
            currentTimerField = TF_SETPOINT;
            setTelnetState(TS_TIMER_EDIT_INPUT);
            break;
        }
        case 4: { // Flame power stepper
            timerStepIsTime = false;
            timerStepOffset = TIMER_FLAME_OFFSET;
            timerStepMin = 1; timerStepMax = 5;
            int v = stove.read(eeprom, base + TIMER_FLAME_OFFSET);
            timerStepValue = (v < 1 || v > 5) ? 1 : v;
            menuCursor = 0;
            telnetShowTimerStep();
            break;
        }
        case 5: { // Fan speed stepper
            timerStepIsTime = false;
            timerStepOffset = TIMER_FAN_OFFSET;
            timerStepMin = 0; timerStepMax = 3;
            int v = stove.read(eeprom, base + TIMER_FAN_OFFSET);
            timerStepValue = (v < 0 || v > 3) ? 0 : v;
            menuCursor = 0;
            telnetShowTimerStep();
            break;
        }
        case 6: { // DOW multi-select
            int d = stove.read(eeprom, base + TIMER_DOW_OFFSET);
            timerDowWorking = (d < 0) ? 0 : (byte)d;
            menuCursor = 0;
            telnetShowTimerDowSelect();
            break;
        }
    }
}

static void telnetHandleTimerEditInput(const String& input)
{
    byte base = timerBases[selectedTimerIdx];
    bool ok   = false;

    if (currentTimerField == TF_SETPOINT) {
        int v = input.toInt();
        if (v < 7 || v > 30) { tpl("Invalid. Must be 7-30."); tp("> "); return; }
        ok = stove.write(eeprom, base + TIMER_SETPOINT_OFFSET, v);
    }

    tpl(ok ? "Done." : "Failed (RS232 error).");
    currentTimerField = TF_NONE;
    menuCursor = timerEditReturnCursor;
    telnetShowTimerEditMenu();
}

// ─────────────────────────────────────────────
// Timer: value stepper (on/off time, flame, fan)
// ─────────────────────────────────────────────

static const char* timerStepFieldName() {
    switch (timerStepOffset) {
        case TIMER_ONTIME_OFFSET:  return "On Time";
        case TIMER_OFFTIME_OFFSET: return "Off Time";
        case TIMER_FLAME_OFFSET:   return "Flame Power";
        case TIMER_FAN_OFFSET:     return "Fan Speed";
        default:                   return "Value";
    }
}

static void telnetShowTimerStep(bool full) {
    if (full) {
        const char* prNames[6] = { "PR1", "PR2", "PR3", "PR4", "PR5", "PR6" };
        cls();
        tpl("=== " + String(prNames[selectedTimerIdx]) + " - " + timerStepFieldName() + " ===\r\n");
        tp("\x1B[s");
    } else {
        tp("\x1B[u");
    }
    String display = timerStepIsTime ? timerValueToTimeStr(timerStepValue) : String(timerStepValue);
    menuRow("^  " + display + "  v", true);
    if (full) {
        tpl("");
        if (timerStepIsTime)
            tp("  \x1B[2m[UP] +10min  [DOWN] -10min  [ENTER] set\x1B[0m\r\n");
        else
            tp("  \x1B[2m[UP] +1  [DOWN] -1  [ENTER] set\x1B[0m\r\n");
        setTelnetState(TS_TIMER_STEP);
    }
}

static void telnetHandleTimerStep(const String& key) {
    if (key == "UP") {
        if (timerStepIsTime)
            timerStepValue = (timerStepValue >= timerStepMax) ? timerStepMin : timerStepValue + 1;
        else
            if (timerStepValue < timerStepMax) timerStepValue++;
        telnetShowTimerStep(false);
        return;
    }
    if (key == "DOWN") {
        if (timerStepIsTime)
            timerStepValue = (timerStepValue <= timerStepMin) ? timerStepMax : timerStepValue - 1;
        else
            if (timerStepValue > timerStepMin) timerStepValue--;
        telnetShowTimerStep(false);
        return;
    }
    if (key == "ENTER") {
        byte base = timerBases[selectedTimerIdx];
        bool ok = stove.write(eeprom, base + timerStepOffset, (byte)timerStepValue);
        tpl(ok ? "\r\nDone." : "\r\nFailed (RS232 error).");
        menuCursor = timerEditReturnCursor;
        telnetShowTimerEditMenu();
    }
}

// ─────────────────────────────────────────────
// Timer: day-of-week multi-select
// ─────────────────────────────────────────────

static void telnetDrawTimerDowSelect() {
    const byte  bits[7] = { 1, 2, 4, 8, 16, 32, 64 };
    const char* days[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    for (int i = 0; i < 7; i++) {
        bool checked = (timerDowWorking & bits[i]) != 0;
        menuRow(String(checked ? "[x] " : "[ ] ") + days[i], i == menuCursor);
    }
    bool allSet = (timerDowWorking & 0x7F) == 0x7F;
    menuRow(String(allSet ? "[x] " : "[ ] ") + "All / None", 7 == menuCursor);
    menuRow("Done",   8 == menuCursor);
    menuRow("Cancel", 9 == menuCursor);
}

static void telnetShowTimerDowSelect(bool full) {
    if (full) {
        const char* prNames[6] = { "PR1", "PR2", "PR3", "PR4", "PR5", "PR6" };
        cls();
        tpl("=== " + String(prNames[selectedTimerIdx]) + " - Days ===\r\n");
        tp("\x1B[s");
    } else {
        tp("\x1B[u");
    }
    telnetDrawTimerDowSelect();
    if (full) {
        tpl("");
        tp("  \x1B[2m[arrows] navigate   [enter/space] toggle   [enter] on Done/Cancel\x1B[0m\r\n");
        menuItemCount = 10;
        setTelnetState(TS_TIMER_DOW_SELECT);
    }
}

static void telnetHandleTimerDowSelect(const String& key) {
    const byte bits[7] = { 1, 2, 4, 8, 16, 32, 64 };

    if (key == "UP")   { menuMoveUp();   telnetShowTimerDowSelect(false); return; }
    if (key == "DOWN") { menuMoveDown(); telnetShowTimerDowSelect(false); return; }

    if (key == "ENTER" || key == "SPACE") {
        if (menuCursor <= 6) {
            timerDowWorking ^= bits[menuCursor];
            telnetShowTimerDowSelect(false);
            return;
        }
        if (menuCursor == 7) {
            timerDowWorking = ((timerDowWorking & 0x7F) == 0x7F) ? 0 : 0x7F;
            telnetShowTimerDowSelect(false);
            return;
        }
        if (menuCursor == 8) { // Done — save
            byte base = timerBases[selectedTimerIdx];
            bool ok = stove.write(eeprom, base + TIMER_DOW_OFFSET, timerDowWorking);
            tpl(ok ? "\r\nDone." : "\r\nFailed (RS232 error).");
            menuCursor = timerEditReturnCursor;
            telnetShowTimerEditMenu();
            return;
        }
        if (menuCursor == 9) { // Cancel — discard
            menuCursor = timerEditReturnCursor;
            telnetShowTimerEditMenu();
            return;
        }
    }
}

// ─────────────────────────────────────────────
// Escape sequence + key dispatcher
// ─────────────────────────────────────────────

static void menuDispatch(const String& key)
{
    switch (telnetState) {
        case TS_MAIN_MENU:       telnetHandleMainMenu(key);      break;
        case TS_CONFIG_MENU:     telnetHandleConfigMenu(key);    break;
        case TS_DEBUG_MENU:      telnetHandleDebugMenu(key);     break;
        case TS_WIFI_MENU:       telnetHandleWifiMenu(key);      break;
        case TS_WIFI_IP_MENU:    telnetHandleWifiIpMenu(key);    break;
        case TS_WIFI_CONFIRM:    telnetHandleWifiConfirm(key);   break;
        case TS_WIFI_INFO:       menuCursor = 0; telnetShowWifiMenu();  break;
        case TS_DEBUG_LOG:       menuCursor = 0; telnetShowDebugMenu(); break;
        case TS_LIVE_SELECT:     telnetHandleLiveSelect(key);    break;
        case TS_TIMERS_MENU:      telnetHandleTimersMenu(key);      break;
        case TS_TIMER_EDIT_MENU:  telnetHandleTimerEditMenu(key);  break;
        case TS_TIMER_STEP:       telnetHandleTimerStep(key);      break;
        case TS_TIMER_DOW_SELECT: telnetHandleTimerDowSelect(key); break;
        case TS_LIVE_RUNNING:
            tpl("\r\nLive mode stopped.");
            menuCursor = 0;
            telnetShowMainMenu();
            break;
        default: break;
    }
}

// ─────────────────────────────────────────────
// Public interface — called from main.cpp
// ─────────────────────────────────────────────

void telnetHandleInput(const String& rawInput)
{
    // ── Char-mode states: parse arrow keys and control chars ──────
    if (isCharModeState(telnetState)) {
        for (int i = 0; i < (int)rawInput.length(); i++) {
            uint8_t c = (uint8_t)rawInput[i];

            // Accumulate ESC sequence (ESC [ A/B/C/D)
            if (escState == 0 && c == 0x1B) { escState = 1; continue; }
            if (escState == 1 && c == '[')  { escState = 2; continue; }
            if (escState == 2) {
                escState = 0;
                if      (c == 'A') menuDispatch("UP");
                else if (c == 'B') menuDispatch("DOWN");
                // ignore left/right
                return;
            }
            escState = 0;

            if (c == '\r' || c == '\n') { menuDispatch("ENTER"); return; }
            if (c == ' ')               { menuDispatch("SPACE"); return; }
        }
        return;
    }

    // ── Line-mode states: text input ──────────────────────────────
    String input = rawInput;
    input.trim();
    if (input.length() == 0) return;

    switch (telnetState) {
        case TS_CONFIG_INPUT:        telnetHandleConfigInput(input);      break;
        case TS_DEBUG_RAW_READ:      telnetHandleDebugRawRead(input);     break;
        case TS_DEBUG_RAW_WRITE:     telnetHandleDebugRawWrite(input);    break;
        case TS_WIFI_HOSTNAME_INPUT: telnetHandleWifiHostname(input);     break;
        case TS_WIFI_STATIC_IP:      telnetHandleWifiStaticIP(input);     break;
        case TS_WIFI_STATIC_NETMASK: telnetHandleWifiStaticNetmask(input);break;
        case TS_WIFI_STATIC_GW:      telnetHandleWifiStaticGW(input);     break;
        case TS_WIFI_STATIC_DNS:     telnetHandleWifiStaticDNS(input);    break;
        case TS_TIMER_EDIT_INPUT:    telnetHandleTimerEditInput(input);   break;
        default:                     menuCursor = 0; telnetShowMainMenu(); break;
    }
}

void telnetOnConnect(const String& ip)
{
    Serial.println("- Telnet: " + ip + " connected");
    escState   = 0;
    menuCursor = 0;

    // Negotiate character-at-a-time mode immediately on connect
    const uint8_t seq[] = {
        0xFF, 0xFB, 0x01,   // IAC WILL ECHO
        0xFF, 0xFB, 0x03,   // IAC WILL SGA
        0xFF, 0xFD, 0x03    // IAC DO  SGA
    };
    telnet.write(seq, sizeof(seq));

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
