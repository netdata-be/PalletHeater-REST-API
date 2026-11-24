#pragma once
// What the board and this build have, and the board-only sensors, for the API, metrics,
// MQTT and telnet. The shared files only call the hooks:
//   boardTick()                      loop()
//   boardStateJson() / boardWarnings()  GET /api/state (status.h)
//   boardMetrics()                   /metrics (metrics.h)
//   boardMqttStates() / boardMqttDiscovery()  mqtt.h
// Board-only parts compile only where the board has them (board.h): the DevKit build gets
// the feature list and empty hooks.
// Included after events.h and mqtt.h (uses evtStore, mqttState, mqttDiscoveryEntity).
#include "board.h"
#include "powerSource.h"

// Internal temperature sensor (temperatureRead()): the S3 only. The classic ESP32's sensor
// is missing on most chips (fixed 53.3 °C), so not used there.
#if defined(BOARD_OPB_S3)
#define BOARD_HAS_CHIP_TEMP 1
#else
#define BOARD_HAS_CHIP_TEMP 0
#endif

// controller.features in GET /api/state; the web app hides what is not listed (hasFeature())
static const char* const BOARD_FEATURES[] = {
    "bluetooth",
#if BOARD_HAS_ONBOARD_LED
    "onboardLed",
#endif
#if BOARD_HAS_WS2812
    "ws2812",
#endif
#if BOARD_HAS_BUZZER
    "buzzer",
#endif
#if BOARD_HAS_POWER_SENSE
    "powerSense",
#endif
#if BOARD_HAS_CHIP_TEMP
    "chipTemp",
#endif
};

#if BOARD_HAS_CHIP_TEMP
// Die temperature, read every 30 s and smoothed (one reading jumps a few degrees). The board
// sits at ~20 °C inside the stove casing, the die runs some 10 - 25 °C above that; the
// ESP32-S3-MINI-1 is rated up to 85 °C ambient. Above 70 °C something is wrong.
static const float CHIP_TEMP_WARN_C = 70, CHIP_TEMP_CLEAR_C = 65;   // warning with hysteresis
static float chipTempC = NAN;
static bool  chipTempHigh = false;
static void chipTempTick()
{
    static uint32_t last = 0;
    if (last && millis() - last < 30000) return;
    last = millis();
    float t = temperatureRead();   // IDF 4.4: start, read, stop (~1 ms)
    if (isnan(t) || t < -30 || t > 125) return;   // failed / implausible read
    chipTempC = isnan(chipTempC) ? t : chipTempC * 0.7f + t * 0.3f;
    if (!chipTempHigh && chipTempC >= CHIP_TEMP_WARN_C) { chipTempHigh = true; LOGW("sys", "chip temperature %.0f °C (warning from %.0f °C, limit 85 °C)", chipTempC, CHIP_TEMP_WARN_C); }
    else if (chipTempHigh && chipTempC < CHIP_TEMP_CLEAR_C) { chipTempHigh = false; LOGI("sys", "chip temperature back to %.0f °C", chipTempC); }
}
#endif

static inline void boardTick()
{
#if BOARD_HAS_POWER_SENSE
    powerSourceTick();
#endif
#if BOARD_HAS_CHIP_TEMP
    chipTempTick();
#endif
}

// GET /api/state: controller.features, power, controller.chipTemp
static void boardStateJson(JsonObject root, JsonObject ctrl)
{
    JsonArray f = ctrl.createNestedArray("features");
    for (const char* n : BOARD_FEATURES) f.add(n);
#if BOARD_HAS_CHIP_TEMP
    if (!isnan(chipTempC)) ctrl["chipTemp"] = serialized(String(chipTempC, 1)); else ctrl["chipTemp"] = nullptr;
#endif
#if BOARD_HAS_POWER_SENSE
    JsonObject p = root.createNestedObject("power");
    if (powerSrc) p["source"] = powerSrcName(powerSrc); else p["source"] = nullptr;
    uint32_t since = powerSince();
    if (since) p["since"] = since; else p["since"] = nullptr;
    p["switchovers"] = powerSwitches;
#endif
}

static void boardWarnings(JsonArray w)
{
#if BOARD_HAS_CHIP_TEMP
    if (chipTempHigh) {
        char d[96];
        snprintf(d, sizeof(d), "Controller chip at %.0f °C (ESP32-S3 limit 85 °C): check the ventilation of the case", chipTempC);
        JsonObject o = w.createNestedObject(); o["id"] = "CHIP_TEMP_HIGH"; o["desc"] = d;   // copied (char*)
    }
#endif
}

static void boardMetrics(String* p)
{
#if BOARD_HAS_POWER_SENSE
    if (powerSrc) setMetric(p, "esp32_power_from_stove", "Power source of the board: 1 = stove supply, 0 = USB", String(powerSrc == PWR_STOVE ? 1 : 0));
    setCounter(p, "esp32_power_switchovers_total", "Switchovers between stove supply and USB since start", String(powerSwitches));
#endif
#if BOARD_HAS_CHIP_TEMP
    if (!isnan(chipTempC)) setMetric(p, "esp32_chip_temp_c", "Internal temperature of the ESP32-S3 in Celsius", String(chipTempC, 1));
#endif
}

static void boardMqttStates(bool force)
{
#if BOARD_HAS_POWER_SENSE
    if (powerSrc) mqttState("power_source", powerSrcName(powerSrc), force);
#endif
#if BOARD_HAS_CHIP_TEMP
    if (!isnan(chipTempC)) mqttState("chip_temp", String(chipTempC, 0), force);   // whole degrees: no publish for every 0.1
#endif
}

static void boardMqttDiscovery()
{
#if BOARD_HAS_POWER_SENSE
    mqttDiscoveryEntity("sensor", "power_source", "Voeding", [](JsonObject& o) {
        o["device_class"] = "enum"; JsonArray a = o.createNestedArray("options"); a.add("stove"); a.add("usb");
        o["icon"] = "mdi:power-plug"; o["entity_category"] = "diagnostic"; }, false);
#endif
#if BOARD_HAS_CHIP_TEMP
    mqttDiscoveryEntity("sensor", "chip_temp", "Temperatuur controller", [](JsonObject& o) {
        o["device_class"] = "temperature"; o["unit_of_measurement"] = "°C"; o["state_class"] = "measurement";
        o["entity_category"] = "diagnostic"; }, false);
#endif
}
