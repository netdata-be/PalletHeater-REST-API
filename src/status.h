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

    JsonObject obj_stove = json.createNestedObject("stoveState");
    obj_stove["stoveState"] = stoveState;
    obj_stove["state"] = StoveStateStr;
    obj_stove["poweredOn"] = StoveIsOn;

    if (int_hydro_mode == 1)
    {
        JsonObject hydro = json.createNestedObject("hydro");
        hydro["Pressure"] = waterPres;
        hydro["Temp"] = waterTemp;
    }

    JsonObject obj_flame = json.createNestedObject("flame");
    obj_flame["power"] = flamePower;                                   // setting
    if (cachedFlameActual >= 0) obj_flame["actual"] = cachedFlameActual; // what the stove runs at now
    json["bluetooth"] = bleEnabled;
    json["telnet"] = telnetEnabled;
    json["apiKeyRequired"] = apiKeyRequired;

    JsonObject obj_temperatures = json.createNestedObject("temperatures");
    obj_temperatures["room"] = ambTemp;
    obj_temperatures["stove"] = ambTempStove;
    obj_temperatures["remoteControl"] = ambTempRemoteControl;
    obj_temperatures["setPoint"] = setPointTemp;
    obj_temperatures["fume"] = fumesTemp;
    json["wirelessSensor"] = wirelessSensorStr();
    if (confirmedCurrentError == 0) {
        json["currentError"] = false;
    } else {
        JsonObject ce = json.createNestedObject("currentError");
        ce["id"]    = confirmedCurrentError;
        ce["error"] = getErrorCode(confirmedCurrentError);
        ce["desc"]  = getErrorDesc(confirmedCurrentError);
    }

    JsonObject obj_energysaving = json.createNestedObject("energySaving");
    obj_energysaving["start"] = energySavingStart;
    obj_energysaving["stop"] = energySavingStop;
    obj_energysaving["enabled"] = energySavingStop != 0;   // see manual §20
    if (energySavingStop != 0)  obj_energysaving["stopTemperature"]  = setPointTemp + energySavingStop;
    if (energySavingStart != 0) obj_energysaving["startTemperature"] = setPointTemp + energySavingStart;

    json["RoomFanSpeed"] = roomFanSpeed;
    {
        // What the room fan really does (the setting above is only the request)
        JsonObject rf = json.createNestedObject("roomFan");
        rf["speed"] = roomFanSpeed;
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
        bc["active"] = stoveState == 5;
        if (cachedBrazierInterval > 0) bc["interval"] = cachedBrazierInterval; else bc["interval"] = nullptr;
    }
    json["lowPalletAlarm"] = cachedLowPallet;
    {
        JsonObject pr = json.createNestedObject("pelletReserve");
        if (cachedPelletReserveOn >= 0) pr["enabled"] = cachedPelletReserveOn != 0; else pr["enabled"] = nullptr;
        if (cachedPelletSensor >= 0) pr["sensor"] = cachedPelletSensor; else pr["sensor"] = nullptr;     // RAM 0xA4 (5 = active)
        if (cachedPelletCounter >= 0) pr["counter"] = cachedPelletCounter; else pr["counter"] = nullptr; // RAM 0x89
    }
    json["stoveBeepEnabled"] = cachedBeepEnabled;
    json["numberOfStarts"] = cachedNumberOfStarts;
    json["hoursSinceLastService"] = cachedHoursSinceLastService;
    json["totalRuntime"] = cachedTotalRuntime;
    json["firmwareVersion"] = cachedFirmwareVersion;
    json["augerSpeed"] = serialized(String(cachedAugerSpeed, 1));
    JsonObject obj_burn = json.createNestedObject("burn");
    const char* cqLabel = getCombustionQualityLabel(cachedCombustionQuality);
    const char* pqLabel = getPelletQualityLabel(cachedPelletQuality);
    obj_burn["combustionQuality"] = cqLabel ? cqLabel : "Unknown";
    obj_burn["pelletQuality"]     = pqLabel ? pqLabel : "Unknown";
    json["fumeFanRPM"] = cachedFumeFanRPM;
    json["RemoteControlLastSeen"] = cachedRemoteControlLastSeen;

    const char* memKeys[5] = {"MemoryPos_1","MemoryPos_2","MemoryPos_3","MemoryPos_4","MemoryPos_5"};
    bool hasErrors = false;
    for (int i = 0; i < 5; i++) {
        if (cachedErrors[i] != 0) { hasErrors = true; break; }
    }
    if (!hasErrors) {
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
    strftime(time_output, 30, "%F %T", localtime(&st));
    time["stove"] = time_output;
    time["delta"] = nt - st;

    // How fresh is this cached data? Lets clients detect a stalled sensor task / bus.
    JsonObject obj_age = json.createNestedObject("dataAge");
    unsigned long nowMs = millis();
    if (lastFastStatesAt) obj_age["fastSeconds"] = (nowMs - lastFastStatesAt) / 1000; else obj_age["fastSeconds"] = nullptr;
    if (lastSlowStatesAt) obj_age["slowSeconds"] = (nowMs - lastSlowStatesAt) / 1000; else obj_age["slowSeconds"] = nullptr;

    JsonObject obj_serial = json.createNestedObject("serial");
    obj_serial["readErrors"]  = stove.readErrors;
    obj_serial["writeErrors"] = stove.writeErrors;
    obj_serial["retries"]     = stove.retries;

    JsonObject obj_uptime = json.createNestedObject("uptime");
    obj_uptime["seconds"] = millis() / 1000;
    obj_uptime["human"]   = uptimeStr();

    // Controller (ESP32) firmware build, from scripts/buildinfo.py
    JsonObject obj_ctrl = json.createNestedObject("controller");
    obj_ctrl["version"] = BUILD_VERSION;
    obj_ctrl["built"]   = BUILD_TIME;
    obj_ctrl["branch"]  = BUILD_BRANCH;
    obj_ctrl["env"]     = BUILD_ENV;

    if (json.overflowed()) log_e("getStatus: JSON document too small");
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
    } else{
        wm.server->send(405);
    }
}
