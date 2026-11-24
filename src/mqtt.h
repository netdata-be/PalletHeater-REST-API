#pragma once
// MQTT: publishes the cached stove values and accepts commands, in both
// directions. Optional Home Assistant MQTT discovery.
//
// Topics (base = prefix, default "openpelletbridge"):
//   <base>/status                 online / offline (retained, last will)
//   <base>/state/<name>           plain values, retained, e.g. room_temp = 21.5,
//                                 power = ON / OFF. Published on change (checked
//                                 every 5 s) and all again every 5 min.
//   <base>/set/<name>             commands: power, mode, setpoint, flame_power,
//                                 fan_speed, energy_saving, es_stop, es_start
//
// Values are plain text (no JSON), so any MQTT client can use them, e.g. Loxone
// (through an MQTT gateway such as LoxBerry) or Node-RED. Commands accept ON / OFF
// as well as 1 / 0. Only the discovery messages under <discovery prefix>/... are
// specific to Home Assistant; they can be switched off.
//
// Settings in NVS ("mqtt"), changed via GET / POST /api/mqtt (and the web app).
#include <constants.h>
#include <PubSubClient.h>
#include <Preferences.h>

struct MqttConfig {
    bool   enabled = false;
    String host;
    uint16_t port = 1883;
    String user;
    String pass;
    String prefix = "openpelletbridge";
    bool   discovery = true;
    String discoveryPrefix = "homeassistant";
};

static MqttConfig   mqttCfg;
static WiFiClient   mqttNet;
static PubSubClient mqtt(mqttNet);
static String       mqttClientId;
static String       mqttLastError;
static unsigned long mqttLastAttempt = 0, mqttLastFull = 0, mqttLastCheck = 0;
static uint32_t     mqttCommands = 0, mqttPublishes = 0;
static bool         mqttNeedFull = true;
static int          mqttEsLastStop = 1, mqttEsLastStart = 1;   // restored by energy_saving ON

static const int MQTT_MAX_STATES = 32;
static String mqttStateKeys[MQTT_MAX_STATES];
static String mqttStateVals[MQTT_MAX_STATES];
static int    mqttStateCount = 0;

static void mqttLoadConfig()
{
    Preferences p;
    p.begin("mqtt", true);
    mqttCfg.enabled         = p.getBool("enabled", false);
    mqttCfg.host            = p.getString("host", "");
    mqttCfg.port            = p.getUShort("port", 1883);
    mqttCfg.user            = p.getString("user", "");
    mqttCfg.pass            = p.getString("pass", "");
    mqttCfg.prefix          = p.getString("prefix", "openpelletbridge");
    mqttCfg.discovery       = p.getBool("disc", true);
    mqttCfg.discoveryPrefix = p.getString("discpre", "homeassistant");
    p.end();
}

static void mqttSaveConfig()
{
    Preferences p;
    p.begin("mqtt", false);
    p.putBool("enabled", mqttCfg.enabled);
    p.putString("host", mqttCfg.host);
    p.putUShort("port", mqttCfg.port);
    p.putString("user", mqttCfg.user);
    p.putString("pass", mqttCfg.pass);
    p.putString("prefix", mqttCfg.prefix);
    p.putBool("disc", mqttCfg.discovery);
    p.putString("discpre", mqttCfg.discoveryPrefix);
    p.end();
}

static String mqttTopic(const char* kind, const char* name) { return mqttCfg.prefix + "/" + kind + "/" + name; }

