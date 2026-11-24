#pragma once
// Pure logic of the OpenPelletBridge firmware: encodings and decisions that do not
// touch the hardware. No Arduino dependencies, so it is used by the firmware and
// by the native unit tests (test/, `pio test -e native`).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>

namespace pc {

// ── Chrono timer times ─────────────────────────────────────────────────────
// The stove stores a timer time as the number of 10 minute slots since 00:00
// (0 - 143); 144 means "off".
static const int TIMER_OFF = 144;

// "HH:MM" on a 10 minute boundary or "off" -> 0 - 144; -1 when invalid.
inline int timerValueFromStr(const char* str)
{
    if (!str) return -1;
    if (strcmp(str, "off") == 0) return TIMER_OFF;
    int h = 0, m = 0;
    char extra = 0;
    if (sscanf(str, "%d:%d%c", &h, &m, &extra) != 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    int totalMins = h * 60 + m;
    if (totalMins % 10 != 0) return -1;
    return totalMins / 10;
}

// 0 - 143 -> "HH:MM", anything else -> "off". out must hold 6 bytes.
inline const char* timerValueToStr(int val, char out[6])
{
    if (val < 0 || val >= TIMER_OFF) { strcpy(out, "off"); return out; }
    int totalMinutes = val * 10;
    snprintf(out, 6, "%02d:%02d", totalMinutes / 60, totalMinutes % 60);
    return out;
}

// ── Clock ──────────────────────────────────────────────────────────────────
// The stove clock in EEPROM 0xF8 - 0xFD is written as BCD (16 -> 0x16).
inline uint8_t toBcd(int number) { return (uint8_t)((number / 10) * 0x10 + (number % 10)); }
inline int fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

// The stove clock is read every few minutes and extrapolated in between.
// Works across a millis() wrap-around: millis() is 32 bit, so the difference is
// taken in uint32_t (also on a 64-bit host). base 0 = unknown.
inline long extrapolateTime(long base, uint32_t readMs, uint32_t nowMs)
{
    if (!base) return 0;
    return base + (long)((uint32_t)(nowMs - readMs) / 1000U);
}

// Plausibility of the 6 stove clock registers (RAM 0x7A - 0x80, year 2 digits).
inline bool validStoveClock(int sec, int min, int hour, int day, int month, int year)
{
    return sec >= 0 && sec <= 59 && min >= 0 && min <= 59 && hour >= 0 && hour <= 23 &&
           day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 0 && year <= 99;
}

// ── Fans ───────────────────────────────────────────────────────────────────
// Fume fan speed (RAM 0x37 / 0x36, EEPROM 0x10 - 0x15): rpm = (raw + 25) * 10.
// 0 = stopped; negative = read error (passed through).
inline int fumeFanRpm(int raw) { return raw > 0 ? (raw + 25) * 10 : raw; }

// Room fan: the power (RAM 0x0A) depends on the selected speed and the actual
// flame power; 0 = off. -1 = not read yet.
inline int roomFanActual(int power, int selectedSpeed)
{
    if (power < 0) return -1;
    return power == 0 ? 0 : selectedSpeed;
}
inline const char* roomFanMode(int power)
{
    if (power < 0) return nullptr;
    return power == 0 ? "off" : "on";
}
// 1 running, 0 stopped, -1 unknown. Falls back to RAM 0xD3 before 0x0A is read.
inline int roomFanIsRunning(int power, int flagD3)
{
    if (power >= 0) return power > 0 ? 1 : 0;
    return flagD3 < 0 ? -1 : (flagD3 != 0 ? 1 : 0);
}

// ── Pellet reserve ─────────────────────────────────────────────────────────
// RAM 0xA4 is not an on / off sensor but a counter 0 - 5 the stove keeps itself:
//   - no pellets at the level sensor: straight to 5 (the reserve countdown in RAM 0x89 runs)
//   - pellets seen: one step down per reading, about every 37 s while burning (23/11/2026:
//     refill at 09:12:29, 5 -> 4 -> 3 -> 2 -> 1 -> 0 at 09:16:12; on 20/11 after the
//     switch-off it went 5 -> 3 -> 0 within seconds). 0 = the stove has confirmed the pellets.
//   - a single 4 between two 5's: pellets sliding past the sensor, not a refill (09:01:54)
// So 1 - 5: the stove has not confirmed pellets yet; 1 - 3: a refill is being confirmed
// (one 4 can still be a flicker).
inline bool reserveSensorLow(int sensor) { return sensor >= 1 && sensor <= 5; }
inline bool reserveRefilling(int sensor) { return sensor >= 1 && sensor <= 3; }
static const int RESERVE_STEP_S = 37;   // one step of RAM 0xA4 while burning (23/11/2026)
// Alarm (the remote shows PELLET RESERVE) when the sensor (RAM 0xA4) is low and the 20 min
// countdown (RAM 0x89) has run down (confirmed 17/11 and 20/11/2026; <= 1 is one minute early).
// Not while a refill is being confirmed (1 - 3).
inline int pelletsLow(int sensor, int counter)
{
    return reserveSensorLow(sensor) && !reserveRefilling(sensor) && counter >= 0 && counter <= 1 ? 1 : 0;
}

// ── Errors ─────────────────────────────────────────────────────────────────
// RAM 0x33 also shows short pre-warnings that are not real errors and never reach
// the stove's error memory: E12 for 1 - 5 s at brazier cleaning (16/11/2026) and E1
// for ~45 s during the start-up check (state 1, 17/11/2026). An error only counts
// once it stayed confirmMs (stable states), or confirmTransitionalMs while the stove
// is in a transitional phase (start-up, pellet loading, ignition, brazier cleaning,
// shut-down), or at once in an alarm state (8, 9, 10). A cleared error is reported
// right away.
inline bool transitionalState(int s) { return s == 1 || s == 2 || s == 3 || s == 5 || s == 6; }
inline bool alarmState(int s) { return s == 8 || s == 9 || s == 10; }
struct ErrorConfirm
{
    int pending = 0;
    uint32_t since = 0;
    int confirmed = 0;
    int update(int raw, int stoveState, uint32_t nowMs, uint32_t confirmMs,      // millis() is 32 bit
               uint32_t confirmTransitionalMs = 0)
    {
        if (raw != pending) { pending = raw; since = nowMs; }
        uint32_t need = transitionalState(stoveState) && confirmTransitionalMs > confirmMs ? confirmTransitionalMs : confirmMs;
        if (raw == 0) confirmed = 0;
        else if (alarmState(stoveState) || (uint32_t)(nowMs - since) >= need) confirmed = raw;
        return confirmed;
    }
};

// ── Combustion / pellet quality ────────────────────────────────────────────
inline const char* combustionQualityLabel(int val)
{
    switch (val) {
        case 76: return "+G";
        case 2: return "-3"; case 3: return "-2"; case 4: return "-1"; case 5: return "0";
        case 6: return "1";  case 7: return "2";  case 8: return "3";
        default: return nullptr;
    }
}
inline const char* pelletQualityLabel(int val)
{
    static const char* L[] = { "-5", "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5" };
    return val >= 0 && val <= 10 ? L[val] : nullptr;
}
// Accepts "+G" / "g" / "+1" / " 1 " etc. Returns -1 when unknown.
inline void normaliseLabel(const char* in, char out[8])
{
    size_t n = 0;
    while (*in && isspace((unsigned char)*in)) in++;
    if (*in == '+') in++;
    while (*in && n < 7) out[n++] = (char)toupper((unsigned char)*in++);
    while (n && isspace((unsigned char)out[n - 1])) n--;
    out[n] = 0;
}
inline int combustionQualityFromLabel(const char* raw)
{
    char l[8]; normaliseLabel(raw ? raw : "", l);
    if (strcmp(l, "G") == 0) return 76;
    for (int v = 2; v <= 8; v++) if (strcmp(combustionQualityLabel(v), l) == 0) return v;
    return -1;
}
inline int pelletQualityFromLabel(const char* raw)
{
    char l[8]; normaliseLabel(raw ? raw : "", l);
    for (int v = 0; v <= 10; v++) if (strcmp(pelletQualityLabel(v), l) == 0) return v;
    return -1;
}

// ── Address lists (/api/dump, /api/keywatch) ───────────────────────────────
// "0x21,52, 0x34" -> out[addr] = true. Empty string = all 256. Invalid tokens
// are skipped. Returns the number of addresses selected.
inline int parseAddressList(const char* param, bool out[256])
{
    if (!param || !*param) { for (int i = 0; i < 256; i++) out[i] = true; return 256; }
    for (int i = 0; i < 256; i++) out[i] = false;
    int count = 0;
    const char* p = param;
    while (*p) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ',') p++;
        char tok[16]; size_t len = (size_t)(p - start);
        while (len && isspace((unsigned char)start[len - 1])) len--;
        if (len == 0 || len >= sizeof(tok)) continue;
        memcpy(tok, start, len); tok[len] = 0;
        char* end = nullptr;
        long a = (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) ? strtol(tok + 2, &end, 16) : strtol(tok, &end, 10);
        if (end == tok || (end && *end) || a < 0 || a > 255) continue;
        if (!out[a]) { out[a] = true; count++; }
    }
    return count;
}

