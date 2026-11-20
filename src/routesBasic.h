#pragma once
// HTTP routes: index page, restart, API docs, web app, clock, energy saving,
// beep and set point.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

void handleRouteIndex() {
    String ip    = WiFi.localIP().toString();
    String ssid  = WiFi.SSID();
    int32_t rssi = WiFi.RSSI();
    char page[1600];
    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>OpenPelletBridge</title>"
        "<style>body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px}"
        "a{display:block;margin:8px 0;padding:10px 14px;background:#f0f0f0;border-radius:6px;"
        "text-decoration:none;color:#222}a:hover{background:#ddd}"
        "table{border-collapse:collapse;width:100%%}"
        "td{padding:6px 8px;border-bottom:1px solid #eee}td:first-child{color:#666;width:120px}"
        "</style></head><body>"
        "<h2>OpenPelletBridge</h2>"
        "<table>"
        "<tr><td>IP</td><td>%s</td></tr>"
        "<tr><td>SSID</td><td>%s</td></tr>"
        "<tr><td>WiFi RSSI</td><td>%d dBm</td></tr>"
        "<tr><td>Uptime</td><td>%s</td></tr>"
        "</table><br>"
        "<a href='/' style='background:#e8590c;color:#fff;font-weight:600'>Open web app</a>"
        "<a href='/api'>API Documentation (Swagger)</a>"
        "<a href='/api/state'>Current stove state (JSON)</a>"
        "<a href='/api/health'>Health check</a>"
        "<a href='/restart'>Restart device</a>"
        "<p style='color:#888;font-size:13px'>To reconfigure WiFi, hold the reset button for 5 seconds.</p>"
        "</body></html>",
        ip.c_str(), ssid.c_str(), rssi, uptimeStr().c_str());
    wm.server->send(200, "text/html", page);
}

void handleRouteRestart() {
    wm.server->send(200, "text/plain", "Restarting...");
    delay(200);
    restartWith("restart requested (web app / API)");
}

void handleRouteSwaggerUI() {
    wm.server->send(200, "text/html", swaggerUI);
}

// ETag of an embedded file: FNV-1a over its bytes, computed once (a few ms for the web app).
// The browser asks again with If-None-Match and gets 304 without the file while it did not
// change (a new firmware with another web app gives another tag).
static bool httpNotModified(const uint8_t* start, const uint8_t* end, uint32_t& cache)
{
    if (!cache) {
        uint32_t h = 2166136261u;
        for (const uint8_t* p = start; p < end; p++) { h ^= *p; h *= 16777619u; }
        cache = h ? h : 1;
    }
    char tag[16]; snprintf(tag, sizeof(tag), "\"%08x\"", (unsigned)cache);
    wm.server->sendHeader("ETag", tag);
    if (wm.server->header("If-None-Match").indexOf(tag) < 0) return false;
    wm.server->send(304);
    return true;
}

// The embedded files are gzipped (scripts/gzip_assets.py). Every browser accepts
// that; a client that does not (plain curl) gets 406 with a hint.
// no-cache: the browser always asks, but with the ETag the answer is a 304 of a few bytes
static void sendGzipped(const char* type, const char* start, const char* end, uint32_t& etag)
{
    if (wm.server->header("Accept-Encoding").indexOf("gzip") < 0) {
        wm.server->send(406, "text/plain", "Only available gzip-compressed: send Accept-Encoding: gzip (curl --compressed)");
        return;
    }
    wm.server->sendHeader("Cache-Control", "no-cache");
    wm.server->sendHeader("Vary", "Accept-Encoding");
    if (httpNotModified((const uint8_t*)start, (const uint8_t*)end, etag)) return;
    wm.server->sendHeader("Content-Encoding", "gzip");
    httpSendLarge(200, type, (const uint8_t*)start, end - start);
}