// PubSubClient state codes in words
static const char* mqttStateText(int st)
{
    switch (st) {
    case -4: return "no answer from the broker (timeout)";
    case -3: return "connection lost";
    case -2: return "broker not reachable (network / host / port)";
    case -1: return "disconnected";
    case 0:  return "connected";
    case 1:  return "broker refused: protocol version";
    case 2:  return "broker refused the client id";
    case 3:  return "broker unavailable";
    case 4:  return "broker refused: wrong user name or password";
    case 5:  return "broker refused: not authorised";
    default: return "unknown state";
    }
}
static int mqttDiscoveryCount = 0;
static bool mqttPub(const String& topic, const String& payload, bool retain = true)
{
    if (!mqtt.connected()) return false;
    bool ok = mqtt.publish(topic.c_str(), payload.c_str(), retain);
    if (ok) mqttPublishes++;
    LogModule* lm = logModule("mqtt");
    if (lm && lm->level >= LOG_DEBUG) {
        if (topic.startsWith(mqttCfg.discoveryPrefix + "/")) LOGD("mqtt", "publish %s (discovery, %u bytes)%s", topic.c_str(), payload.length(), ok ? "" : " FAILED");
        else LOGD("mqtt", "publish %s = %s%s", topic.c_str(), payload.c_str(), ok ? "" : " FAILED");
    }
    if (!ok) LOGW("mqtt", "publish to %s failed (%u bytes, buffer 1024)", topic.c_str(), payload.length());
    return ok;
}

// Publish a state value when it changed since the last publish (or always with force)
static void mqttState(const char* name, const String& value, bool force)
{
    int i = 0;
    for (; i < mqttStateCount; i++) if (mqttStateKeys[i] == name) break;
    if (i == mqttStateCount && mqttStateCount < MQTT_MAX_STATES) { mqttStateKeys[i] = name; mqttStateVals[i] = "\x01"; mqttStateCount++; }
    if (i < MQTT_MAX_STATES && !force && mqttStateVals[i] == value) return;
    if (mqttPub(mqttTopic("state", name), value) && i < MQTT_MAX_STATES) mqttStateVals[i] = value;
}

static String onOff(bool b) { return b ? "ON" : "OFF"; }
static String num1(float v) { return String(v, 1); }

