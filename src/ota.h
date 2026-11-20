#include <ArduinoOTA.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include "constants.h"

// The OTA password is the controller's API key (auth.h, NVS; /api/auth shows it),
// not a password in the source code: that one was public in the repository.
void enableOTA(const String& password){
    // same name as the web app (IP settings, default openpelletbridge). ArduinoOTA must not
    // start mDNS itself: it did so with its own name ("Pelletheater"), which replaced
    // openpelletbridge.local on the network. mdnsStart() (setupWifi.h) announces the OTA
    // service (_arduino._tcp) together with the others.
    {
        Preferences p; p.begin("net", true);
        String hn = p.getString("hostname", "openpelletbridge");
        p.end();
        ArduinoOTA.setHostname(hn.c_str());
    }
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setPassword(password.c_str());
    ArduinoOTA.onStart([](){
        otaActive = true;   // pauses the sensor task: no serial traffic, more time for WiFi
        // Stop BLE advertising so it stops competing with WiFi for the shared radio.
        if (bleEnabled) NimBLEDevice::stopAdvertising();
        String type;
        type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        LOGI("update", "ArduinoOTA update started");
    }).onEnd([](){
        otaActive = false;
        if (bleEnabled) NimBLEDevice::startAdvertising();
        LOGI("update", "ArduinoOTA update done");
        markRestart("firmware update (ArduinoOTA)");
    }).onProgress([](unsigned int progress, unsigned int total){
        esp_task_wdt_reset();
    }).onError([](ota_error_t error){
        otaActive = false;
        if (bleEnabled) NimBLEDevice::startAdvertising();
        LOGE("update", "ArduinoOTA error %u", error);
        switch(error){
            case OTA_AUTH_ERROR:
                LOGE("update", "ArduinoOTA: Auth failed");
                break;
            case OTA_BEGIN_ERROR:
                LOGE("update", "ArduinoOTA: Begin failed");
                break;
            case OTA_CONNECT_ERROR:
                LOGE("update", "ArduinoOTA: Connect failed");
                break;
            case OTA_RECEIVE_ERROR:
                LOGE("update", "ArduinoOTA: Receive failed");
                break;
            case OTA_END_ERROR:
                LOGE("update", "ArduinoOTA: End failed");
                break;
            default:
                LOGE("update", "ArduinoOTA: unknown error");
        }
    });

    ArduinoOTA.begin();
}