// Home screen install (web app manifest + icons, web/icons/). Public like the app.
#define EMBED(name, sym) extern const uint8_t name##Start[] asm("_binary_" sym "_start"); extern const uint8_t name##End[] asm("_binary_" sym "_end");
EMBED(iconSvg, "web_icons_icon_svg")
EMBED(icon180, "web_icons_icon_180_png")
static void sendEmbedded(const char* type, const uint8_t* start, const uint8_t* end, uint32_t& etag)
{
    wm.server->sendHeader("Cache-Control", "max-age=86400");
    if (httpNotModified(start, end, etag)) return;
    httpSendLarge(200, type, start, end - start);
}
static uint32_t etagIconSvg = 0, etagIcon180 = 0, etagWebApp = 0, etagSpec = 0;
void handleRouteIconSvg() { sendEmbedded("image/svg+xml", iconSvgStart, iconSvgEnd, etagIconSvg); }
void handleRouteIcon180() { sendEmbedded("image/png", icon180Start, icon180End, etagIcon180); }
void handleRouteManifest()
{
    static const char manifest[] =
        "{\"name\":\"OpenPelletBridge\",\"short_name\":\"PelletBridge\",\"start_url\":\"/\",\"scope\":\"/\","
        "\"display\":\"standalone\",\"background_color\":\"#f4f5f7\",\"theme_color\":\"#e8590c\","
        "\"icons\":[{\"src\":\"/icon.svg\",\"sizes\":\"any\",\"type\":\"image/svg+xml\"},"
        "{\"src\":\"/icon-180.png\",\"sizes\":\"180x180\",\"type\":\"image/png\"}]}";
    wm.server->sendHeader("Cache-Control", "max-age=86400");
    wm.server->send(200, "application/manifest+json", manifest);
}

void handleRouteWebApp() {
    sendGzipped("text/html", webAppHTML, webAppHTMLEnd, etagWebApp);
}

void handleRouteSwaggerJSON() { 
    sendGzipped("application/json", swaggerJSON, swaggerJSONEnd, etagSpec);
}

void handleRouteTime()
{
    StaticJsonDocument<350> json;
    char time_output[30];
    int http_code;
    http_code=200;

    json.clear();

    if (wm.server->method() == HTTP_PATCH)
    {
        // Optional datetime=YYYY-MM-DDTHH:MM[:SS] (local time, e.g. from the browser);
        // without it the stove is synced to NTP time.
        String dt = wm.server->arg("datetime");
        if (dt.length())
        {
            struct tm t = {};
            int Y, M, D, h, m, sec = 0;
            int n = sscanf(dt.c_str(), "%d-%d-%d%*c%d:%d:%d", &Y, &M, &D, &h, &m, &sec);
            if (n < 5 || Y < 2020 || Y > 2099 || M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || m < 0 || m > 59)
            {
                wm.server->send(400, "application/json", "{\"result\":\"failed - datetime must be YYYY-MM-DDTHH:MM[:SS]\"}");
                return;
            }
            t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = m; t.tm_sec = sec; t.tm_isdst = -1;
            mktime(&t);   // normalises and fills in tm_wday
            setStoveDateTimeTm(t);
            json["result"] = "success Set stove time to " + dt;
        }
        else
        {
            setStoveDateTime();
            json["result"] = "success Synced NTP time to stove";
        }
        delay(200);
        getStoveDateTime();
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_PATCH )
    {
        JsonObject time = json.createNestedObject("time");
        time_t st = stoveTimeNow(), nt = ntpTimeNow();
        strftime(time_output, 30, "%F %T", localtime(&st));
        time["stove"] = time_output;

        strftime(time_output, 30, "%F %T", localtime(&nt));
        time["ntp"] = time_output;

        time["delta"] = nt - st;
    } else
    {
        http_code=405;
    }


    sendJson(http_code, json);
}