// ── MQTT command payloads ──────────────────────────────────────────────────
// ON / OFF style payloads: "ON", "on", "1", "true" -> 1; "OFF", "0", "false" -> 0;
// anything else -1. Home Assistant sends ON / OFF, Loxone usually 1 / 0.
inline int parseOnOff(const char* s)
{
    if (!s) return -1;
    char l[8]; normaliseLabel(s, l);
    if (!strcmp(l, "ON") || !strcmp(l, "1") || !strcmp(l, "TRUE")) return 1;
    if (!strcmp(l, "OFF") || !strcmp(l, "0") || !strcmp(l, "FALSE")) return 0;
    return -1;
}
// Integer in [lo, hi]; also accepts "21.0" (whole numbers only). false when invalid.
inline bool parseIntRange(const char* s, int lo, int hi, int& out)
{
    if (!s) return false;
    while (isspace((unsigned char)*s)) s++;
    char* end = nullptr;
    double d = strtod(s, &end);
    if (end == s) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return false;
    long v = (long)d;
    if ((double)v != d || v < lo || v > hi) return false;
    out = (int)v;
    return true;
}
// Room fan speed: 0 - 3, or "off" / "OFF" for 0 (Home Assistant select / fan).
inline bool parseFanSpeed(const char* s, int& out)
{
    char l[8]; normaliseLabel(s ? s : "", l);
    if (!strcmp(l, "OFF") || !strcmp(l, "UIT")) { out = 0; return true; }
    return parseIntRange(s, 0, 3, out);
}