static void mqttPublishStates(bool force)
{
    bool stoveOk = stoveState >= 0;
    mqttState("state",          StoveStateStr, force);
    mqttState("state_code",     String(stoveState), force);
    if (stoveState != UNKNOWN) {   // on / off unknown while the stove does not answer: keep the last value
        mqttState("power",      onOff(StoveIsOn), force);
        mqttState("mode",       StoveIsOn ? "heat" : "off", force);   // Home Assistant climate
    }
    if (stoveState == UNKNOWN) {   // no answer from the stove: a controller-side error
        mqttState("error_active", "ON", force);
        mqttState("error",        "LINK No answer from the stove (serial link down)", force);
    }
    // values never read from the stove since the start are not published (no initial 0s)
    if (!lastFastStatesAt || !lastSlowStatesAt) { mqttState("wifi_rssi", String(WiFi.RSSI()), force); return; }
    if (stoveOk) {
        mqttState("room_temp",   num1(ambTemp), force);
        mqttState("stove_temp",  num1(ambTempStove), force);
        mqttState("remote_temp", num1(ambTempRemoteControl), force);
        mqttState("fume_temp",   String((int)fumesTemp), force);
    }
    mqttState("setpoint",       String((int)setPointTemp), force);
    mqttState("flame_power",    String(flamePower), force);
    if (cachedFlameActual >= 0) mqttState("flame_actual", String(cachedFlameActual), force);
    mqttState("fan_speed",      String(roomFanSpeed), force);
    int run = roomFanIsRunning();
    if (run >= 0) mqttState("fan_running", onOff(run == 1), force);
    if (cachedRoomFanControl >= 0) mqttState("fan_power", String(cachedRoomFanControl), force);
    mqttState("energy_saving",  onOff(energySavingStop != 0), force);
    mqttState("es_stop",        String(energySavingStop), force);
    mqttState("es_start",       String(-energySavingStart), force);   // degrees below the set point, 0 = no restart
    mqttState("fume_fan_rpm",   String(cachedFumeFanRPM), force);
    mqttState("auger",          String(cachedAugerSpeed, 1), force);                 // on-time per ~12 s cycle (s)
    mqttState("auger_percent",  String(pc::augerPercent(cachedAugerSpeed)), force);  // share of the time it runs
    mqttState("brazier_minutes", stoveState == 4 && cachedBrazierCountdown >= 0 ? String(cachedBrazierCountdown) : String(""), force);
    if (stoveState != UNKNOWN) {
        bool empty = !confirmedCurrentError && pelletsEmptyOff();   // the controller's own error (stoveCache.h)
        mqttState("error_active",   onOff(confirmedCurrentError != 0 || empty), force);
        mqttState("error",          confirmedCurrentError ? getErrorCode(confirmedCurrentError) + " " + getErrorDesc(confirmedCurrentError)
                                    : empty ? String("PELLETS ") + PELLETS_EMPTY_DESC : String("none"), force);
    }
    mqttState("low_pellets",    onOff(cachedLowPallet != 0), force);
    // warning (not an error) and the sensor the stove regulates on (stoveCache.h)
    {
        String w = remoteSensorWarningText();
        mqttState("warning_active", onOff(w.length() > 0), force);
        mqttState("warning",        w.length() ? String("REMOTE_SENSOR_NOT_USED ") + w : String("none"), force);
        mqttState("room_source",    cachedRoomSource == pc::RS_REMOTE ? "remote" : cachedRoomSource == pc::RS_STOVE ? "stove" : "", force);
    }
    {
        // estimate until the low pellet alarm (pellets.h); empty = not known yet
        double rem = pellets.remainingKg(), h = pellets.hoursAt(pelletsAvgAuger());
        mqttState("pellets_kg",    rem >= 0 ? String(rem, 1) : String(""), force);
        mqttState("pellets_hours", h >= 0 ? String(h, 1) : String(""), force);
        // use per day and cost (pellets.h); empty = not known (no calibration / no price / no stock)
        double today = pelKgToday(), perDay = pelKgPerDay7(), month = pelCostPerMonth(), stock = pelStockBags();
        mqttState("pellets_kg_today",   today >= 0 ? String(today, 1) : String(""), force);
        mqttState("pellets_kg_per_day", perDay >= 0 ? String(perDay, 1) : String(""), force);
        mqttState("pellets_cost_month", month >= 0 ? String(month, 0) : String(""), force);
        mqttState("pellets_stock_bags", stock >= 0 ? String(stock, 1) : String(""), force);
    }
    mqttState("hours_service",  String(cachedHoursSinceLastService), force);
    mqttState("total_hours",    String(cachedTotalRuntime), force);
    mqttState("starts",         String(cachedNumberOfStarts), force);
    mqttState("wifi_rssi",      String(WiFi.RSSI()), force);
}

// ── Home Assistant discovery ────────────────────────────────────────────────
static void mqttDiscoveryEntity(const char* component, const char* id, const char* name,
                                void (*fill)(JsonObject&), bool hasCommand, const char* stateName = nullptr)
{
    mqttDiscoveryCount++;
    DynamicJsonDocument d(1024);
    JsonObject o = d.to<JsonObject>();
    o["name"] = name;
    o["unique_id"] = mqttClientId + "_" + id;
    o["availability_topic"] = mqttCfg.prefix + "/status";
    if (strcmp(component, "climate") != 0) {
        o["state_topic"] = mqttTopic("state", stateName ? stateName : id);
        if (hasCommand) o["command_topic"] = mqttTopic("set", id);
    }
    JsonObject dev = o.createNestedObject("device");
    dev.createNestedArray("identifiers").add(mqttClientId);
    dev["name"] = "OpenPelletBridge";
    dev["manufacturer"] = "Micronova (OpenPelletBridge controller)";
    dev["model"] = BUILD_STOVE_MODEL;                 // the stove this firmware is built for
    if (cachedFirmwareVersion[0] && strcmp(cachedFirmwareVersion, "Unknown") != 0) dev["hw_version"] = cachedFirmwareVersion;   // firmware of the stove board
    if (cachedSerialNumber >= 0) dev["serial_number"] = String(cachedSerialNumber);   // MATR of the stove
    dev["sw_version"] = BUILD_VERSION;
    dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/app";
    if (fill) fill(o);
    String topic = mqttCfg.discoveryPrefix + "/" + component + "/" + mqttClientId + "/" + id + "/config";
    String payload; serializeJson(d, payload);
    mqttPub(topic, payload, true);
}

