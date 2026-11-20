#pragma once
// HTTP routes for the chrono programs: /api/timers and /api/timers/prN.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

static int timeStrToTimerValue(const char* str)
{
    return pc::timerValueFromStr(str);
}

// ─────────────────────────────────────────────
// Timer helpers shared by /api/timers and /api/timers/prN
// ─────────────────────────────────────────────

static int timerIndexFromUri(const String& uri)
{
    const char* prNames[6] = {"pr1", "pr2", "pr3", "pr4", "pr5", "pr6"};
    String lower = uri;
    lower.toLowerCase();
    for (int i = 0; i < 6; i++)
        if (lower.endsWith(prNames[i])) return i;
    return -1;
}

// Returns false (and sets timer["error"]) when a register could not be read,
// so clients never see — or write back — half-read garbage.
static bool serializeTimerToJson(int idx, JsonObject timer)
{
    byte base = timerBases[idx];
    int enabled = stove.read(eeprom, base + TIMER_ONOFF_OFFSET);
    int onVal   = stove.read(eeprom, base + TIMER_ONTIME_OFFSET);
    int offVal  = stove.read(eeprom, base + TIMER_OFFTIME_OFFSET);
    int dow     = stove.read(eeprom, base + TIMER_DOW_OFFSET);
    int sp      = stove.read(eeprom, base + TIMER_SETPOINT_OFFSET);
    int fp      = stove.read(eeprom, base + TIMER_FLAME_OFFSET);
    int fan     = stove.read(eeprom, base + TIMER_FAN_OFFSET);
    if (enabled < 0 || onVal < 0 || offVal < 0 || dow < 0 || sp < 0 || fp < 0 || fan < 0)
    {
        timer["error"] = "Failed to read timer from stove - RS232 error";
        return false;
    }

    // Keep the BLE / metrics cache in sync with what we just read
    cachedTimerEnabled[idx]  = enabled;
    cachedTimerOnVal[idx]    = onVal;
    cachedTimerOffVal[idx]   = offVal;
    cachedTimerSetpoint[idx] = sp;
    cachedTimerFlame[idx]    = fp;
    cachedTimerFan[idx]      = fan;
    cachedTimerDowMask[idx]  = dow;

    timer["enabled"] = enabled > 0;
    // Always return the stored program, also when it is switched off: the stove keeps
    // it (the remote shows it in the CHRONO menu) and the web app needs it to edit.
    {
        timer["on"]  = timerValueToTimeStr(onVal);
        timer["off"] = timerValueToTimeStr(offVal);

        JsonObject enabledOn = timer.createNestedObject("enabled_on");
        enabledOn["monday"]    = (dow & 1)  != 0;
        enabledOn["tuesday"]   = (dow & 2)  != 0;
        enabledOn["wednesday"] = (dow & 4)  != 0;
        enabledOn["thursday"]  = (dow & 8)  != 0;
        enabledOn["friday"]    = (dow & 16) != 0;
        enabledOn["saturday"]  = (dow & 32) != 0;
        enabledOn["sunday"]    = (dow & 64) != 0;

        JsonObject settings = timer.createNestedObject("settings");
        settings["setpoint"]     = sp;
        settings["flame_power"]  = fp;
        settings["RoomFanSpeed"] = fan;
    }
    return true;
}