// ── Network settings ───────────────────────────────────────────────────────
// Hostname (RFC 1123 label): 1 - 32 chars, letters, digits and '-', not starting
// or ending with '-'.
inline bool validHostname(const char* s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 1 || n > 32 || s[0] == '-' || s[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)s[i]) && s[i] != '-') return false;
    return true;
}
// "192.168.1.10" -> host order uint32 (a << 24 | ...). false when invalid.
inline bool ipv4FromStr(const char* s, uint32_t& out)
{
    if (!s) return false;
    unsigned a, b, c, d; char extra;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}
// Contiguous ones from the top (255.255.255.0 yes, 255.0.255.0 no), not 0.0.0.0.
inline bool validNetmask(uint32_t m)
{
    if (m == 0) return false;
    uint32_t inv = ~m;
    return (inv & (inv + 1)) == 0;
}
// A static configuration that can work: valid addresses, the address is not the
// network or broadcast address, and the gateway is in the same subnet. Returns an
// error text, or nullptr when fine.
inline const char* checkStaticIp(const char* ip, const char* netmask, const char* gateway, const char* dns)
{
    uint32_t i, m, g, d;
    if (!ipv4FromStr(ip, i)) return "invalid IP address";
    if (!ipv4FromStr(netmask, m) || !validNetmask(m)) return "invalid netmask";
    if (!ipv4FromStr(gateway, g)) return "invalid gateway";
    if (!ipv4FromStr(dns, d)) return "invalid DNS server";
    if ((i & ~m) == 0 || (i & ~m) == ~m) return "IP address is the network or broadcast address";
    if ((i & m) != (g & m)) return "gateway is not in the same subnet as the IP address";
    if (i == g) return "IP address equals the gateway";
    return nullptr;
}
// SSID: 1 - 32 bytes. Password: empty (open network) or 8 - 63 characters (WPA2).
inline bool validSsid(const char* s) { return s && strlen(s) >= 1 && strlen(s) <= 32; }
inline bool validWifiPassword(const char* s)
{
    if (!s) return false;
    size_t n = strlen(s);
    return n == 0 || (n >= 8 && n <= 63);
}
// WiFi signal quality in % from RSSI (dBm), the usual linear -100 .. -50 mapping.
inline int rssiQuality(int rssi)
{
    if (rssi >= 0) return 0;          // 0 = not connected / unknown
    if (rssi <= -100) return 0;
    if (rssi >= -50) return 100;
    return 2 * (rssi + 100);
}