static void mqttPublishDiscovery()
{
    if (!mqttCfg.discovery) return;
    const char* sensor = "sensor";
    auto temp = [](JsonObject& o) { o["device_class"] = "temperature"; o["unit_of_measurement"] = "°C"; o["state_class"] = "measurement"; };
    mqttDiscoveryEntity("climate", "climate", "Kachel", [](JsonObject& o) {
        String b = mqttCfg.prefix;
        o["mode_state_topic"] = b + "/state/mode";
        o["mode_command_topic"] = b + "/set/mode";
        JsonArray m = o.createNestedArray("modes"); m.add("off"); m.add("heat");
        o["temperature_state_topic"] = b + "/state/setpoint";
        o["temperature_command_topic"] = b + "/set/setpoint";
        o["current_temperature_topic"] = b + "/state/room_temp";
        o["min_temp"] = 7; o["max_temp"] = 30; o["temp_step"] = 1; o["precision"] = 0.5;
        o["temperature_unit"] = "C";
    }, false);
    mqttDiscoveryEntity("switch", "power", "Aan / uit", nullptr, true);
    mqttDiscoveryEntity("number", "setpoint", "Gewenste temperatuur", [](JsonObject& o) {
        o["min"] = 7; o["max"] = 30; o["step"] = 1; o["unit_of_measurement"] = "°C"; o["mode"] = "box"; }, true);
    mqttDiscoveryEntity("number", "flame_power", "Vlamvermogen", [](JsonObject& o) {
        o["min"] = 1; o["max"] = 4; o["step"] = 1; o["mode"] = "slider"; }, true);
    mqttDiscoveryEntity("select", "fan_speed", "Ventilator", [](JsonObject& o) {
        JsonArray a = o.createNestedArray("options"); a.add("off"); a.add("1"); a.add("2"); a.add("3");
        o["value_template"] = "{{ 'off' if value == '0' else value }}";
        o["command_template"] = "{{ '0' if value == 'off' else value }}"; }, true);
    mqttDiscoveryEntity("switch", "energy_saving", "Energy saving", nullptr, true);
    mqttDiscoveryEntity("number", "es_stop", "Energy saving stop boven gewenste temp.", [](JsonObject& o) {
        o["min"] = 0; o["max"] = 3; o["step"] = 1; o["unit_of_measurement"] = "°C"; o["entity_category"] = "config"; }, true);
    mqttDiscoveryEntity("number", "es_start", "Energy saving herstart onder gewenste temp.", [](JsonObject& o) {
        o["min"] = 0; o["max"] = 3; o["step"] = 1; o["unit_of_measurement"] = "°C"; o["entity_category"] = "config"; }, true);
    mqttDiscoveryEntity(sensor, "state", "Toestand", nullptr, false);
    mqttDiscoveryEntity(sensor, "room_temp", "Kamertemperatuur", temp, false);
    mqttDiscoveryEntity(sensor, "stove_temp", "Sensor kachel", temp, false);
    mqttDiscoveryEntity(sensor, "remote_temp", "Sensor afstandsbediening", temp, false);
    mqttDiscoveryEntity(sensor, "fume_temp", "Rookgastemperatuur", temp, false);
    mqttDiscoveryEntity(sensor, "flame_actual", "Werkelijk vlamvermogen", [](JsonObject& o) { o["state_class"] = "measurement"; }, false);
    mqttDiscoveryEntity(sensor, "fume_fan_rpm", "Rookgasventilator", [](JsonObject& o) { o["unit_of_measurement"] = "rpm"; o["state_class"] = "measurement"; }, false);
    mqttDiscoveryEntity(sensor, "auger", "Schroef aan-tijd", [](JsonObject& o) { o["unit_of_measurement"] = "s"; o["state_class"] = "measurement"; o["entity_category"] = "diagnostic"; }, false);
    mqttDiscoveryEntity(sensor, "auger_percent", "Schroef", [](JsonObject& o) { o["unit_of_measurement"] = "%"; o["state_class"] = "measurement"; }, false);
    mqttDiscoveryEntity(sensor, "brazier_minutes", "Branderreiniging over", [](JsonObject& o) { o["unit_of_measurement"] = "min"; o["device_class"] = "duration"; }, false);
    mqttDiscoveryEntity(sensor, "error", "Foutmelding", nullptr, false);
    mqttDiscoveryEntity(sensor, "hours_service", "Uren sinds onderhoud", [](JsonObject& o) { o["unit_of_measurement"] = "h"; o["device_class"] = "duration"; o["entity_category"] = "diagnostic"; }, false);
    mqttDiscoveryEntity(sensor, "total_hours", "Werkuren", [](JsonObject& o) { o["unit_of_measurement"] = "h"; o["device_class"] = "duration"; o["state_class"] = "total_increasing"; o["entity_category"] = "diagnostic"; }, false);
    mqttDiscoveryEntity(sensor, "starts", "Aantal starts", [](JsonObject& o) { o["state_class"] = "total_increasing"; o["entity_category"] = "diagnostic"; }, false);
    mqttDiscoveryEntity(sensor, "wifi_rssi", "WiFi-signaal", [](JsonObject& o) { o["unit_of_measurement"] = "dBm"; o["device_class"] = "signal_strength"; o["entity_category"] = "diagnostic"; }, false);
    mqttDiscoveryEntity("binary_sensor", "fan_running", "Ventilator draait", [](JsonObject& o) { o["device_class"] = "running"; }, false);
    mqttDiscoveryEntity("binary_sensor", "error_active", "Fout actief", [](JsonObject& o) { o["device_class"] = "problem"; }, false);
    mqttDiscoveryEntity("binary_sensor", "low_pellets", "Pellets bijna op", [](JsonObject& o) { o["device_class"] = "problem"; }, false);
    mqttDiscoveryEntity("binary_sensor", "warning_active", "Waarschuwing actief", [](JsonObject& o) { o["device_class"] = "problem"; }, false);
    mqttDiscoveryEntity(sensor, "warning", "Waarschuwing", nullptr, false);
    mqttDiscoveryEntity(sensor, "room_source", "Bron kamertemperatuur", [](JsonObject& o) { o["icon"] = "mdi:thermometer-lines"; }, false);
    mqttDiscoveryEntity(sensor, "pellets_kg", "Pellets tot alarm", [](JsonObject& o) { o["unit_of_measurement"] = "kg"; o["device_class"] = "weight"; o["state_class"] = "measurement"; }, false);
    mqttDiscoveryEntity(sensor, "pellets_hours", "Branduren tot pelletalarm", [](JsonObject& o) { o["unit_of_measurement"] = "h"; o["device_class"] = "duration"; o["state_class"] = "measurement"; }, false);
    mqttDiscoveryEntity(sensor, "pellets_kg_today", "Pelletverbruik vandaag", [](JsonObject& o) { o["unit_of_measurement"] = "kg"; o["device_class"] = "weight"; o["state_class"] = "total_increasing"; }, false);
    mqttDiscoveryEntity(sensor, "pellets_kg_per_day", "Pelletverbruik per dag (7 d)", [](JsonObject& o) { o["unit_of_measurement"] = "kg"; o["device_class"] = "weight"; o["state_class"] = "measurement"; }, false);
    mqttDiscoveryEntity(sensor, "pellets_cost_month", "Pelletkost per maand", [](JsonObject& o) { o["unit_of_measurement"] = "EUR"; o["device_class"] = "monetary"; }, false);
    mqttDiscoveryEntity(sensor, "pellets_stock_bags", "Pelletvoorraad (zakken)", [](JsonObject& o) { o["unit_of_measurement"] = "zakken"; o["state_class"] = "measurement"; o["icon"] = "mdi:sack"; }, false);
}