// Validate a timer object before anything is written, so a request is either
// applied completely or rejected with 400. Returns an empty string when valid.
static String validateTimerJson(const char* name, JsonObject timer)
{
    String prefix = String(name) + ": ";
    if (timer.isNull()) return prefix + "must be a JSON object";

    if (timer.containsKey("enabled") && !timer["enabled"].is<bool>())
        return prefix + "'enabled' must be true or false";

    const char* timeKeys[2] = {"on", "off"};
    for (const char* key : timeKeys)
    {
        if (!timer.containsKey(key)) continue;
        const char* str = timer[key].as<const char*>();
        if (!str || timeStrToTimerValue(str) < 0)
            return prefix + "'" + key + "' must be HH:MM on a 10 minute boundary or 'off'";
    }

    if (timer.containsKey("enabled_on"))
    {
        if (!timer["enabled_on"].is<JsonObject>()) return prefix + "'enabled_on' must be an object";
        for (JsonPair kv : timer["enabled_on"].as<JsonObject>())
        {
            String day = kv.key().c_str();
            if (day != "monday" && day != "tuesday" && day != "wednesday" && day != "thursday" &&
                day != "friday" && day != "saturday" && day != "sunday")
                return prefix + "unknown day '" + day + "' in 'enabled_on'";
            if (!kv.value().is<bool>()) return prefix + "'enabled_on." + day + "' must be true or false";
        }
    }

    if (timer.containsKey("settings"))
    {
        if (!timer["settings"].is<JsonObject>()) return prefix + "'settings' must be an object";
        JsonObject settings = timer["settings"].as<JsonObject>();
        struct { const char* key; int min; int max; } ranges[] = {
            { "setpoint",     7, 30 },
            { "flame_power",  1, 4  },
            { "RoomFanSpeed", 0, 3  },
        };
        for (auto& r : ranges)
        {
            if (!settings.containsKey(r.key)) continue;
            if (!settings[r.key].is<int>())
                return prefix + "'settings." + r.key + "' must be an integer";
            int v = settings[r.key].as<int>();
            if (v < r.min || v > r.max)
                return prefix + "'settings." + r.key + "' must be " + r.min + "-" + r.max;
        }
    }
    return "";
}

static void sendJsonError(int code, const String& msg)
{
    StaticJsonDocument<256> json;
    json["error"] = msg;
    String out;
    serializeJson(json, out);
    wm.server->send(code, "application/json", out);
}

// Partial write — only touches keys present in the JSON object.
// Call validateTimerJson() first; this function assumes valid input.
static void applyTimerJson(int idx, JsonObject timer)
{
    byte base = timerBases[idx];

    if (timer.containsKey("enabled"))
        stove.write(eeprom, base + TIMER_ONOFF_OFFSET, (timer["enabled"] | false) ? 1 : 0);

    if (timer.containsKey("on"))
    {
        int val = timeStrToTimerValue(timer["on"].as<const char*>());
        if (val >= 0) stove.write(eeprom, base + TIMER_ONTIME_OFFSET, val);
    }

    if (timer.containsKey("off"))
    {
        int val = timeStrToTimerValue(timer["off"].as<const char*>());
        if (val >= 0) stove.write(eeprom, base + TIMER_OFFTIME_OFFSET, val);
    }

    if (timer.containsKey("enabled_on"))
    {
        JsonObject dow = timer["enabled_on"].as<JsonObject>();
        byte dowVal = 0;
        if (dow["monday"]    | false) dowVal |= 1;
        if (dow["tuesday"]   | false) dowVal |= 2;
        if (dow["wednesday"] | false) dowVal |= 4;
        if (dow["thursday"]  | false) dowVal |= 8;
        if (dow["friday"]    | false) dowVal |= 16;
        if (dow["saturday"]  | false) dowVal |= 32;
        if (dow["sunday"]    | false) dowVal |= 64;
        stove.write(eeprom, base + TIMER_DOW_OFFSET, dowVal);
    }

    if (timer.containsKey("settings"))
    {
        JsonObject settings = timer["settings"].as<JsonObject>();
        if (settings.containsKey("setpoint"))
        {
            int sp = settings["setpoint"].as<int>();
            if (sp >= 7 && sp <= 30) stove.write(eeprom, base + TIMER_SETPOINT_OFFSET, sp);
        }
        if (settings.containsKey("flame_power"))
        {
            int fp = settings["flame_power"].as<int>();
            if (fp >= 1 && fp <= 4) stove.write(eeprom, base + TIMER_FLAME_OFFSET, fp);
        }
        if (settings.containsKey("RoomFanSpeed"))
        {
            int fs = settings["RoomFanSpeed"].as<int>();
            if (fs >= 0 && fs <= 3) stove.write(eeprom, base + TIMER_FAN_OFFSET, fs);
        }
    }
}

// ─────────────────────────────────────────────
// GET/POST  /api/timers  — all timers
// ─────────────────────────────────────────────