// ---- Firmware versions ---------------------------------------------------------
// Release tags are "vMAJOR.MINOR[.PATCH]". A local build is versioned by
// `git describe --tags --always --dirty`: "v1.2.0" on the tag itself,
// "v1.2.0-3-gabc1234[-dirty]" three commits later, or a bare commit hash when the
// repository has no tags yet (no version then).
struct Version {
    int major = 0, minor = 0, patch = 0;
    int ahead = 0;          // commits after the tag (git describe)
    bool dirty = false;     // uncommitted changes in the build
};
inline bool parseVersion(const char* s, Version& v)
{
    v = Version();
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    if (!isdigit((unsigned char)*s)) return false;
    char* end;
    v.major = (int)strtol(s, &end, 10);
    if (*end != '.' || !isdigit((unsigned char)end[1])) return false;   // a dot is required: "1234abc" is a hash
    v.minor = (int)strtol(end + 1, &end, 10);
    if (*end == '.' && isdigit((unsigned char)end[1])) v.patch = (int)strtol(end + 1, &end, 10);
    // git describe suffix: -<n>-g<hash>
    if (*end == '-' && isdigit((unsigned char)end[1])) {
        char* e2;
        long n = strtol(end + 1, &e2, 10);
        if (e2[0] == '-' && e2[1] == 'g') v.ahead = (int)n;
    }
    v.dirty = strstr(s, "-dirty") != nullptr;
    return true;
}
// <0 when a is older than b, 0 same release, >0 newer. Commits after a tag make a
// build newer than that tag.
inline int compareVersion(const Version& a, const Version& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.ahead != b.ahead) return a.ahead < b.ahead ? -1 : 1;
    return 0;
}
// Is release `latest` newer than the running firmware `current`? False when
// either has no version (a development build is never offered an "update").
inline bool updateAvailable(const char* current, const char* latest)
{
    Version c, l;
    if (!parseVersion(current, c) || !parseVersion(latest, l)) return false;
    return compareVersion(l, c) > 0;
}

// ---- Stove states and error codes ------------------------------------------------
// RAM 0x21 stove state -> English name, and whether the stove counts as "on"
// (power switch). Unlisted values give "Unknown" / off.
inline const char* stoveStateName(int state)
{
    switch (state) {
    case -1: return "No answer (serial link down)";
    case 0:  return "Off";
    case 1:  return "Starting";
    case 2:  return "Pellet loading";
    case 3:  return "Ignition";
    case 4:  return "Working";
    case 5:  return "Brazier cleaning";    // periodic cleaning while working, still running
    case 6:  return "Final cleaning";
    case 7:  return "Standby";
    case 8:  return "Alarm State";
    case 9:  return "Ignition failure";
    case 10: return "Alarm";
    default: return "Unknown";
    }
}
inline bool stoveStateIsOn(int state)
{
    // 8 (alarm): doStoveOff() still sends a shutdown (6) in this state, so treat it as on
    return (state >= 1 && state <= 5) || state == 7 || state == 8;
}

// Error memory / current error (RAM 0x6E): value -> display code and description.
struct ErrorInfo { int id; const char* code; const char* desc; };
inline const ErrorInfo* errorTable(int& count)
{
    static const ErrorInfo t[] = {
        {   0, "---",        "No Error" },
        {   1, "E8",         "Fume sensor error" },
        {   2, "E4",         "Fume temperature too high" },
        {   4, "E9",         "Ignition temperature not reached within limit" },
        {   8, "E7",         "Fume temperature too low" },
        {  16, "E3",         "Temperature inside stove too high" },
        {  32, "E1",         "Pellet loading door is open for too long or air pressure inside stove is wrong" },
        {  64, "E2",         "Pressure sensor error during ignition" },
        { 128, "E6",         "Auger error - Too many pellets dispatched" },
        { 129, "E12",        "Fume fan error" },
        { 130, "E11",        "Unknown error - Not described in manual" },
        { 132, "E19",        "External contact N.PEL / Pellet is active" },
        { 136, "E13",        "Unknown error - Not described in manual" },
        { 255, "PowerError", "Display shows power error" },
    };
    count = (int)(sizeof(t) / sizeof(t[0]));
    return t;
}
inline const ErrorInfo* errorInfo(int id)
{
    int n; const ErrorInfo* t = errorTable(n);
    for (int i = 0; i < n; i++) if (t[i].id == id) return &t[i];
    return nullptr;
}
inline const char* errorCode(int id) { const ErrorInfo* e = errorInfo(id); return e ? e->code : "---"; }
inline const char* errorDesc(int id) { const ErrorInfo* e = errorInfo(id); return e ? e->desc : "Unknown error"; }

// ---- Firmware release asset ----------------------------------------------------
// Which release asset to download: the current name wins, the name before the
// rename (PalletHeater, up to v1.0.0) is a fallback. 0 = not a firmware image.
#define PC_FW_ASSET     "openpelletbridge-firmware.bin"
#define PC_FW_ASSET_OLD "palletheater-firmware.bin"
inline int firmwareAssetRank(const char* name)
{
    if (!name) return 0;
    if (strcmp(name, PC_FW_ASSET) == 0) return 2;
    if (strcmp(name, PC_FW_ASSET_OLD) == 0) return 1;
    return 0;
}

