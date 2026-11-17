#pragma once
// GET /api/state: the cached stove values as JSON.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

void getStatus()
{
    if (wm.server->method() == HTTP_GET)
    {
    //telnet.println("Get status called");

    DynamicJsonDocument json(3072);   // grew with roomFan, pelletReserve, controller
    json.clear();
    // Values never read from the stove since the start are null (not the firmware's
    // initial 0). Once read, the last known value stays; dataAge says how old it is.
    // fast = state, flame, fans, fumes, errors (every 2 s), slow = the rest (~16 s).
    const bool fastOk = lastFastStatesAt != 0, slowOk = lastSlowStatesAt != 0;
#define KNOWN(ok, dst, val) do { if (ok) dst = (val); else dst = nullptr; } while (0)

    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    // on / off is not known while the stove does not answer
    if (stoveState == UNKNOWN) obj_stove["poweredOn"] = nullptr; else obj_stove["poweredOn"] = StoveIsOn;

    if (int_hydro_mode == 1)
    {
        JsonObject hydro = json.createNestedObject("hydro");
        hydro["Pressure"] = waterPres;
        hydro["Temp"] = waterTemp;
    }

    JsonObject obj_flame = json.createNestedObject("flame");
    KNOWN(fastOk, obj_flame["power"], flamePower);                     // setting
    if (fastOk && cachedFlameActual >= 0) obj_flame["actual"] = cachedFlameActual; // what the stove runs at now
    else obj_flame["actual"] = nullptr;
    json["bluetooth"] = bleEnabled;
    json["telnet"] = telnetEnabled;
    json["apiKeyRequired"] = apiKeyRequired;

    JsonObject obj_temperatures = json.createNestedObject("temperatures");
    KNOWN(slowOk, obj_temperatures["room"], ambTemp);
    KNOWN(slowOk, obj_temperatures["stove"], ambTempStove);
    KNOWN(slowOk, obj_temperatures["remoteControl"], ambTempRemoteControl);
    KNOWN(slowOk, obj_temperatures["setPoint"], setPointTemp);
    KNOWN(fastOk, obj_temperatures["fume"], fumesTemp);
    KNOWN(slowOk, json["wirelessSensor"], wirelessSensorStr());
    if (stoveState == UNKNOWN) {
        // no answer from the stove: an error of the controller side (id -1), not a stove error code
        JsonObject ce = json.createNestedObject("currentError");
        ce["id"]     = -1;
        ce["error"]  = "LINK";
        ce["desc"]   = "No answer from the stove (serial link down)";
        ce["source"] = "controller";
    } else if (confirmedCurrentError == 0) {
        json["currentError"] = false;
    } else {
        JsonObject ce = json.createNestedObject("currentError");
        ce["id"]     = confirmedCurrentError;
        ce["error"]  = getErrorCode(confirmedCurrentError);
        ce["desc"]   = getErrorDesc(confirmedCurrentError);
        ce["source"] = "stove";
    }

    JsonObject obj_energysaving = json.createNestedObject("energySaving");
    KNOWN(slowOk, obj_energysaving["start"], energySavingStart);
    KNOWN(slowOk, obj_energysaving["stop"], energySavingStop);
    KNOWN(slowOk, obj_energysaving["enabled"], energySavingStop != 0);   // see manual §20
    if (slowOk && energySavingStop != 0)  obj_energysaving["stopTemperature"]  = setPointTemp + energySavingStop;
    if (slowOk && energySavingStart != 0) obj_energysaving["startTemperature"] = setPointTemp + energySavingStart;

    KNOWN(fastOk, json["RoomFanSpeed"], roomFanSpeed);
    {
        // What the room fan really does (the setting above is only the request)
        JsonObject rf = json.createNestedObject("roomFan");
        KNOWN(fastOk, rf["speed"], roomFanSpeed);
        int act = roomFanActual();
        if (act >= 0) rf["actual"] = act; else rf["actual"] = nullptr;
        int run = roomFanIsRunning();
        if (run >= 0) rf["running"] = run == 1; else rf["running"] = nullptr;
        const char* mode = roomFanMode();   // off / on (power from RAM 0x0A)
        if (mode) rf["mode"] = mode; else rf["mode"] = nullptr;
        if (cachedHotForFan >= 0) rf["hotEnough"] = cachedHotForFan != 0; else rf["hotEnough"] = nullptr;
        if (cachedRoomFanControl >= 0) rf["drive"] = cachedRoomFanControl; else rf["drive"] = nullptr;
    }
    {
        // Minutes to the next brazier cleaning: only counts while the stove is working
        // (state 4); null otherwise. 0 while the cleaning itself runs (state 5).
        JsonObject bc = json.createNestedObject("brazierCleaning");
        if (stoveState == 4 && cachedBrazierCountdown >= 0) bc["minutesLeft"] = cachedBrazierCountdown; else bc["minutesLeft"] = nullptr;
        if (stoveState == UNKNOWN) bc["active"] = nullptr; else bc["active"] = stoveState == 5;   // unknown without the stove
        if (cachedBrazierInterval > 0) bc["interval"] = cachedBrazierInterval; else bc["interval"] = nullptr;
    }
    KNOWN(slowOk, json["lowPalletAlarm"], cachedLowPallet);
    {
        JsonObject pr = json.createNestedObject("pelletReserve");
        if (cachedPelletReserveOn >= 0) pr["enabled"] = cachedPelletReserveOn != 0; else pr["enabled"] = nullptr;
        if (cachedPelletSensor >= 0) pr["sensor"] = cachedPelletSensor; else pr["sensor"] = nullptr;     // RAM 0xA4 (5 = active)
        if (cachedPelletCounter >= 0) pr["counter"] = cachedPelletCounter; else pr["counter"] = nullptr; // RAM 0x89
        // RAM 0x89 counts down 20 -> 0, about 1 per minute, from the moment the level sensor
        // (RAM 0xA4) reports low pellets (seen 17/11/2026): minutes until the reserve alarm
        if (slowOk && cachedPelletSensor == 5 && cachedPelletCounter >= 0) pr["minutesLeft"] = cachedPelletCounter; else pr["minutesLeft"] = nullptr;
        // phase 2: RAM 0x91 counts half minutes until the stove switches off
        if (slowOk && cachedPelletShutdown >= 0) pr["shutdownMinutes"] = (cachedPelletShutdown + 1) / 2; else pr["shutdownMinutes"] = nullptr;
    }
    KNOWN(slowOk, json["stoveBeepEnabled"], cachedBeepEnabled);
    KNOWN(slowOk, json["numberOfStarts"], cachedNumberOfStarts);
    KNOWN(slowOk, json["hoursSinceLastService"], cachedHoursSinceLastService);
    KNOWN(slowOk, json["totalRuntime"], cachedTotalRuntime);
    KNOWN(slowOk, json["firmwareVersion"], cachedFirmwareVersion);
    if (slowOk) json["augerSpeed"] = serialized(String(cachedAugerSpeed, 1)); else json["augerSpeed"] = nullptr;
    JsonObject obj_burn = json.createNestedObject("burn");
    const char* cqLabel = getCombustionQualityLabel(cachedCombustionQuality);
    const char* pqLabel = getPelletQualityLabel(cachedPelletQuality);
    if (slowOk) { obj_burn["combustionQuality"] = cqLabel ? cqLabel : "Unknown"; obj_burn["pelletQuality"] = pqLabel ? pqLabel : "Unknown"; }
    else { obj_burn["combustionQuality"] = nullptr; obj_burn["pelletQuality"] = nullptr; }
    KNOWN(fastOk, json["fumeFanRPM"], cachedFumeFanRPM);
    if (fastOk && cachedRemoteControlLastSeen >= 0) json["RemoteControlLastSeen"] = cachedRemoteControlLastSeen; else json["RemoteControlLastSeen"] = nullptr;

    const char* memKeys[5] = {"MemoryPos_1","MemoryPos_2","MemoryPos_3","MemoryPos_4","MemoryPos_5"};
    bool hasErrors = false;
    for (int i = 0; i < 5; i++) {
        if (cachedErrors[i] != 0) { hasErrors = true; break; }
    }
    if (!slowOk) {
        json["previousErrors"] = nullptr;   // error memory not read yet
    } else if (!hasErrors) {
        json["previousErrors"] = false;
    } else {
        JsonObject obj_errors = json.createNestedObject("previousErrors");
        for (int i = 0; i < 5; i++) {
            if (cachedErrors[i] != 0) {
                JsonObject eb = obj_errors.createNestedObject(memKeys[i]);
                eb["id"]    = cachedErrors[i];
                eb["error"] = getErrorCode(cachedErrors[i]);
                eb["desc"]  = getErrorDesc(cachedErrors[i]);
            }
        }
    }

    char time_output[30];
    JsonObject time = json.createNestedObject("time");
    time_t st = stoveTimeNow(), nt = ntpTimeNow();
    strftime(time_output, 30, "%F %T", localtime(&nt));
    time["ntp"] = time_output;
    if (stoveTimeReadMs) {
        strftime(time_output, 30, "%F %T", localtime(&st));
        time["stove"] = time_output;
        time["delta"] = nt - st;
    } else { time["stove"] = nullptr; time["delta"] = nullptr; }   // stove clock not read yet

    // How fresh is this cached data? Lets clients detect a stalled sensor task / bus.
    JsonObject obj_age = json.createNestedObject("dataAge");
    unsigned long nowMs = millis();
    if (lastFastStatesAt) obj_age["fastSeconds"] = (nowMs - lastFastStatesAt) / 1000; else obj_age["fastSeconds"] = nullptr;
    if (lastSlowStatesAt) obj_age["slowSeconds"] = (nowMs - lastSlowStatesAt) / 1000; else obj_age["slowSeconds"] = nullptr;

    JsonObject obj_serial = json.createNestedObject("serial");
    obj_serial["readErrors"]  = stove.readErrors;
    obj_serial["writeErrors"] = stove.writeErrors;
    obj_serial["retries"]     = stove.retries;
    obj_serial["linkUp"]      = !stove.linkDown;     // false: the stove does not answer, bus commands fail at once
    obj_serial["skipped"]     = stove.skipped;       // commands refused while the link was down
    obj_serial["transactions"] = stove.transactions;

    JsonObject obj_uptime = json.createNestedObject("uptime");
    obj_uptime["seconds"] = millis() / 1000;
    obj_uptime["human"]   = uptimeStr();

    // Controller (ESP32) firmware build, from scripts/buildinfo.py
    JsonObject obj_ctrl = json.createNestedObject("controller");
    obj_ctrl["version"] = BUILD_VERSION;
    obj_ctrl["built"]   = BUILD_TIME;
    obj_ctrl["branch"]  = BUILD_BRANCH;
    obj_ctrl["env"]     = BUILD_ENV;
    obj_ctrl["stoveModel"] = BUILD_STOVE_MODEL;
    obj_ctrl["resetReason"] = diagResetText();      // why the controller last (re)started
    obj_ctrl["bootCount"]   = diagBoots;
    obj_ctrl["crashCount"]  = diagCrashes;          // starts after a crash, watchdog or brown-out

#undef KNOWN
    if (json.overflowed()) LOGE("http", "getStatus: JSON document too small");
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
    } else{
        wm.server->send(405);
    }
}
