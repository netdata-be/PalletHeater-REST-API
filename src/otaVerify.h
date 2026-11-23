#pragma once
// Rollback of a firmware update that does not start properly.
//
// The bootloader has rollback on (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): a new image starts
// "pending verify", and if the controller restarts before the image is confirmed, the
// bootloader goes back to the previous firmware. The Arduino core would confirm every image
// right away (initArduino, before setup()), which makes that useless; verifyRollbackLater()
// below stops that, and this file confirms the image itself:
//   - confirmed after OTA_TRIAL_OK_MS running with WiFi connected: setup() went through and the
//     loop, the network and the web server work
//   - a crash, watchdog reset or power cut before that: the bootloader starts the previous
//     firmware, which logs "rolled back" and shows it in /api/firmware
//   - no WiFi for OTA_TRIAL_NOWIFI_MS: back to the previous firmware on purpose (a controller
//     that cannot be reached right after an update is most likely the update's fault)
//   - another update during the trial: the running image is confirmed first (ESP-IDF refuses
//     to write the other slot while the running one is pending)
// Applies to every way of updating: GitHub (fwUpdate.h), /api/update, espota, Bluetooth.
// The version on trial is kept in NVS "ota"/"trial" (written first thing in setup()), so the
// previous firmware can tell which update it rolled back.
#include <esp_ota_ops.h>
#include <Preferences.h>
#include <WiFi.h>

void otaLastInstallChanged(const String& last);   // fwUpdate.h: its copy of "last install"
extern "C" bool verifyRollbackLater() { return true; }   // overrides the weak C default in the Arduino core

static const uint32_t OTA_TRIAL_OK_MS     = 120000;   // 2 min with WiFi
static const uint32_t OTA_TRIAL_NOWIFI_MS = 600000;   // 10 min without WiFi: roll back
static bool   otaTrial = false;      // the running image is on trial (pending verify)
static String otaPrevVersion;        // the firmware before this one (NVS "ota"/"good"), for "last install"
static String otaRolledBack;         // version of an update that was rolled back ("" = none)

// First thing in setup(): is this image on trial, was the last update rolled back?
void otaVerifySetup()
{
    const esp_partition_t* run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        otaTrial = true;
        Preferences p; p.begin("ota", false);
        otaPrevVersion = p.getString("good", p.getString("trial", "?"));   // "trial" for firmware from before "good" existed
        p.putString("trial", BUILD_VERSION); p.end();
        // a new firmware runs: an unfinished GitHub install noted by the previous one is over
        // (installed by upload, espota or Bluetooth this time; fwUpdate.h reads it later in setup)
        Preferences f; f.begin("fwupd", false); f.remove("prog"); f.end();
    }
    // a partition marked invalid: an update that did not start properly. It stays marked until
    // the next update overwrites that slot, so this is shown until then.
    if (esp_ota_get_last_invalid_partition()) {
        Preferences p; p.begin("ota", false); otaRolledBack = p.getString("trial", "?"); p.remove("crashtest"); p.end();
        if (otaRolledBack == BUILD_VERSION) otaRolledBack = "a new firmware";   // it failed before it could note its version
    }
}
// After the log is up (setup): say what happened
void otaVerifyReport()
{
    if (otaTrial) LOGI("update", "new firmware %s on trial: confirmed after %lu s running with WiFi; a restart before that goes back to the previous firmware",
                       BUILD_VERSION, (unsigned long)(OTA_TRIAL_OK_MS / 1000));
    if (otaRolledBack.length())
        LOGW("update", "the update to %s did not start properly and was rolled back: running %s", otaRolledBack.c_str(), BUILD_VERSION);
}
static void otaConfirm(const char* why)
{
    if (!otaTrial) return;
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    otaTrial = false;
    if (e == ESP_OK) {
        // the last install, whatever way it came (GitHub, upload, espota, Bluetooth)
        Preferences p; p.begin("ota", false); p.putString("good", BUILD_VERSION); p.end();
        if (otaPrevVersion.length() && otaPrevVersion != BUILD_VERSION) {
            String last = otaPrevVersion + " -> " + BUILD_VERSION;
            Preferences f; f.begin("fwupd", false); f.putString("last", last); f.end();
            otaLastInstallChanged(last);
        }
    }
    if (e == ESP_OK) LOGI("update", "firmware %s confirmed (%s)", BUILD_VERSION, why);
    else LOGW("update", "confirming firmware %s failed: %s", BUILD_VERSION, esp_err_to_name(e));
}
// Before an update writes the other slot
void otaConfirmBeforeUpdate() { otaConfirm("a new update starts"); }
// From loop()
void otaVerifyLoop()
{
    if (!otaTrial) return;
    uint32_t up = millis();
#ifdef OTA_ROLLBACK_TEST
    // Test build (-DOTA_ROLLBACK_TEST, never in a release): crash once 30 s into the trial; the
    // bootloader should start the previous firmware. Only once (NVS flag), so a bootloader without
    // rollback does not end in a restart loop: then this build starts normally the second time.
    if (up >= 30000) {
        Preferences p; p.begin("ota", false);
        bool done = p.getBool("crashtest", false);
        p.putBool("crashtest", !done);
        p.end();
        if (!done) { LOGW("update", "OTA_ROLLBACK_TEST: crashing on purpose to test the rollback"); delay(500); abort(); }
    }
#endif
    if (up >= OTA_TRIAL_OK_MS && WiFi.isConnected()) { otaConfirm("running 2 min with WiFi"); return; }
    if (up >= OTA_TRIAL_NOWIFI_MS && !WiFi.isConnected()) {
        LOGW("update", "firmware %s: no WiFi for %lu min after the update - going back to the previous firmware",
             BUILD_VERSION, (unsigned long)(OTA_TRIAL_NOWIFI_MS / 60000));
        delay(500);   // let the log reach SPIFFS / the console
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}
// Seconds until the trial ends (with WiFi), 0 = not on trial
uint32_t otaTrialSecondsLeft() { return otaTrial ? (millis() >= OTA_TRIAL_OK_MS ? 0 : (OTA_TRIAL_OK_MS - millis()) / 1000) : 0; }
