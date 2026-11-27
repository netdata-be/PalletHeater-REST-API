#pragma once
// WS2812 status LEDs (statusLed.h): the states of LED1 / LED2 from WiFi and the stove, and
// GET/POST /api/leds. Only on boards with them (BOARD_HAS_WS2812); included after the stove
// cache, the pellet and the update modules (single translation unit).
#if BOARD_HAS_WS2812

// LED2 from the cached stove values (stoveCache.h); no bus access
static uint8_t ledStoveState(bool& warn)
{
    warn = false;
    if (stove.linkDown || (lastFastStatesAt && stoveState == UNKNOWN)) return LS_LINK_DOWN;
    if (!lastFastStatesAt) return LS_OFF;   // nothing read yet
    if (stoveState >= ALARM || confirmedCurrentError || pelletsEmptyOff()) return LS_ALARM;
    // warnings, as in GET /api/state: pellet reserve running, an error the stove counts down
    // (pendingError, e.g. E1 hopper lid open), the remote's sensor not used
    warn = (pc::reserveSensorLow(cachedPelletSensor) && !pc::reserveRefilling(cachedPelletSensor)) || cachedLowPallet
        || cachedCurrentError > 0 || remoteSensorNotUsed();
    switch (stoveState) {
    case STARTING: case PALLET_LOADING: case IGNITION: return LS_STARTING;
    case WORKING:          return LS_WORKING;
    case BRAZIER_CLEANING: return LS_CLEANING;
    case FINAL_CLEANING:   return LS_EXTINGUISHING;
    case STANDBY:          return LS_STANDBY;
    default:               return LS_OFF;
    }
}
static bool ledNightNow()
{
    if (!ledCfg.night || ledCfg.nightFrom == ledCfg.nightTo) return false;
    time_t t = time(nullptr);
    if (t <= 1700000000) return false;   // clock not set: no night
    struct tm lt;
    localtime_r(&t, &lt);
    int h = lt.tm_hour, f = ledCfg.nightFrom, e = ledCfg.nightTo;
    return f < e ? h >= f && h < e : h >= f || h < e;
}

// From loop() (statusLedLoop), every 200 ms
void ledsStateTick()
{
    static uint32_t last = 0;
    static bool first = true;
    if (millis() - last < 200) return;
    last = millis();
    if (first) {   // a crash, watchdog, brown-out or rolled back update before this start
        first = false;
        ledError = resetIsCrash(esp_reset_reason()) || otaRolledBack.length();
    }
    if (WiFi.status() == WL_CONNECTED) ledSys = LS_CONNECTED;
    else ledSys = wm.getConfigPortalActive() ? LS_PORTAL : LS_CONNECTING;
    ledBleUpdate = otaInProgress;   // bleUart.h
    bool warn;
    ledStove = ledStoveState(warn);
    ledStoveWarn = warn;
    ledNight = ledNightNow();
}

// GET/POST /api/leds: enabled, brightness 1 - 100 (% of the driver's cap), night (off from
// nightFrom to nightTo, local hours); GET also shows what the LEDs show now
void handleRouteLeds()
{
    StaticJsonDocument<384> json;
    int code = 200;
    if (wm.server->method() == HTTP_POST) {
        auto has = [](const char* k) { return wm.server->hasArg(k); };
        auto arg = [](const char* k) { String v = wm.server->arg(k); v.trim(); return v; };
        LedSettings c = ledCfg;
        String err;
        int v, b;
        if (has("enabled")) { if ((b = pc::parseOnOff(arg("enabled").c_str())) < 0) err = "enabled must be true / false"; else c.enabled = b == 1; }
        if (has("brightness")) { if (!pc::parseIntRange(arg("brightness").c_str(), 1, 100, v)) err = "brightness must be 1 - 100"; else c.brightness = v; }
        if (has("night")) { if ((b = pc::parseOnOff(arg("night").c_str())) < 0) err = "night must be true / false"; else c.night = b == 1; }
        if (has("nightFrom")) { if (!pc::parseIntRange(arg("nightFrom").c_str(), 0, 23, v)) err = "nightFrom must be 0 - 23"; else c.nightFrom = v; }
        if (has("nightTo")) { if (!pc::parseIntRange(arg("nightTo").c_str(), 0, 23, v)) err = "nightTo must be 0 - 23"; else c.nightTo = v; }
        if (err.length()) { json["result"] = "failed - " + err; code = 400; }
        else {
            ledCfg = c;
            ledSaveSettings();
            ledNight = ledNightNow();
            json["result"] = "success";
            LOGI("sys", "status LEDs: %s, brightness %u %%, night off %s (%02u - %02u h) (%s)", c.enabled ? "on" : "off",
                 c.brightness, c.night ? "on" : "off", c.nightFrom, c.nightTo, clientIp().c_str());
        }
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    json["enabled"] = ledCfg.enabled;
    json["brightness"] = ledCfg.brightness;
    json["maxPwmPercent"] = LED_MAX_PWM * 100 / 255;
    json["night"] = ledCfg.night;
    json["nightFrom"] = ledCfg.nightFrom;
    json["nightTo"] = ledCfg.nightTo;
    json["nightActive"] = (bool)ledNight;
    json["sys"] = LED_LOOK[ledSysState()].name;
    json["stove"] = LED_LOOK[ledStove].name;
    json["stoveWarning"] = (bool)ledStoveWarn;
    String out; serializeJson(json, out);
    wm.server->send(code, "application/json; charset=utf-8", out);
}

#endif