// ---- Ring buffers for the live views (log, bus monitor) ---------------------------
// Readers keep a cursor = the total number of items written so far (`next` of the
// previous read). A cursor older than the buffer reports `lost`; a cursor from
// before a restart (larger than the total) starts again at the oldest item.
template <size_t N>
struct TextRing {
    char buf[N];
    uint32_t total = 0;                       // bytes ever written
    void write(const char* s, size_t n) {
        for (size_t i = 0; i < n; i++) buf[(total + i) % N] = s[i];
        total += (uint32_t)n;
    }
    uint32_t oldest() const { return total > N ? total - (uint32_t)N : 0; }
    // Copies at most `max` bytes from cursor `since` into out. Returns the count;
    // `from` = where the copy really started, `lost` = bytes that were overwritten.
    size_t read(uint32_t since, char* out, size_t max, uint32_t& from, bool& lost) const {
        uint32_t lo = oldest();
        lost = since < lo && since != 0;
        from = (since < lo || since > total) ? lo : since;
        size_t n = total - from; if (n > max) n = max;
        for (size_t i = 0; i < n; i++) out[i] = buf[(from + i) % N];
        return n;
    }
};

template <typename T, size_t N>
struct Ring {
    T items[N];
    uint32_t total = 0;                       // items ever pushed
    void push(const T& v) { items[total % N] = v; total++; }
    uint32_t oldest() const { return total > N ? total - (uint32_t)N : 0; }
    // Same cursor rules as TextRing. Item k (k >= from) is at(k).
    size_t read(uint32_t since, size_t max, uint32_t& from, bool& lost) const {
        uint32_t lo = oldest();
        lost = since < lo && since != 0;
        from = (since < lo || since > total) ? lo : since;
        size_t n = total - from; return n > max ? max : n;
    }
    const T& at(uint32_t k) const { return items[k % N]; }
};

// ---- Hourly history (7 days) -------------------------------------------------------
// The 2-minute samples are averaged per clock hour. Unknown inputs are negative
// (-1) and left out of their average.
struct HourInput {
    uint32_t t;
    int room10, stove10, remote10;            // °C x10
    int setpoint, fume, flame, flameActual, fan, fanActual, state;
    int auger10, fumeRpm;                     // -1 = unknown
};
struct HourRecord {                           // stored on SPIFFS: keep the layout
    uint32_t t;                               // start of the hour (unix, UTC)
    int16_t  room10, stove10, remote10;
    uint8_t  setpoint, fume, fumeMax;
    uint8_t  burnPct;                         // % of the samples in a heating state (1 - 5)
    uint8_t  flame10, flameAct10, fan10, fanAct10;   // averages x10, 0xFF = unknown
    int8_t   state;                           // most frequent state
    uint8_t  n;                               // number of samples
    int16_t  auger10;                         // auger on-time per cycle x10 (s), -1 = unknown
    uint16_t fumeRpm;                         // 0xFFFF = unknown
};
inline bool heatingState(int s) { return s >= 1 && s <= 5; }
// Auger value (RAM 0x0D / 10 = on-time in seconds per cycle of ~12 s, measured 20/11/2026) as
// the share of the time the auger runs, 0 - 100 % (20.0 while pre-loading = running all the time)
inline int augerPercent(float onTime) { if (onTime <= 0) return 0; int p = (int)(onTime / 12.0f * 100.0f + 0.5f); return p > 100 ? 100 : p; }
struct HourAcc {
    uint32_t hour = 0; int n = 0;
    long room = 0, stove = 0, remote = 0, fume = 0, flame = 0, fan = 0;
    long flameAct = 0, fanAct = 0, auger = 0, rpm = 0;
    int nFlameAct = 0, nFanAct = 0, nAuger = 0, nRpm = 0, burning = 0, fumeMax = 0, setpoint = 0;
    int states[12] = {0};                     // index state + 1 (-1 .. 10)
    static uint32_t hourOf(uint32_t t) { return t - t % 3600; }
    bool empty() const { return n == 0; }
    void reset() { *this = HourAcc(); }
    void add(const HourInput& s) {
        if (n == 0) hour = hourOf(s.t);
        n++;
        room += s.room10; stove += s.stove10; remote += s.remote10;
        fume += s.fume; if (s.fume > fumeMax) fumeMax = s.fume;
        flame += s.flame; fan += s.fan; setpoint = s.setpoint;   // set point: last value of the hour
        if (s.flameActual >= 0) { flameAct += s.flameActual; nFlameAct++; }
        if (s.fanActual >= 0)   { fanAct += s.fanActual; nFanAct++; }
        if (s.auger10 >= 0)     { auger += s.auger10; nAuger++; }
        if (s.fumeRpm >= 0)     { rpm += s.fumeRpm; nRpm++; }
        if (heatingState(s.state)) burning++;
        if (s.state >= -1 && s.state <= 10) states[s.state + 1]++;
    }
    HourRecord result() const {
        HourRecord r = {};
        r.t = hour; r.n = (uint8_t)(n > 255 ? 255 : n);
        if (!n) return r;
        auto avg = [](long sum, int cnt) { return (long)((sum >= 0 ? sum * 2 + cnt : sum * 2 - cnt) / (2 * cnt)); };   // rounded
        r.room10 = (int16_t)avg(room, n); r.stove10 = (int16_t)avg(stove, n); r.remote10 = (int16_t)avg(remote, n);
        r.setpoint = (uint8_t)setpoint; r.fume = (uint8_t)avg(fume, n); r.fumeMax = (uint8_t)fumeMax;
        r.burnPct = (uint8_t)((burning * 100 + n / 2) / n);
        r.flame10 = (uint8_t)avg(flame * 10, n); r.fan10 = (uint8_t)avg(fan * 10, n);
        r.flameAct10 = nFlameAct ? (uint8_t)avg(flameAct * 10, nFlameAct) : 0xFF;
        r.fanAct10   = nFanAct   ? (uint8_t)avg(fanAct * 10, nFanAct)     : 0xFF;
        r.auger10 = nAuger ? (int16_t)avg(auger, nAuger) : -1;
        r.fumeRpm = nRpm ? (uint16_t)avg(rpm, nRpm) : 0xFFFF;
        int best = 0; for (int i = 1; i < 12; i++) if (states[i] > states[best]) best = i;
        r.state = (int8_t)(best - 1);
        return r;
    }
};