// ── Commands ────────────────────────────────────────────────────────────────
static void mqttCommand(const String& name, const char* payload)
{
    mqttCommands++;
    StoveBusLock lock;   // same lock as the HTTP handlers and the sensor task
    CmdSource cs((String("MQTT command ") + name).c_str());
    int v = 0, code = 0;
    bool ok = false;
    String result;
    if (name == "power" || name == "mode") {
        int on = name == "mode" ? (strcmp(payload, "heat") == 0 ? 1 : strcmp(payload, "off") == 0 ? 0 : pc::parseOnOff(payload))
                                : pc::parseOnOff(payload);
        if (on < 0) result = "invalid payload";
        else { result = on ? doStoveOn(code) : doStoveOff(code); ok = code < 300; getStoveState(); }
    } else if (name == "setpoint") {
        if (!pc::parseIntRange(payload, 7, 30, v)) result = "must be 7 - 30";
        else if ((ok = stove.write(eeprom, TempSetpointAddr, (byte)v))) { setPointTemp = v; getTempSetpoint(); }
    } else if (name == "flame_power") {
        if (!pc::parseIntRange(payload, 1, 4, v)) result = "must be 1 - 4";
        else if ((ok = setFlamePower(v))) getFlamePower();
    } else if (name == "fan_speed") {
        if (!pc::parseFanSpeed(payload, v)) result = "must be off / 0 - 3";
        else if ((ok = setRoomFanSpeed(v))) getRoomFanSpeed();
    } else if (name == "energy_saving") {
        int on = pc::parseOnOff(payload);
        if (on < 0) result = "invalid payload";
        else if (on) {   // restore the last offsets (or +1 / -1)
            ok = setEnergySavingStop(energySavingStop ? energySavingStop : mqttEsLastStop);
            if (ok && energySavingStart == 0) ok = setEnergySavingStart(mqttEsLastStart);
            getEnergySaving();
        } else {         // the stove has no on/off flag: off = both offsets 0 (as in the web app)
            ok = setEnergySavingStop(0) && setEnergySavingStart(0);
            getEnergySaving();
        }
    } else if (name == "es_stop" || name == "es_start") {
        if (!pc::parseIntRange(payload, 0, 3, v)) result = "must be 0 - 3";
        else { ok = name == "es_stop" ? setEnergySavingStop(v) : setEnergySavingStart(v); getEnergySaving(); }
    } else result = "unknown command";
    if (!ok && !result.length()) result = "RS232 error";
    if (ok) LOGI("mqtt", "command %s = %s: ok", name.c_str(), payload); else LOGE("mqtt", "command %s = %s failed: %s", name.c_str(), payload, result.c_str());
    mqttPub(mqttCfg.prefix + "/result", name + " " + payload + ": " + (ok ? String("ok") : result), false);
    mqttPublishStates(false);
}