void handleRouteEnergysaving()
{
    StaticJsonDocument<350> json;
    int http_code;
    http_code=200;
    json.clear();

    if (wm.server->method() == HTTP_POST)
    {
        for (uint8_t i = 0; i < wm.server->args(); i++)
        {
            if (wm.server->argName(i) == "start")
            {
                if (  wm.server->arg(i) == "0" ||  wm.server->arg(i) == "-1" ||  wm.server->arg(i) == "-2" ||  wm.server->arg(i) == "-3") 
                {
                    int start = wm.server->arg(i).toInt() * -1;
                    if ( setEnergySavingStart(start) )
                    {
                        json["result"] = "success";
                    } else 
                    {
                        json["result"] = "Failed - RS2232 error...";
                        http_code=500;
                    }
                } else
                {
                    json["result"] = "failed - start is out of range: Should be between -3 and 0";
                    http_code=400;
                }
            }
            if (wm.server->argName(i) == "stop")
            {
                if (  wm.server->arg(i) == "0" ||  wm.server->arg(i) == "1" ||  wm.server->arg(i) == "2" ||  wm.server->arg(i) == "3") 
                {
                    int stop = wm.server->arg(i).toInt();
                    if ( setEnergySavingStop(stop) )
                    {
                        json["result"] = "success";
                    } else 
                    {
                        json["result"] = "Failed - RS2232 error...";
                        http_code=500;
                    }
                } else
                {
                    json["result"] = "failed - stop is out of range: Should be between 0 and 3";
                    http_code=400;
                }
            }
        }
    }
    
    if (wm.server->method() == HTTP_GET || wm.server->method() == HTTP_POST)
    {
        delay(20);
        getEnergySaving();
        JsonObject obj_energysaving = json.createNestedObject("energySaving");
        obj_energysaving["start"] = energySavingStart;
        obj_energysaving["stop"] = energySavingStop;
        // Manual §20: energy saving only stops the stove when STOP is set;
        // START = 0 (OFF) means it does not restart automatically after stopping.
        obj_energysaving["enabled"] = energySavingStop != 0;
        if (energySavingStop != 0)  obj_energysaving["stopTemperature"]  = setPointTemp + energySavingStop;
        if (energySavingStart != 0) obj_energysaving["startTemperature"] = setPointTemp + energySavingStart;


    } else {
        wm.server->send(405);
        return;
    }

    sendJson(http_code, json);
}

void handleRouteBeep()
{
    StaticJsonDocument<128> json;

    if (wm.server->method() == HTTP_POST)
    {
        String val = wm.server->arg("enabled");
        if (val != "true" && val != "false" && val != "1" && val != "0")
        {
            json["result"] = "failed - expected enabled=true/false";
            sendJson(400, json);
            return;
        }
        bool enable = (val == "true" || val == "1");
        bool ok = stove.write(eeprom, beepEnabledAddr, enable ? 1 : 0);
        if (!ok)
        {
            json["result"] = "failed - RS232 error";
            sendJson(500, json);
            return;
        }
        cachedBeepEnabled = enable;
        json["result"]  = "success";
        json["enabled"] = cachedBeepEnabled;
        sendJson(200, json);
        return;
    }

    if (wm.server->method() == HTTP_GET)
    {
        cachedBeepEnabled = stoveBeepIsEnabled();
        json["enabled"] = cachedBeepEnabled;
        sendJson(200, json);
        return;
    }

    wm.server->send(405);
}

void handleRouteSetpoint()
{
    StaticJsonDocument<128> json;
    if (wm.server->method() != HTTP_GET && wm.server->method() != HTTP_POST)
    {
        wm.server->send(405);
        return;
    }
    if (wm.server->method() == HTTP_POST)
    {
        String val = wm.server->arg("value");
        int sp = val.toInt();
        if (sp < 7 || sp > 30)
        {
            json["result"] = "failed - value must be 7-30";
            sendJson(400, json);
            return;
        }
        bool ok = stove.write(eeprom, TempSetpointAddr, (byte)sp);
        if (!ok)
        {
            json["result"] = "failed - RS232 error";
            sendJson(500, json);
            return;
        }
        setPointTemp = sp;
        json["result"] = "success";
    }
    getTempSetpoint();
    json["setpoint"] = (int)setPointTemp;
    sendJson(200, json);
}
