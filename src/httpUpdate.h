#pragma once
// Firmware update over HTTP: POST /api/update (multipart/form-data, field "firmware").
//
// Much faster than ArduinoOTA/espota, which sends 1 KB at a time and waits for an
// acknowledgement after every block; here the image is streamed in one TCP stream
// and written to the OTA partition while it arrives.
//
// Authorisation (checked when the upload starts, before anything is written):
//   X-API-Key / Authorization: Bearer <api key>   (see auth.h)
// A valid key is always needed, also when the API key is not required for the
// other routes (the web app reads it from /api/auth in that case).
#include <constants.h>
#include <auth.h>
#include <Update.h>
#include <NimBLEDevice.h>

static bool        updAuthorised = false;
static bool        updStarted    = false;
static String      updError;
static size_t      updWritten    = 0;

// Always the API key (X-API-Key / Authorization: Bearer), also when it is not required
// for the rest of the API. The fixed OTA password (X-OTA-Password) is gone: it was public.
static bool updRequestAuthorised()
{
    return requestHasValidKey();
}

static void updFail(const String& msg)
{
    if (updError.length() == 0) updError = msg;
    if (updStarted) { Update.abort(); updStarted = false; }
    otaActive = false;
    if (bleEnabled) NimBLEDevice::startAdvertising();
}

// The web server also calls the upload callback for a POST that is NOT multipart
// (as a "raw" request); upload() is not valid then and using it crashes the ESP32.
static bool updIsMultipart()
{
    return wm.server->header("Content-Type").startsWith("multipart/form-data");
}

// Called by the web server for every chunk of the uploaded file
void handleUpdateUpload()
{
    if (!updIsMultipart()) return;
    HTTPUpload& up = wm.server->upload();
    switch (up.status)
    {
    case UPLOAD_FILE_START:
        updError = ""; updWritten = 0; updStarted = false;
        updAuthorised = updRequestAuthorised();
        if (!updAuthorised) { updError = "unauthorised"; return; }
        otaActive = true;                                   // pause the sensor task
        if (bleEnabled) NimBLEDevice::stopAdvertising();    // give WiFi the radio
        LOGI("update", "upload started: %s", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { updFail(String("begin failed: ") + Update.errorString()); return; }
        updStarted = true;
        break;
    case UPLOAD_FILE_WRITE:
        if (!updStarted) return;
        esp_task_wdt_reset();
        if (Update.write(up.buf, up.currentSize) != up.currentSize) { updFail(String("write failed: ") + Update.errorString()); return; }
        updWritten += up.currentSize;
        break;
    case UPLOAD_FILE_END:
        if (!updStarted) return;
        if (!Update.end(true)) { updFail(String("end failed: ") + Update.errorString()); return; }
        updStarted = false;
        LOGI("update", "upload done: %u bytes", (unsigned)updWritten);
        break;
    case UPLOAD_FILE_ABORTED:
        updFail("upload aborted");
        break;
    }
}

// Called once the whole request has been received
void handleUpdateDone()
{
    if (!updIsMultipart()) {
        updAuthorised = false;
        wm.server->send(400, "application/json", "{\"result\":\"failed - send the firmware as multipart/form-data, field \\\"firmware\\\"\"}");
        return;
    }
    if (!updAuthorised) {
        wm.server->send(401, "application/json", "{\"error\":\"Missing or invalid API key or OTA password\"}");
        return;
    }
    if (updError.length() || !Update.isFinished()) {
        if (updError.length() == 0) updError = "no firmware received (multipart field \"firmware\")";
        updFail(updError);
        StaticJsonDocument<256> json;
        json["result"] = "failed - " + updError;
        String out; serializeJson(json, out);
        wm.server->send(500, "application/json", out);
        return;
    }
    StaticJsonDocument<128> json;
    json["result"] = "success";
    json["bytes"] = (unsigned)updWritten;
    json["restarting"] = true;
    String out; serializeJson(json, out);
    wm.server->sendHeader("Connection", "close");
    wm.server->send(200, "application/json", out);
    delay(500);           // let the response go out
    restartWith("firmware upload (/api/update)");
}
