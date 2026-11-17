#pragma once
// Key watch: reverse-engineering aid to find what the remote control does on the
// stove when a key is pressed.
//
// The GRF remote talks to the stove board over radio; that traffic is not on the
// serial bus this controller is connected to. Only its effect in the stove RAM is
// visible, and some of those registers are set for well under a second (RAM 0x08 /
// 0x16 / 0x24 changed around key presses in a 1.2 s sampled test). The normal
// polling is far too slow for that, so while a watch runs the sensor task stops its
// normal polling and reads only the chosen RAM addresses back to back
// (about 20 reads per second in total), logging every change with a millisecond
// timestamp.
//
//   POST   /api/keywatch?address=0x08,0x16,0x24&seconds=60   start (clears the log)
//   GET    /api/keywatch                                     status + log
//   DELETE /api/keywatch                                     stop
//
// Read-only: nothing is written to the stove. The normal values (/api/state) are
// not refreshed while a watch runs.
#include <constants.h>

#define KW_MAX_ADDRS   8
#define KW_LOG_SIZE    600
#define KW_MAX_SECONDS 300
#define KW_DEFAULT_ADDRS "0x08,0x15,0x16,0x18,0x19,0x1A,0x24"

struct KwEvent { uint32_t ms; uint8_t addr; uint8_t from; uint8_t to; };

static volatile bool kwActive = false;
static unsigned long kwStartMs = 0, kwUntilMs = 0, kwStoppedMs = 0;
static time_t   kwStartEpoch = 0;
static uint8_t  kwAddrs[KW_MAX_ADDRS];
static uint8_t  kwN = 0;
static int      kwBase[KW_MAX_ADDRS];   // first value read, -1 = not read yet
static int      kwLast[KW_MAX_ADDRS];
static KwEvent  kwLog[KW_LOG_SIZE];
static volatile uint16_t kwCount = 0;
static bool     kwOverflow = false;
static uint32_t kwReads = 0, kwErrors = 0;

// Called by the sensor task on every round. Returns true while a watch runs (the
// task then skips its normal polling).
bool keyWatchTick()
{
    if (!kwActive) return false;
    if ((long)(millis() - kwUntilMs) >= 0) { kwActive = false; kwStoppedMs = millis(); return false; }
    if (stoveMutex) xSemaphoreTakeRecursive(stoveMutex, portMAX_DELAY);
    for (uint8_t i = 0; i < kwN && kwActive; i++)
    {
        int v = stove.read(ram, kwAddrs[i]);
        kwReads++;
        if (v < 0) { kwErrors++; continue; }
        if (kwLast[i] < 0) { kwBase[i] = kwLast[i] = v; continue; }
        if (v != kwLast[i])
        {
            if (kwCount < KW_LOG_SIZE) { kwLog[kwCount] = { (uint32_t)(millis() - kwStartMs), kwAddrs[i], (uint8_t)kwLast[i], (uint8_t)v }; kwCount++; }
            else kwOverflow = true;
            kwLast[i] = v;
        }
    }
    if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
    return true;
}

static void kwParseAddrs(const String& param)
{
    kwN = 0;
    String csv = param + ",";
    int start = 0;
    for (unsigned int i = 0; i < csv.length() && kwN < KW_MAX_ADDRS; i++)
    {
        if (csv[i] != ',') continue;
        String tok = csv.substring(start, i); tok.trim(); start = i + 1;
        if (!tok.length()) continue;
        int a = (tok.startsWith("0x") || tok.startsWith("0X")) ? (int)strtol(tok.c_str(), nullptr, 16) : tok.toInt();
        if (a < 0 || a > 255) continue;
        bool dup = false;
        for (uint8_t j = 0; j < kwN; j++) if (kwAddrs[j] == a) dup = true;
        if (!dup) kwAddrs[kwN++] = (uint8_t)a;
    }
}

static String kwHex(uint8_t a) { char b[6]; snprintf(b, sizeof(b), "0x%02X", a); return b; }

static void kwSendStatus(int code)
{
    uint16_t n = kwCount;
    String out; out.reserve(400 + n * 52);
    unsigned long ref = kwActive ? millis() : kwStoppedMs;
    out += "{\"active\":"; out += kwActive ? "true" : "false";
    out += ",\"remainingSeconds\":"; out += kwActive ? String((kwUntilMs - millis()) / 1000) : "0";
    out += ",\"started\":"; out += kwStartEpoch ? String((uint32_t)kwStartEpoch) : "null";
    unsigned long dur = kwStartMs ? ref - kwStartMs : 0;
    out += ",\"elapsedMs\":"; out += String(dur);
    out += ",\"reads\":"; out += String(kwReads);
    out += ",\"readErrors\":"; out += String(kwErrors);
    out += ",\"readsPerSecond\":"; out += dur > 0 ? String(kwReads * 1000.0f / dur, 1) : "0";
    out += ",\"overflow\":"; out += kwOverflow ? "true" : "false";
    out += ",\"addresses\":{";
    for (uint8_t i = 0; i < kwN; i++)
    {
        if (i) out += ",";
        out += "\"" + kwHex(kwAddrs[i]) + "\":{\"start\":" + (kwBase[i] >= 0 ? String(kwBase[i]) : "null")
             + ",\"now\":" + (kwLast[i] >= 0 ? String(kwLast[i]) : "null") + "}";
    }
    out += "},\"events\":[";
    for (uint16_t i = 0; i < n; i++)
    {
        if (i) out += ",";
        out += "{\"ms\":" + String(kwLog[i].ms) + ",\"addr\":\"" + kwHex(kwLog[i].addr) + "\",\"from\":"
             + String(kwLog[i].from) + ",\"to\":" + String(kwLog[i].to) + "}";
    }
    out += "]}";
    wm.server->send(code, "application/json", out);
}

void handleRouteKeyWatch()
{
    HTTPMethod m = wm.server->method();
    if (m == HTTP_POST)
    {
        kwActive = false;
        kwParseAddrs(wm.server->hasArg("address") ? wm.server->arg("address") : String(KW_DEFAULT_ADDRS));
        if (kwN == 0) { wm.server->send(400, "application/json", "{\"result\":\"failed - no valid address (max 8, RAM 0x00 - 0xFF)\"}"); return; }
        long secs = wm.server->hasArg("seconds") ? wm.server->arg("seconds").toInt() : 60;
        if (secs < 1) secs = 1;
        if (secs > KW_MAX_SECONDS) secs = KW_MAX_SECONDS;
        for (uint8_t i = 0; i < KW_MAX_ADDRS; i++) { kwBase[i] = -1; kwLast[i] = -1; }
        kwCount = 0; kwOverflow = false; kwReads = 0; kwErrors = 0;
        kwStartEpoch = time(nullptr);
        kwStartMs = millis(); kwUntilMs = kwStartMs + secs * 1000UL; kwStoppedMs = 0;
        kwActive = true;
        Serial.printf("Key watch started: %u addresses, %ld s\n", kwN, secs);
        kwSendStatus(200);
    }
    else if (m == HTTP_DELETE)
    {
        if (kwActive) { kwActive = false; kwStoppedMs = millis(); }
        kwSendStatus(200);
    }
    else if (m == HTTP_GET) kwSendStatus(200);
    else wm.server->send(405, "application/json", "{\"result\":\"failed - use GET, POST or DELETE\"}");
}