static void mqttCallback(char* topic, byte* payload, unsigned int len)
{
    String t(topic);
    String setBase = mqttCfg.prefix + "/set/";
    if (!t.startsWith(setBase)) return;
    char buf[32];
    unsigned int n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n); buf[n] = 0;
    mqttCommand(t.substring(setBase.length()), buf);
}

static void mqttConnect()
{
    // 1 KB buffer (discovery messages are ~600 bytes), only once MQTT is used
    if (mqtt.getBufferSize() < 1024) mqtt.setBufferSize(1024);
    mqtt.setServer(mqttCfg.host.c_str(), mqttCfg.port);
    String will = mqttCfg.prefix + "/status";
    bool ok = mqttCfg.user.length()
        ? mqtt.connect(mqttClientId.c_str(), mqttCfg.user.c_str(), mqttCfg.pass.c_str(), will.c_str(), 1, true, "offline")
        : mqtt.connect(mqttClientId.c_str(), will.c_str(), 1, true, "offline");
    if (!ok) {
        String e = String("connect failed: ") + mqttStateText(mqtt.state()) + " (state " + mqtt.state() + ")";
        if (e != mqttLastError)   // once, not every retry; refused credentials will not fix themselves
            logPrintf(mqtt.state() == 4 || mqtt.state() == 5 ? LOG_ERROR : LOG_WARN, "mqtt", "%s:%u: %s", mqttCfg.host.c_str(), mqttCfg.port, e.c_str());
        mqttLastError = e;
        return;
    }
    mqttLastError = "";
    mqttPub(will, "online", true);
    mqtt.subscribe((mqttCfg.prefix + "/set/#").c_str());
    LOGI("mqtt", "listening for commands on %s/set/#", mqttCfg.prefix.c_str());
    mqttDiscoveryCount = 0;
    mqttPublishDiscovery();
    if (mqttCfg.discovery) LOGI("mqtt", "Home Assistant discovery published: %d entities under %s/", mqttDiscoveryCount, mqttCfg.discoveryPrefix.c_str());
    mqttNeedFull = true;
    LOGI("mqtt", "connected to %s:%u as %s", mqttCfg.host.c_str(), mqttCfg.port, mqttClientId.c_str());
}

