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
        "<title>PalletHeater</title>"
        "<style>body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px}"
        "a{display:block;margin:8px 0;padding:10px 14px;background:#f0f0f0;border-radius:6px;"
        "text-decoration:none;color:#222}a:hover{background:#ddd}"
        "table{border-collapse:collapse;width:100%%}"
        "td{padding:6px 8px;border-bottom:1px solid #eee}td:first-child{color:#666;width:120px}"
        "</style></head><body>"
        "<h2>PalletHeater</h2>"
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
    ESP.restart();
}

void handleRouteSwaggerUI() {
    wm.server->send(200, "text/html", swaggerUI);
}

// The embedded files are gzipped (scripts/gzip_assets.py). Every browser accepts
// that; a client that does not (plain curl) gets 406 with a hint.
static void sendGzipped(const char* type, const char* start, const char* end)
{
    if (wm.server->header("Accept-Encoding").indexOf("gzip") < 0) {
        wm.server->send(406, "text/plain", "Only available gzip-compressed: send Accept-Encoding: gzip (curl --compressed)");
        return;
    }
    wm.server->sendHeader("Content-Encoding", "gzip");
    wm.server->sendHeader("Cache-Control", "no-cache");
    wm.server->send_P(200, type, start, end - start);
}

void handleRouteWebApp() {
    sendGzipped("text/html", webAppHTML, webAppHTMLEnd);
}

void handleRouteSwaggerJSON() { 
    sendGzipped("application/json", swaggerJSON, swaggerJSONEnd);
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


    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
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

    serializeJson(json, buffer);
    wm.server->send(http_code, "application/json", buffer);
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
            serializeJson(json, buffer);
            wm.server->send(400, "application/json", buffer);
            return;
        }
        bool enable = (val == "true" || val == "1");
        bool ok = stove.write(eeprom, beepEnabledAddr, enable ? 1 : 0);
        if (!ok)
        {
            json["result"] = "failed - RS232 error";
            serializeJson(json, buffer);
            wm.server->send(500, "application/json", buffer);
            return;
        }
        cachedBeepEnabled = enable;
        json["result"]  = "success";
        json["enabled"] = cachedBeepEnabled;
        serializeJson(json, buffer);
        wm.server->send(200, "application/json", buffer);
        return;
    }

    if (wm.server->method() == HTTP_GET)
    {
        cachedBeepEnabled = stoveBeepIsEnabled();
        json["enabled"] = cachedBeepEnabled;
        serializeJson(json, buffer);
        wm.server->send(200, "application/json", buffer);
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
            serializeJson(json, buffer);
            wm.server->send(400, "application/json", buffer);
            return;
        }
        bool ok = stove.write(eeprom, TempSetpointAddr, (byte)sp);
        if (!ok)
        {
            json["result"] = "failed - RS232 error";
            serializeJson(json, buffer);
            wm.server->send(500, "application/json", buffer);
            return;
        }
        setPointTemp = sp;
        json["result"] = "success";
    }
    getTempSetpoint();
    json["setpoint"] = (int)setPointTemp;
    serializeJson(json, buffer);
    wm.server->send(200, "application/json", buffer);
}
