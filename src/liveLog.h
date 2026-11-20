#pragma once
// Live log for the web app (GET /api/log, liveDebug.h).
//
// Everything this firmware prints with Serial.* still goes to the serial port (UART0)
// and is also kept in an 8 KB ring buffer in RAM, each line prefixed with the time.
// ESP-IDF log output (ESP_LOGx) is captured too. Output of precompiled libraries
// that print to the UART themselves (WiFiManager, NimBLE, Arduino log_x) is not.
//
// How: `Serial` is redefined to a small Stream that forwards to the real port and
// copies into the buffer. That works because main.cpp is one translation unit:
// this header is included first, so every later `Serial.` in our headers uses it.
#include <Arduino.h>
#include <esp_log.h>
#include <time.h>
#include <pelletcore.h>
#include <Preferences.h>
#include <esp_system.h>

// On the heap (static DRAM is full): allocated by liveLogSetup(), output before that is not kept
typedef pc::TextRing<8192> LogRing;
static LogRing* logRing = nullptr;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
static bool logAtLineStart = true;

// ── Log that survives a restart ───────────────────────────────────────
// The last 2 KB of output also go to RTC memory, which keeps its content over a
// software restart, watchdog reset or crash (not over a power cut). At the next
// start that text is put at the top of the live log, so you can see what
// happened just before. The same block holds a note why we restarted ourselves.
static const uint32_t RTC_MAGIC = 0x4F504231;   // "OPB1"
struct RtcDiag {
    uint32_t magic;
    uint32_t total;            // bytes ever written this boot
    char     log[2048];
    char     note[48];         // reason of a restart we did ourselves
};
static RTC_NOINIT_ATTR RtcDiag rtcDiag;
static bool rtcReady = false;

// ── Breadcrumb: what the loop task is doing ───────────────────────────
// The loop task is watched by the task watchdog (WDT_TIMEOUT). Each step of loop() and
// each web request notes here what it is doing; the block is in RTC memory, so
// after a watchdog reset or crash the next start can tell where it got stuck.
// When the task watchdog fires, esp_task_wdt_isr_user_handler() (called by the
// ESP-IDF just before it resets) adds the task running on each CPU at that moment.
static const uint32_t CRUMB_MAGIC = 0x4F504243;   // "OPBC"
struct RtcCrumb {
    uint32_t magic;
    uint32_t since;        // millis() when the step started
    uint32_t wdtAt;        // millis() when the task watchdog fired, 0 = it did not
    char     step[64];     // what the loop task was doing
    char     cpu[2][16];   // task running on CPU 0 / 1 when the watchdog fired
};
static RTC_NOINIT_ATTR RtcCrumb rtcCrumb;
static String diagCrumb;   // where the previous run was when it crashed, "" = not known / no crash

static void crumb(const char* step)
{
    strncpy(rtcCrumb.step, step, sizeof(rtcCrumb.step) - 1);
    rtcCrumb.step[sizeof(rtcCrumb.step) - 1] = 0;
    rtcCrumb.since = millis();
}
static void crumbf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void crumbf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rtcCrumb.step, sizeof(rtcCrumb.step), fmt, ap);
    va_end(ap);
    rtcCrumb.since = millis();
}
// Called by the ESP-IDF task watchdog interrupt (weak symbol in esp_system),
// right before the reset: only fill in RTC memory, no logging.
extern "C" void esp_task_wdt_isr_user_handler(void)
{
    rtcCrumb.wdtAt = millis();
    if (!rtcCrumb.wdtAt) rtcCrumb.wdtAt = 1;
    for (int c = 0; c < 2; c++) {
        TaskHandle_t t = xTaskGetCurrentTaskHandleForCPU(c);
        const char* n = t ? pcTaskGetTaskName(t) : "?";
        strncpy(rtcCrumb.cpu[c], n ? n : "?", sizeof(rtcCrumb.cpu[c]) - 1);
        rtcCrumb.cpu[c][sizeof(rtcCrumb.cpu[c]) - 1] = 0;
    }
}