// ── Pellet consumption ───────────────────────────────────────────────────────
// The level sensor (RAM 0xA4 = 5) always trips at the same height in the hopper.
// Between two trips exactly the pellets added in between were burned. The auger
// value (RAM 0x0D, fixed per flame power: 3.0 / 4.6 / 6.2 / 7.8) is the on-time in
// seconds of the auger motor per cycle of about 12 s (measured 20/11/2026), so the
// value times the time is proportional to the motor's running time: "auger units";
// kg per unit = kg added / units between two trips. Values above the cycle (20.0 while
// pre-loading at the start) mean the motor runs all the time: counted as the cycle.
//   - a trip counts once the sensor stayed low for LOW_CONFIRM_S (it flickers when
//     the level is right at the sensor); the cycle boundary is where it first went low
//   - kg per unit weighs recent cycles more (RECENT_WEIGHT per older cycle): a new
//     pellet brand or a changed refill amount shows up after one or two cycles
//   - at every trip the kg the estimate still predicted is kept (predKg): > 0 = the
//     alarm came earlier than calculated, < 0 = later; the next estimate already uses
//     this cycle, so it corrects itself
//   - a refill is detected when the sensor is back to normal for REFILL_CONFIRM_S
//     after a confirmed trip, with the default amount (refillKg); a refill without a
//     trip (topped up early) can only be entered by hand (addKg)
// Plain data (no pointers): stored as a blob in NVS.
struct PelletCycle {
    uint32_t t;        // end of the cycle (epoch s)
    float    kg;       // pellets added during the cycle
    float    units;    // auger units burned during the cycle
};
struct PelletTracker {
    static const uint32_t LOW_CONFIRM_S = 60;
    static const uint32_t REFILL_CONFIRM_S = 30;
    static const int      MAX_CYCLES = 10;
    static const uint32_t MAX_STEP_S = 30;   // longer gaps (no readings) are counted as 30 s
    static constexpr float AUGER_CYCLE_S = 12.0f;   // auger cycle; a larger on-time = running all the time
    static constexpr double RECENT_WEIGHT = 0.75;   // weight of each older cycle relative to the next one

