#pragma once
// Pure logic of the PalletHeater firmware: encodings and decisions that do not
// touch the hardware. No Arduino dependencies, so it is used by the firmware and
// by the native unit tests (test/, `pio test -e native`).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

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
// Not verified: alarm assumed when the sensor (RAM 0xA4) is 5 and the counter
// (RAM 0x89) has run down to <= 1.
inline int pelletsLow(int sensor, int counter)
{
    return sensor == 5 && counter >= 0 && counter <= 1 ? 1 : 0;
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

} // namespace pc