void mqttSetup()
{
    mqttLoadConfig();
    uint64_t mac = ESP.getEfuseMac();
    char id[32]; snprintf(id, sizeof(id), "openpelletbridge_%06x", (uint32_t)(mac >> 24) & 0xFFFFFF);
    mqttClientId = id;
    mqtt.setSocketTimeout(2);     // do not block the loop long when the broker is down
    mqtt.setKeepAlive(30);
    mqtt.setCallback(mqttCallback);
}

static void mqttRestart()
{
    if (mqtt.connected()) { mqttPub(mqttCfg.prefix + "/status", "offline", true); mqtt.disconnect(); }
    mqttLastAttempt = 0; mqttStateCount = 0; mqttNeedFull = true;
}

void mqttLoop()
{
    static bool wasConnected = false;
    // firmware update: close the connection (its socket buffers count during a download from
    // GitHub, fwUpdate.h); reconnects by itself when the update failed or the device restarted
    if (otaActive && mqtt.connected()) {
        LOGI("mqtt", "firmware update running - disconnecting from %s:%u", mqttCfg.host.c_str(), mqttCfg.port);
        mqttRestart();
        wasConnected = false;
        return;
    }
    if (!mqttCfg.enabled || !mqttCfg.host.length() || WiFi.status() != WL_CONNECTED || otaActive) { wasConnected = mqtt.connected(); return; }
    unsigned long now = millis();
    if (wasConnected && !mqtt.connected()) LOGW("mqtt", "connection to %s:%u lost: %s - reconnecting every 15 s", mqttCfg.host.c_str(), mqttCfg.port, mqttStateText(mqtt.state()));
    wasConnected = mqtt.connected();
    if (!mqtt.connected()) {
        if (mqttLastAttempt && now - mqttLastAttempt < 15000) return;   // retry every 15 s
        mqttLastAttempt = now;
        mqttConnect();
        if (!mqtt.connected()) return;
    }
    mqtt.loop();
    {
        // the device info (stove firmware, serial number) is read after the first connect:
        // send the discovery again once it is known
        static bool devInfoSent = false;
        bool known = cachedSerialNumber >= 0 && strcmp(cachedFirmwareVersion, "Unknown") != 0;
        if (known && !devInfoSent) { devInfoSent = true; if (mqttDiscoveryCount) { mqttDiscoveryCount = 0; mqttPublishDiscovery(); } }
    }
    if (energySavingStop) mqttEsLastStop = energySavingStop;     // remembered for energy_saving ON
    if (energySavingStart) mqttEsLastStart = energySavingStart;
    if (mqttNeedFull || now - mqttLastFull >= 300000) {
        mqttPublishStates(true); mqttLastFull = now; mqttLastCheck = now; mqttNeedFull = false;
    } else if (now - mqttLastCheck >= 5000) {
        mqttPublishStates(false); mqttLastCheck = now;
    }
}