static void rtcLogWrite(char c)
{
    rtcDiag.log[rtcDiag.total % sizeof(rtcDiag.log)] = c;
    rtcDiag.total++;
}

static void logCapture(const uint8_t* b, size_t n)
{
    // time stamp for lines starting in this chunk: wall clock once NTP is there
    if (!logRing) return;
    char st[16];
    time_t now = time(nullptr);
    if (now > 1700000000) { struct tm tm; localtime_r(&now, &tm); strftime(st, sizeof(st), "%H:%M:%S ", &tm); }
    else snprintf(st, sizeof(st), "+%lus ", (unsigned long)(millis() / 1000));
    size_t sl = strlen(st);
    portENTER_CRITICAL(&logMux);
    for (size_t i = 0; i < n; i++) {
        char c = (char)b[i];
        if (c == '\r') continue;
        if (logAtLineStart) {
            logRing->write(st, sl); logAtLineStart = false;
            if (rtcReady) for (size_t k = 0; k < sl; k++) rtcLogWrite(st[k]);
        }
        logRing->write(&c, 1);
        if (rtcReady) rtcLogWrite(c);
        if (c == '\n') logAtLineStart = true;
    }
    portEXIT_CRITICAL(&logMux);
}

class TeeSerial : public Stream {
public:
    using Print::write;
    void begin(unsigned long baud) { Serial.begin(baud); }
    size_t write(uint8_t c) override { size_t n = Serial.write(c); logCapture(&c, 1); return n; }
    size_t write(const uint8_t* b, size_t n) override { size_t w = Serial.write(b, n); logCapture(b, n); return w; }
    int available() override { return Serial.available(); }
    int read() override { return Serial.read(); }
    int peek() override { return Serial.peek(); }
    void flush() override { Serial.flush(); }
    operator bool() const { return true; }
};
static TeeSerial logSerial;
#define Serial logSerial