    uint32_t version = 2;         // 2: predKg added at the end (version 1 is read and converted)
    double   units = 0;           // auger units counted since the start
    double   cycleStart = -1;     // units at the last trip, < 0 = no trip seen yet
    float    cycleKg = 0;         // kg added since that trip
    float    refillKg = 15;       // amount of an automatically detected refill
    float    pendingKg = 0;       // amount entered by hand while low, used for the refill that follows
    uint8_t  low = 0;             // confirmed low (between a trip and the refill)
    uint32_t lowSince = 0;        // sensor low since (epoch s), 0 = not low
    double   lowUnits = 0;        // units when the sensor went low
    uint32_t okSince = 0;         // sensor normal again since (while low)
    uint32_t lastT = 0;           // time of the last update
    uint32_t lastRefillT = 0;     // time of the last refill (detected or entered), 0 = none
    float    lastRefillKg = 0;    // its amount
    uint8_t  nCycles = 0;
    PelletCycle cycles[MAX_CYCLES] = {};
    // per cycle (same index): kg the estimate still had left when the alarm came; NAN = there
    // was no estimate yet (first cycle). Keep at the end: older layouts are converted by size.
    float    predKg[MAX_CYCLES] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};

    enum Event { NONE = 0, TRIP = 1, REFILL = 2 };

    // One reading: time (epoch s), level sensor (RAM 0xA4) and auger value (RAM 0x0D).
    // Returns what happened.
    int update(uint32_t t, int sensor, float auger)
    {
        if (lastT && t > lastT && auger > 0) {
            uint32_t dt = t - lastT;
            float a = auger > AUGER_CYCLE_S ? AUGER_CYCLE_S : auger;   // pre-load: 20.0 = 12 s of 12 s
            units += (double)a * (dt > MAX_STEP_S ? MAX_STEP_S : dt);
        }
        lastT = t;
        int ev = NONE;
        if (sensor == 5) {
            okSince = 0;
            if (!low) {
                if (!lowSince) { lowSince = t; lowUnits = units; }
                if (t - lowSince >= LOW_CONFIRM_S) { trip(t); ev = TRIP; }
            }
        } else if (sensor == 0) {
            lowSince = 0;
            if (low) {
                if (!okSince) okSince = t;
                if (t - okSince >= REFILL_CONFIRM_S) {
                    low = 0; okSince = 0;
                    float kg = pendingKg > 0 ? pendingKg : refillKg;
                    cycleKg += kg;
                    pendingKg = 0;
                    lastRefillT = t; lastRefillKg = kg;
                    ev = REFILL;
                }
            }
        }
        return ev;
    }
    // Pellets added by hand at time t. While low: replaces the default amount of the
    // refill that is detected when the sensor clears (its time is that of the detection).
    void addKg(float kg, uint32_t t)
    {
        if (kg <= 0) return;
        if (low) { pendingKg = kg; return; }
        cycleKg += kg;
        lastRefillT = t; lastRefillKg = kg;
    }
    void resetCalibration() { nCycles = 0; }

    // kg per auger unit from the stored cycles: weighted total kg / total units, the newest
    // cycle with weight 1, each older one RECENT_WEIGHT times the next; 0 = not known
    double kgPerUnit() const
    {
        double kg = 0, u = 0, w = 1;
        for (int i = nCycles - 1; i >= 0; i--, w *= RECENT_WEIGHT) { kg += w * cycles[i].kg; u += w * cycles[i].units; }
        return u > 0 ? kg / u : 0;
    }
    // weight of cycle i in kgPerUnit (newest = 1)
    double cycleWeight(int i) const { double w = 1; for (int j = nCycles - 1; j > i; j--) w *= RECENT_WEIGHT; return w; }
    // past the estimate: the counter says the pellets above the sensor are used up, but the
    // alarm did not come yet (more pellets per unit than calibrated)
    bool overdue() const
    {
        double k = kgPerUnit();
        return !low && cycleStart >= 0 && k > 0 && cycleKg - k * (units - cycleStart) < 0;
    }
    // Spread of the cycles (relative standard deviation of kg / unit), 0 with < 2 cycles
    double spread() const
    {
        if (nCycles < 2) return 0;
        double m = 0;
        for (int i = 0; i < nCycles; i++) m += cycles[i].kg / cycles[i].units;
        m /= nCycles;
        double v = 0;
        for (int i = 0; i < nCycles; i++) { double d = cycles[i].kg / cycles[i].units - m; v += d * d; }
        v /= (nCycles - 1);
        return m > 0 ? sqrt(v) / m : 0;
    }
    // kg left above the sensor level; < 0 = not known (no trip seen or not calibrated)
    double remainingKg() const
    {
        double k = kgPerUnit();
        if (cycleStart < 0 || k <= 0) return -1;
        if (low) return 0;
        double r = cycleKg - k * (units - cycleStart);
        return r < 0 ? 0 : r;
    }
    // hours until the sensor trips at the given auger value, < 0 = not known / not burning
    double hoursAt(float auger) const
    {
        double r = remainingKg(), k = kgPerUnit();
        if (r < 0 || k <= 0 || auger <= 0) return -1;
        return r / (k * auger * 3600.0);
    }