// GET  /api/mqtt  -> settings (without the password) and connection status
// POST /api/mqtt?enabled=&host=&port=&user=&password=&prefix=&discovery=&discoveryPrefix=
//      only the given fields change; password "" keeps the stored one, "-" clears it
// Check and apply a changed configuration (shared by /api/mqtt and the telnet menu):
// stored in NVS and the client reconnects. Returns an error text, "" = applied.
static String mqttApply(const MqttConfig& c, const char* who)
{
    if (c.enabled && !c.host.length()) return "host is required when MQTT is enabled";
    if (!c.prefix.length() || c.prefix.indexOf('#') >= 0 || c.prefix.indexOf('+') >= 0) return "prefix must not be empty or contain # / +";
    if (!c.discoveryPrefix.length()) return "discoveryPrefix must not be empty";
    if (mqttCfg.enabled && mqtt.connected()) mqttRestart();
    mqttCfg = c; mqttSaveConfig(); mqttRestart();
    LOGI("mqtt", "settings changed by %s: %s, broker %s:%u", who, c.enabled ? "on" : "off", c.host.c_str(), c.port);
    return "";
}

void handleRouteMqtt()
{
    StaticJsonDocument<768> json;
    int code = 200;
    if (wm.server->method() == HTTP_POST) {
        auto has = [](const char* k) { return wm.server->hasArg(k); };
        auto arg = [](const char* k) { String v = wm.server->arg(k); v.trim(); return v; };
        auto boolArg = [&](const char* k, bool& dst) { int b = pc::parseOnOff(arg(k).c_str()); if (b < 0) return false; dst = b == 1; return true; };
        MqttConfig c = mqttCfg;
        String err;
        if (has("enabled") && !boolArg("enabled", c.enabled)) err = "enabled must be true / false";
        if (has("discovery") && !boolArg("discovery", c.discovery)) err = "discovery must be true / false";
        if (has("host")) c.host = arg("host");
        if (has("port")) { int p = 0; if (!pc::parseIntRange(arg("port").c_str(), 1, 65535, p)) err = "port must be 1 - 65535"; else c.port = p; }
        if (has("user")) c.user = arg("user");
        if (has("password")) { String p = wm.server->arg("password"); if (p == "-") c.pass = ""; else if (p.length()) c.pass = p; }
        if (has("prefix")) { String p = arg("prefix"); while (p.endsWith("/")) p.remove(p.length() - 1);
                             if (!p.length() || p.indexOf('#') >= 0 || p.indexOf('+') >= 0) err = "prefix must not be empty or contain # / +"; else c.prefix = p; }
        if (has("discoveryPrefix")) { String p = arg("discoveryPrefix"); if (!p.length()) err = "discoveryPrefix must not be empty"; else c.discoveryPrefix = p; }
        if (!err.length()) err = mqttApply(c, clientIp().c_str());
        if (err.length()) { json["result"] = "failed - " + err; code = 400; }
        else json["result"] = "success";
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    json["enabled"] = mqttCfg.enabled;
    json["host"] = mqttCfg.host;
    json["port"] = mqttCfg.port;
    json["user"] = mqttCfg.user;
    json["passwordSet"] = mqttCfg.pass.length() > 0;
    json["prefix"] = mqttCfg.prefix;
    json["discovery"] = mqttCfg.discovery;
    json["discoveryPrefix"] = mqttCfg.discoveryPrefix;
    json["clientId"] = mqttClientId;
    json["connected"] = mqtt.connected();
    if (mqttLastError.length()) json["lastError"] = mqttLastError; else json["lastError"] = nullptr;
    json["publishes"] = mqttPublishes;
    json["commands"] = mqttCommands;
    String out; serializeJson(json, out);
    wm.server->send(code, "application/json", out);
}