// ── Log levels per module ─────────────────────────────────────────────
//   LOGE("wifi", "...")  error     LOGW  warning     LOGI  info     LOGD  debug
// A line is printed when its level is at or below the module's level (default
// info; bus: warnings). Set at runtime with /api/loglevel, kept in NVS "log".
// Output: "E [wifi] message" (plus the time stamp in the live log).
enum { LOG_OFF = 0, LOG_ERROR = 1, LOG_WARN = 2, LOG_INFO = 3, LOG_DEBUG = 4 };
static const char* const LOG_LEVEL_NAMES[] = { "off", "error", "warn", "info", "debug" };
struct LogModule { const char* tag; uint8_t level; const char* what; };
static LogModule logModules[] = {
    { "sys",     LOG_INFO, "start-up, restarts, settings" },
    { "wifi",    LOG_INFO, "WiFi connection, IP, network changes" },
    { "mqtt",    LOG_INFO, "MQTT connection and commands" },
    { "stove",   LOG_INFO, "stove values (debug: every reading)" },
    { "bus",     LOG_WARN, "stove bus: failed reads / writes" },
    { "update",  LOG_INFO, "firmware updates" },
    { "http",    LOG_INFO, "web server" },
    { "https",   LOG_INFO, "HTTPS: certificate, TLS (debug: every connection)" },
    { "history", LOG_INFO, "history and event log" },
    { "ble",     LOG_INFO, "Bluetooth: connections and commands" },
};
static const int LOG_MODULES = sizeof(logModules) / sizeof(logModules[0]);
static LogModule* logModule(const char* tag)
{
    for (int i = 0; i < LOG_MODULES; i++) if (strcmp(logModules[i].tag, tag) == 0) return &logModules[i];
    return nullptr;
}
static void logPrintf(int level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void logPrintf(int level, const char* tag, const char* fmt, ...)
{
    LogModule* m = logModule(tag);
    if (level > (m ? m->level : LOG_INFO)) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("%c [%s] %s\n", "?EWID"[level], tag, buf);
}
#define LOGE(tag, ...) logPrintf(LOG_ERROR, tag, __VA_ARGS__)
#define LOGW(tag, ...) logPrintf(LOG_WARN,  tag, __VA_ARGS__)
#define LOGI(tag, ...) logPrintf(LOG_INFO,  tag, __VA_ARGS__)
#define LOGD(tag, ...) logPrintf(LOG_DEBUG, tag, __VA_ARGS__)
static int logLevelFromName(const char* n)
{
    for (int i = 0; i <= LOG_DEBUG; i++) if (strcasecmp(n, LOG_LEVEL_NAMES[i]) == 0) return i;
    return -1;
}
static void logLoadLevels()
{
    Preferences p;
    p.begin("log", true);
    for (int i = 0; i < LOG_MODULES; i++) logModules[i].level = p.getUChar(logModules[i].tag, logModules[i].level);
    p.end();
}
static void logSaveLevel(const char* tag, uint8_t level)
{
    Preferences p;
    p.begin("log", false);
    p.putUChar(tag, level);
    p.end();
}

// ── Why did the controller (re)start? ─────────────────────────────────
static esp_reset_reason_t diagReason = ESP_RST_UNKNOWN;
static String   diagNote;            // our own restart note from the previous run
// ── Who is writing to the stove? ─────────────────────────────────────
// Every entry point that writes to the stove (web/API, MQTT, Bluetooth, telnet)
// sets the source while it holds the stove bus lock; the write hook in
// liveDebug.h logs it with each write. Empty = the controller itself.
static char cmdSource[96] = "";
struct CmdSource {
    char prev[sizeof(cmdSource)];
    explicit CmdSource(const char* src) { strncpy(prev, cmdSource, sizeof(prev)); strncpy(cmdSource, src, sizeof(cmdSource) - 1); cmdSource[sizeof(cmdSource) - 1] = 0; }
    ~CmdSource() { strncpy(cmdSource, prev, sizeof(cmdSource)); }
};
// Last write to the stove state register (power on / off), for the reason of a state change
static volatile uint32_t lastStateWriteMs = 0;
static volatile uint8_t  lastStateWriteVal = 0;
static char lastStateWriteSrc[sizeof(cmdSource)] = "";

// ── Sensor task timing (stoveCache.h), summarised in the health line ──
static volatile uint32_t diagFastCycles = 0, diagFastSumMs = 0, diagFastMaxMs = 0;
static volatile uint32_t diagSlowCycles = 0, diagSlowMaxMs = 0, diagLockWaitMaxMs = 0;
static void diagCycle(bool slow, uint32_t waitMs, uint32_t durMs)
{
    if (waitMs > diagLockWaitMaxMs) diagLockWaitMaxMs = waitMs;
    if (slow) { diagSlowCycles++; if (durMs > diagSlowMaxMs) diagSlowMaxMs = durMs; }
    else { diagFastCycles++; diagFastSumMs += durMs; if (durMs > diagFastMaxMs) diagFastMaxMs = durMs; }
}

// WiFi drops (filled by the WiFi events in wifiInfo.h)
static volatile uint32_t wifiDisconnects = 0;
static uint32_t netwdRepairs = 0;   // network watchdog: reconnects + WiFi driver restarts since the start (wifiInfo.h)
static volatile uint8_t  wifiLastReason = 0;
static volatile uint32_t wifiLastDisconnectMs = 0;
static volatile bool     wifiWasUp = false;
static uint32_t diagBoots = 0, diagCrashes = 0;
static const char* resetReasonName(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_EXT:       return "reset pin";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake-up";
    case ESP_RST_BROWNOUT:  return "brown-out (supply voltage dropped)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
    }
}
static bool resetIsCrash(esp_reset_reason_t r)
{
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
}
static String diagResetText()
{
    String s = resetReasonName(diagReason);
    if (diagNote.length()) s += " – " + diagNote;
    if (diagCrumb.length()) s += " – " + diagCrumb;
    return s;
}
// Remember why we are about to restart (shown after the start), e.g. before
// ArduinoOTA restarts by itself.
static void markRestart(const char* why)
{
    strncpy(rtcDiag.note, why, sizeof(rtcDiag.note) - 1);
    rtcDiag.note[sizeof(rtcDiag.note) - 1] = 0;
}
// Use instead of ESP.restart(): logs and remembers the reason
static void restartWith(const char* why)
{
    markRestart(why);
    LOGI("sys", "restart: %s", why);
    Serial.flush();
    delay(100);
    ESP.restart();
}