void handleRouteTimers()
{
    const char* prNames[6] = {"PR1", "PR2", "PR3", "PR4", "PR5", "PR6"};

    if (wm.server->method() == HTTP_GET)
    {
        DynamicJsonDocument json(2048);
        JsonObject timers = json.createNestedObject("timers");
        for (int i = 0; i < 6; i++)
            serializeTimerToJson(i, timers.createNestedObject(prNames[i]));
        sendJson(200, json);
        return;
    }

    if (wm.server->method() == HTTP_POST)
    {
        String body = wm.server->arg("plain");
        if (body.length() == 0) { wm.server->send(400, "application/json", "{\"error\":\"Empty body\"}"); return; }
        DynamicJsonDocument input(2048);
        if (deserializeJson(input, body)) { wm.server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
        if (!input.containsKey("timers")) { wm.server->send(400, "application/json", "{\"error\":\"Missing 'timers' key\"}"); return; }

        JsonObject timers = input["timers"].as<JsonObject>();
        if (timers.isNull()) { sendJsonError(400, "'timers' must be an object"); return; }

        // Reject unknown program names and validate everything before writing anything
        for (JsonPair kv : timers)
        {
            int idx = -1;
            for (int i = 0; i < 6; i++)
                if (strcmp(kv.key().c_str(), prNames[i]) == 0) idx = i;
            if (idx < 0) { sendJsonError(400, String("Unknown timer '") + kv.key().c_str() + "', expected PR1-PR6"); return; }
            String err = validateTimerJson(prNames[idx], kv.value().as<JsonObject>());
            if (err.length()) { sendJsonError(400, err); return; }
        }

        for (int i = 0; i < 6; i++)
            if (timers.containsKey(prNames[i]))
            {
                applyTimerJson(i, timers[prNames[i]].as<JsonObject>());
                refreshTimerCache(i);
            }

        wm.server->send(200, "application/json", "{\"result\":\"success\"}");
        return;
    }

    wm.server->send(405, "application/json", "{\"error\":\"Method not allowed\"}");
}

// ─────────────────────────────────────────────
// GET / POST / PATCH / DELETE  /api/timers/prN
// ─────────────────────────────────────────────

static void sendTimerResponse(int idx)
{
    const char* prNames[6] = {"PR1", "PR2", "PR3", "PR4", "PR5", "PR6"};
    DynamicJsonDocument json(512);
    json["result"] = "success";
    if (!serializeTimerToJson(idx, json.createNestedObject(prNames[idx])))
        json["result"] = "success - but reading the timer back failed";
    String out;
    serializeJson(json, out);
    wm.server->send(200, "application/json", out);
}

void handleRouteTimer()
{
    int idx = timerIndexFromUri(wm.server->uri());
    if (idx < 0) { wm.server->send(404, "application/json", "{\"error\":\"Timer not found\"}"); return; }

    const char* prNames[6] = {"PR1", "PR2", "PR3", "PR4", "PR5", "PR6"};

    // GET — return this timer's current config
    if (wm.server->method() == HTTP_GET)
    {
        DynamicJsonDocument json(512);
        bool ok = serializeTimerToJson(idx, json.createNestedObject(prNames[idx]));
        String out;
        serializeJson(json, out);
        wm.server->send(ok ? 200 : 500, "application/json", out);
        return;
    }

    // DELETE — disable the timer
    if (wm.server->method() == HTTP_DELETE)
    {
        stove.write(eeprom, timerBases[idx] + TIMER_ONOFF_OFFSET, 0);
        sendTimerResponse(idx);
        return;
    }

    // POST  — no body: enable the timer; with a JSON body: partial update (same as PATCH)
    // PATCH — partial update (only provided fields written)
    // Bodies are validated first; invalid input returns 400 and nothing is written.
    if (wm.server->method() == HTTP_POST || wm.server->method() == HTTP_PATCH)
    {
        String body = wm.server->arg("plain");

        // Empty body on POST = enable the timer without touching other settings
        if (body.length() == 0)
        {
            if (wm.server->method() == HTTP_POST)
            {
                stove.write(eeprom, timerBases[idx] + TIMER_ONOFF_OFFSET, 1);
                sendTimerResponse(idx);
            }
            else
            {
                wm.server->send(400, "application/json", "{\"error\":\"Empty body\"}");
            }
            return;
        }

        DynamicJsonDocument input(512);
        if (deserializeJson(input, body)) { wm.server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }

        JsonObject timer = input.as<JsonObject>();
        String err = validateTimerJson(prNames[idx], timer);
        if (err.length()) { sendJsonError(400, err); return; }
        applyTimerJson(idx, timer);
        sendTimerResponse(idx);
        return;
    }

    wm.server->send(405, "application/json", "{\"error\":\"Method not allowed\"}");
}