private:
    void trip(uint32_t t)
    {
        low = 1;
        if (cycleStart >= 0 && cycleKg > 0 && lowUnits > cycleStart) {
            double k = kgPerUnit();   // before this cycle: what the estimate said
            float pred = k > 0 ? (float)(cycleKg - k * (lowUnits - cycleStart)) : NAN;
            if (nCycles == MAX_CYCLES) {
                memmove(cycles, cycles + 1, sizeof(cycles[0]) * (MAX_CYCLES - 1));
                memmove(predKg, predKg + 1, sizeof(predKg[0]) * (MAX_CYCLES - 1));
                nCycles--;
            }
            predKg[nCycles] = pred;
            cycles[nCycles++] = { t, cycleKg, (float)(lowUnits - cycleStart) };
        }
        cycleStart = lowUnits;
        cycleKg = 0;
    }
};

// ── Room temperature source ──────────────────────────────────────────────────────────
// The stove regulates on RAM 0x01: its own sensor (RAM 0x44) or, with the wireless sensor
// on, the remote control's (RAM 0x8F) while the remote is heard. Which one is not readable;
// it is the one RAM 0x01 equals (all in steps of 0.5 C, so exact). When it equals both or
// neither, the previous answer stays. 0 = not known, 1 = stove sensor, 2 = remote control.
enum RoomSource { RS_UNKNOWN = 0, RS_STOVE = 1, RS_REMOTE = 2 };
inline int roomSourceOf(float room, float stoveSensor, float remoteSensor, int prev)
{
    bool s = room == stoveSensor, r = room == remoteSensor;
    if (s && !r) return RS_STOVE;
    if (r && !s) return RS_REMOTE;
    return prev;
}

// ── Pellet use per day and cost ─────────────────────────────────────────────
// Auger units per local day (YYYYMMDD), not kg: kg = units x kg per unit of the calibration at
// the moment it is shown, so old days get more accurate with every calibration cycle too.
// The day in progress runs from dayStart (the unit counter at its start). Plain data, stored as a
// file on SPIFFS (pellets.h).
struct PelletDay {
    uint32_t day;      // YYYYMMDD (local time)
    float    units;    // auger units burned that day
};
struct PelletUsage {
    static const int MAX_DAYS = 400;
    uint32_t version = 1;
    uint32_t curDay = 0;          // day in progress, 0 = none yet
    double   dayStart = 0;        // unit counter at its start
    int32_t  n = 0, head = 0;     // closed days in the ring, next write position
    PelletDay days[MAX_DAYS] = {};

    // Called with the local day and the unit counter; closes the day in progress when the date
    // changed (all units since its start count for it, also over days the controller was off).
    // Returns true when a day was closed.
    bool roll(uint32_t today, double units)
    {
        if (!today) return false;
        if (!curDay) { curDay = today; dayStart = units; return false; }
        if (today == curDay) return false;
        float u = units > dayStart ? (float)(units - dayStart) : 0;
        days[head] = { curDay, u };
        head = (head + 1) % MAX_DAYS;
        if (n < MAX_DAYS) n++;
        curDay = today; dayStart = units;
        return true;
    }
    double unitsToday(double units) const { return curDay && units > dayStart ? units - dayStart : 0; }
    // i = 0 is the most recent closed day
    const PelletDay& closed(int i) const { return days[(head - 1 - i + 2 * MAX_DAYS) % MAX_DAYS]; }
    // Units of the last k closed days (yesterday and before), and how many of them are known
    double unitsClosed(int k, int* got = nullptr) const
    {
        double u = 0; int c = 0;
        for (int i = 0; i < n && i < k; i++) { u += closed(i).units; c++; }
        if (got) *got = c;
        return u;
    }
};
// Price and stock: price per bag, kg per bag, bags in stock (fractions allowed)
struct PelletCost {
    float pricePerBag = 0;   // €, 0 = not set
    float bagKg = 15;
    float stockBags = -1;    // < 0 = not kept
    double pricePerKg() const { return pricePerBag > 0 && bagKg > 0 ? pricePerBag / bagKg : 0; }
    // pellets taken from the stock (a refill): kg out of the bags, never below 0
    void take(float kg) { if (stockBags >= 0 && bagKg > 0) { stockBags -= kg / bagKg; if (stockBags < 0) stockBags = 0; } }
};
// "2026-11-23" from YYYYMMDD
inline void dayText(uint32_t d, char out[11]) { snprintf(out, 11, "%04u-%02u-%02u", (unsigned)(d / 10000), (unsigned)(d / 100 % 100), (unsigned)(d % 100)); }

} // namespace pc