// ESP-IDF logs: copy, then hand over to the previous handler (the UART)
static vprintf_like_t logPrevVprintf = nullptr;
static int logVprintf(const char* fmt, va_list ap)
{
    char buf[192];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap2);
    va_end(ap2);
    if (n > 0) logCapture((const uint8_t*)buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
    return logPrevVprintf ? logPrevVprintf(fmt, ap) : vprintf(fmt, ap);
}
static void liveLogSetup()
{
    if (!logRing) logRing = new (std::nothrow) LogRing();
    logPrevVprintf = esp_log_set_vprintf(logVprintf);
    logLoadLevels();

    // previous run: the log from RTC memory and our own restart note
    diagReason = esp_reset_reason();
    // breadcrumb of the previous run: only after a crash (after our own restart it
    // just shows the restart code path)
    if (rtcCrumb.magic == CRUMB_MAGIC && diagReason != ESP_RST_POWERON && resetIsCrash(diagReason)) {
        rtcCrumb.step[sizeof(rtcCrumb.step) - 1] = 0;
        rtcCrumb.cpu[0][sizeof(rtcCrumb.cpu[0]) - 1] = 0;
        rtcCrumb.cpu[1][sizeof(rtcCrumb.cpu[1]) - 1] = 0;
        diagCrumb = String("loop task was in: ") + (rtcCrumb.step[0] ? rtcCrumb.step : "?");
        if (rtcCrumb.wdtAt) {
            uint32_t d = rtcCrumb.wdtAt - rtcCrumb.since;
            char b[96];
            snprintf(b, sizeof(b), " (for %lu.%lu s); running: CPU0 %s, CPU1 %s",
                     (unsigned long)(d / 1000), (unsigned long)(d % 1000 / 100), rtcCrumb.cpu[0], rtcCrumb.cpu[1]);
            diagCrumb += b;
        }
    }
    rtcCrumb.magic = CRUMB_MAGIC; rtcCrumb.wdtAt = 0;
    crumb("setup");
    bool rtcValid = rtcDiag.magic == RTC_MAGIC && diagReason != ESP_RST_POWERON;
    if (rtcValid) {
        rtcDiag.note[sizeof(rtcDiag.note) - 1] = 0;
        diagNote = rtcDiag.note;
        if (logRing && rtcDiag.total) {
            String head = String("===== log before the restart (") + diagResetText() + ") =====\n";
            uint32_t n = rtcDiag.total < sizeof(rtcDiag.log) ? rtcDiag.total : sizeof(rtcDiag.log);
            portENTER_CRITICAL(&logMux);
            logRing->write(head.c_str(), head.length());
            for (uint32_t i = rtcDiag.total - n; i < rtcDiag.total; i++) { char c = rtcDiag.log[i % sizeof(rtcDiag.log)]; logRing->write(&c, 1); }
            if (rtcDiag.log[(rtcDiag.total - 1) % sizeof(rtcDiag.log)] != '\n') logRing->write("\n", 1);
            logRing->write("===== start =====\n", 18);
            portEXIT_CRITICAL(&logMux);
        }
    }
    rtcDiag.magic = RTC_MAGIC; rtcDiag.total = 0; rtcDiag.note[0] = 0;
    rtcReady = true;

    Preferences p;
    p.begin("diag", false);
    diagBoots = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", diagBoots);
    diagCrashes = p.getUInt("crashes", 0);
    if (resetIsCrash(diagReason)) p.putUInt("crashes", ++diagCrashes);
    p.end();
    if (resetIsCrash(diagReason)) LOGE("sys", "start #%lu after %s", (unsigned long)diagBoots, diagResetText().c_str());
    else LOGI("sys", "start #%lu after %s", (unsigned long)diagBoots, diagResetText().c_str());
}
