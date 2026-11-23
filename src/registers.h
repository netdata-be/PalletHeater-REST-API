#pragma once
// Register names and descriptions: remote menus and parameters, RAM / EEPROM
// value texts, /api/dump and /api/remote.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

const char* getSelectedParameterName(int id)
{
    switch (id)
    {
        case 0:   return nullptr;
        case 71:  return "Multi Comfort";
        case 76:  return "Beep";
        case 79:  return "Display settings";
        case 80:  return "Language";
        case 123: return "State Stove";
        case 128: return "Flame power";
        case 130: return "MultiFuoco";
        case 145: return "Energy Saving - Stop";
        case 146: return "Energy Saving - Start";
        case 193: return "Combustion quality";
        case 197: return "Pellet contact";
        case 198: return "Pellet quality";
        case 199: return "Pellet reserve";
        case 200: return "Multi Comfort Plus";
        case 235: return "WPS";
        case 236: return "Reset";
        case 248: return "Day of week";
        case 249: return "Hour";
        case 250: return "min";
        case 217: return "Instell wireless";
        case 251: return "Day";
        case 252: return "Month";
        case 253: return "year";
        default:
        {
            // CHRONO programs: parameter = EEPROM address + 1 of the program field
            static const char* const fields[8] = { "On/Off", "Start time", "Stop time", "Set point",
                                                   "Flame power", "Room fan", nullptr, "Week days" };
            static char chronoBuf[32];
            for (int i = 0; i < 6; i++) {
                int off = id - 1 - timerBases[i];
                if (off >= 0 && off <= 7 && fields[off]) {
                    snprintf(chronoBuf, sizeof(chronoBuf), "PR%d - %s", i + 1, fields[off]);
                    return chronoBuf;
                }
            }
        }
            static char unknownParamBuf[16];
            snprintf(unknownParamBuf, sizeof(unknownParamBuf), "(%d)", id);
            return unknownParamBuf;
    }
}

const char* getRemoteMenuName(int id)
{
    switch (id)
    {
        case 0:  return "Main screen";
        case 1:  return "Multifuoco";
        case 2:  return "Chrono";
        case 3:  return "Language";
        case 5:  return "Clock";
        case 7:  return "Multicomfort";
        case 8:  return "Multi Comfort Plus";
        case 9:  return "Energy Saving";
        case 11: return "Beep";
        case 12: return "Display";
        case 13: return "Installer";
        case 39: return "Installer > Fill Auger";
        case 40: return "Installer > Pellet reserve";
        case 41: return "Installer > Pellet contact";
        case 42: return "Installer > External Thermo";
        case 43: return "Installer > Memory";
        case 51: return "Installer > Memory > Total Hours";
        case 52: return "Installer > Memory > Interval hours";
        case 53: return "Installer > Memory > Amount of starts";
        case 54: return "Installer > Memory > Error Memory";
        case 44: return "Installer > Pellet quality";
        case 45: return "Installer > Combustion quality";
        case 46: return "Installer > Instell wireless";
        case 47: return "Installer > Link wireless";
        case 49: return "Installer > Wifi";
        case 64: return "Installer > Wifi > WPS";
        case 65: return "Installer > Wifi > Wifi Reset";
        case 14: return "State Stove";
        case 50: return "Installer > Parameters";
        case 19: return "Installer > Parameters > Factory Reset";
        case 20: return "Installer > Parameters > Databank";
        case 21: return "Installer > Parameters > Smoke Ext Settings";
        case 22: return "Installer > Parameters > Load Settings";
        case 23: return "Installer > Parameters > MultiFUOCO v1";
        case 26: return "Installer > Parameters > Reset Alarm";
        case 25: return "Installer > Parameters > Reset Parthour";
        case 27: return "Installer > Parameters > Encoder";
        case 15: return "exit";
        default:
            static char unknownMenuBuf[16];
            snprintf(unknownMenuBuf, sizeof(unknownMenuBuf), "(%d)", id);
            return unknownMenuBuf;
    }
}

static bool isKnownRamAddr(byte addr)
{
    switch (addr) {
        case 0x01: case 0x02: case 0x04: case 0x08: case 0x16: case 0x24: case 0x0A: case 0xA7: case 0x0D: case 0x18: case 0x19: case 0x1A: case 0xB6:
        case 0x27: case 0x36: case 0x81: case 0x82:
        case 0x21: case 0x31: case 0x32: case 0x33: case 0x34:
        case 0x37: case 0x3E: case 0x44: case 0x48: case 0x4D:
        case 0x50: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x89: case 0xA4: case 0x91: case 0xAC: case 0xB7: case 0xB8:
        case 0x7E: case 0x7F: case 0x80: case 0x8F: case 0xD0:
        case 0xD3: case 0xDF:
            return true;
        default: return false;
    }
}

static bool isKnownEepromAddr(byte addr)
{
    switch (addr) {
        case 0x00: case 0x01: case 0x03: case 0x0A: case 0x0B: case 0x0F: case 0x10: case 0x11:
        case 0x12: case 0x13: case 0x14: case 0x15: case 0x20: case 0x21:
        case 0x02: case 0x04: case 0x05: case 0x06: case 0x07:
        case 0x08: case 0x09: case 0x48: case 0x49: case 0x4B:
        case 0x4F: case 0x7D: case 0x7F: case 0x81: case 0x8A:
        case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        case 0x90: case 0x91: case 0xBC: case 0xC0: case 0xC4:
        case 0xC6: case 0xE0: case 0xE1: case 0xE2: case 0xE3:
        case 0xE4: case 0xE7: case 0xE8: case 0xEA: case 0xEB: case 0xEC:
        case 0xEE: case 0xEF: case 0xCD: case 0xCE: case 0xCF:
            return true;
        default: return false;
    }
}

static String getRamValueTxt(byte addr, int val)
{
    if (val < 0) return "read error";
    char buf[64];
    switch (addr) {
        case 0x01: snprintf(buf, sizeof(buf), "%.1f °C (ambient temp)", val / 2.0f);          return buf;
        case 0x04: if (!val) return "0 (too cold for the room fan)"; snprintf(buf, sizeof(buf), "%d (hot enough for the room fan; follows the flame level)", val); return buf;
        case 0x0D: snprintf(buf, sizeof(buf), "%.1f s (auger on-time per cycle)", val / 10.0f); return buf;
        case 0x18: { const char* p = getSelectedParameterName(val); return p ? p : ""; }
        case 0x19: snprintf(buf, sizeof(buf), "Menu edit value %d", val); return buf;
        case 0x08: {   // key pressed on the remote (verified 17/11/2026)
            if (!val) return "No key pressed";
            String k = "Key:";
            if (val & 1) k += " DOWN"; if (val & 2) k += " UP"; if (val & 4) k += " ESC"; if (val & 8) k += " OK";
            if (val & ~0x0F) k += " +?";
            return k;
        }
        case 0x16: snprintf(buf, sizeof(buf), "Key event count %d (1 = press, +1 per auto-repeat)", val); return buf;
        case 0x24: snprintf(buf, sizeof(buf), "Key release countdown %d", val); return buf;
        case 0xB6: return (val & 0x01) ? "Wireless temperature sensor off (INSTELL WIRELESS)" : "Wireless temperature sensor on (INSTELL WIRELESS)";
        case 0x1A: return getRemoteMenuName(val);
        case 0x21:
            switch (val) {
                case 0:  return "Off";
                case 1:  return "Starting";
                case 2:  return "Pellet loading";
                case 3:  return "Ignition";
                case 4:  return "Working";
                case 5:  return "Brazier cleaning";
                case 6:  return "Final cleaning";
                case 7:  return "Standby";
                case 8:  return "Alarm State";
                case 9:  return "Ignition failure";
                case 10: return "Alarm";
                default: snprintf(buf, sizeof(buf), "Unknown state (%d)", val); return buf;
            }
        case 0x31: snprintf(buf, sizeof(buf), "%d sec (countdown)", val);    return buf;
        case 0x32: snprintf(buf, sizeof(buf), "%d min (brazier cleaning countdown)", val); return buf;
        case 0x33: snprintf(buf, sizeof(buf), "%s - %s", getErrorCode(val).c_str(), getErrorDesc(val).c_str()); return buf;
        case 0x34: snprintf(buf, sizeof(buf), "Flame power %d", val);        return buf;
        case 0x37: if (!val) return "0 rpm (fume fan stopped)"; snprintf(buf, sizeof(buf), "%d rpm (fume fan)", (val + 25) * 10); return buf;
        case 0x36: if (!val) return "0 rpm (fume fan target, stopped)"; snprintf(buf, sizeof(buf), "%d rpm (fume fan target)", (val + 25) * 10); return buf;
        case 0x02: case 0x27: snprintf(buf, sizeof(buf), "%d (= fume temp - 30)", val); return buf;
        case 0x81: return "Clock: seconds since midnight / 2 (low byte, with 0x82)";
        case 0x82: return "Clock: seconds since midnight / 2 (high byte, with 0x81)";
        case 0x3E: snprintf(buf, sizeof(buf), "%d °C (fume temp)", val);      return buf;
        case 0x44: snprintf(buf, sizeof(buf), "%.1f °C (stove room temp)", val / 2.0f); return buf;
        case 0x48: if (!val) return "0 rpm (fume fan stopped)"; snprintf(buf, sizeof(buf), "%d rpm (fume fan)", (val + 25) * 10); return buf;
        case 0x4D: return val ? "1 (flip)" : "0 (flop)";
        case 0x50: snprintf(buf, sizeof(buf), "%d (unknown; stayed 0 during the pellet reserve countdown)", val); return buf;
        case 0x91: snprintf(buf, sizeof(buf), "%d (reserve phase 2: off in %d min)", val, (val + 1) / 2); return buf;
        case 0x89: snprintf(buf, sizeof(buf), "%d min until the pellet reserve alarm (counts down while RAM 0xA4 = 5, 20 = full)", val); return buf;
        case 0xB2: snprintf(buf, sizeof(buf), "%d (auger motor: ~200 running, 0 stopped)", val); return buf;
        case 0xA4: snprintf(buf, sizeof(buf), "%d (level sensor: %s)", val, val == 5 ? "pellets low" : val == 0 ? "pellets OK" : val >= 1 && val <= 3 ? "pellets seen, the stove confirms the refill (counts down to 0)" : val == 4 ? "pellets seen (refill starting, or pellets sliding past the sensor)" : "unknown value"); return buf;
        case 0xAC: snprintf(buf, sizeof(buf), "%d (copy of the level sensor 0xA4)", val); return buf;
        case 0xB7: snprintf(buf, sizeof(buf), "%d (hours since last service, low byte; copy of EEPROM 0xE7)", val); return buf;
        case 0xB8: snprintf(buf, sizeof(buf), "%d (hours since last service, high byte; copy of EEPROM 0xE8)", val); return buf;
        case 0x39: snprintf(buf, sizeof(buf), "%d (status bits? 0x80 reserve, 0x20 pellets used up)", val); return buf;
        case 0x7A: snprintf(buf, sizeof(buf), "%d sec", val);  return buf;
        case 0x7B: {
            const char* days[] = {"", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
            return (val >= 1 && val <= 7) ? days[val] : "?";
        }
        case 0x7C: snprintf(buf, sizeof(buf), "%02d:xx (hour)", val);  return buf;
        case 0x7D: snprintf(buf, sizeof(buf), "xx:%02d (min)", val);   return buf;
        case 0x7E: snprintf(buf, sizeof(buf), "Day %d", val);          return buf;
        case 0x7F: snprintf(buf, sizeof(buf), "Month %d", val);        return buf;
        case 0x80: snprintf(buf, sizeof(buf), "Year 20%02d", val);     return buf;
        case 0x8F: snprintf(buf, sizeof(buf), "%.1f °C (remote control temp)", val / 2.0f); return buf;
        case 0xD0: snprintf(buf, sizeof(buf), "%.1f °C (stove room temp)", val / 2.0f); return buf;
        case 0xD3: snprintf(buf, sizeof(buf), "%d (follows the actual flame power, min 1; not the fan)", val); return buf;
        case 0x0A: if (!val) return "Room fan power 0 (off)"; snprintf(buf, sizeof(buf), "Room fan power %d (speed x flame: 9/14/19 flame 1, 21/23/25 flame 2, 27 flame 3 speed 1)", val); return buf;
        case 0xA7: if (!val) return "Room fan drive (2) 0 (off)"; snprintf(buf, sizeof(buf), "Room fan drive (2) %d (~117 - 1.5 x power 0x0A)", val); return buf;
        case 0xDF: snprintf(buf, sizeof(buf), "%.1f rpm (feed rate)", val / 10.0f); return buf;
        default:   return "";
    }
}

static String getEepromValueTxt(byte addr, int val)
{
    if (val < 0) return "read error";
    char buf[64];
    switch (addr) {
        // 0x00 - 0x15: installer parameters Pr01 - Pr22 (address = Pr - 1), as in the Micronova
        // L023 manual; the values match what the stove did (20/11/2026)
        case 0x00: snprintf(buf, sizeof(buf), "%d min (max. ignition time, Pr01)", val); return buf;
        case 0x01: snprintf(buf, sizeof(buf), "%d min (start phase II, Pr02)", val); return buf;
        case 0x02: snprintf(buf, sizeof(buf), "%d min (brazier cleaning interval)", val); return buf;
        case 0x03: snprintf(buf, sizeof(buf), "%.1f s (auger, pellet load, Pr04)", val / 10.0f); return buf;
        case 0x04: snprintf(buf, sizeof(buf), "%.1f s (auger, start phase II, Pr05)", val / 10.0f); return buf;
        case 0x0A: snprintf(buf, sizeof(buf), "%d s (alarm delay, Pr11)", val); return buf;
        case 0x0B: snprintf(buf, sizeof(buf), "%d s (brazier cleaning time, Pr12)", val); return buf;
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
            snprintf(buf, sizeof(buf), "%d rpm (fume fan %s)", (val + 25) * 10, addr == 0x0F ? "start I" : addr == 0x10 ? "start II" : "per power"); return buf;
        case 0x20: snprintf(buf, sizeof(buf), "%d s (wait after pre-load)", val); return buf;
        case 0x21: snprintf(buf, sizeof(buf), "%d s (pellet pre-load time)", val); return buf;
        case 0x05: snprintf(buf, sizeof(buf), "%.1f s (auger, flame power 1)", val / 10.0f); return buf;
        case 0x06: snprintf(buf, sizeof(buf), "%.1f s (auger, flame power 2)", val / 10.0f); return buf;
        case 0x07: snprintf(buf, sizeof(buf), "%.1f s (auger, flame power 3)", val / 10.0f); return buf;
        case 0x08: snprintf(buf, sizeof(buf), "%.1f s (auger, flame power 4)", val / 10.0f); return buf;
        case 0x09: snprintf(buf, sizeof(buf), "%.1f (auger value level 5, unused)", val / 10.0f); return buf;
        case 0x48: return val ? "Ext thermostat: Stop" : "Ext thermostat: Modulate";
        case 0x49: return val ? "Ext thermostat: N.C." : "Ext thermostat: N.O.";
        case 0x4B: return val ? "Beep enabled" : "Beep disabled";
        case 0x4F: {
            const char* langs[] = {"", "ENG", "FRA", "DEU", "NLD", "POR", "SPA", "HRV", "DAN"};
            return (val >= 1 && val <= 8) ? langs[val] : "Unknown language";
        }
        case 0x7D: snprintf(buf, sizeof(buf), "%d °C (setpoint room temp)", val); return buf;
        case 0x7F: snprintf(buf, sizeof(buf), "Flame power %d", val);             return buf;
        case 0x81: snprintf(buf, sizeof(buf), "Room fan speed %d", val);           return buf;
        case 0x90: snprintf(buf, sizeof(buf), "+%d °C (energy saving stop offset)", val);  return buf;
        case 0x91: snprintf(buf, sizeof(buf), "-%d °C (energy saving start offset)", val); return buf;
        case 0x8A: snprintf(buf, sizeof(buf), "%c (FW variant char 0)", (char)val); return buf;
        case 0x8B: snprintf(buf, sizeof(buf), "%c (FW variant char 1)", (char)val); return buf;
        case 0x8C: snprintf(buf, sizeof(buf), "FW product %d", val);  return buf;
        case 0x8D: snprintf(buf, sizeof(buf), "FW major %d", val);    return buf;
        case 0x8E: snprintf(buf, sizeof(buf), "FW minor %d", val);    return buf;
        case 0x8F: snprintf(buf, sizeof(buf), "FW patch %d", val);    return buf;
        case 0xCD: snprintf(buf, sizeof(buf), "Serial number byte 1 (high) 0x%02X", val); return buf;
        case 0xCE: snprintf(buf, sizeof(buf), "Serial number byte 2 0x%02X", val);        return buf;
        case 0xCF: snprintf(buf, sizeof(buf), "Serial number byte 3 (low) 0x%02X", val);  return buf;
        case 0xBC: { const char* l = getPelletQualityLabel(val);     return l ? l : "Unknown pellet quality"; }
        case 0xC0: { const char* l = getCombustionQualityLabel(val); return l ? l : "Unknown combustion quality"; }
        case 0xC4: return val ? "Pellet contact N.C." : "Pellet contact N.O.";
        case 0xC6: return val ? "Low pellet monitoring ON" : "Low pellet monitoring OFF";
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4:
            snprintf(buf, sizeof(buf), "%s - %s", getErrorCode(val).c_str(), getErrorDesc(val).c_str());
            return buf;
        case 0xE7: snprintf(buf, sizeof(buf), "%d (hours since last service, low byte)", val); return buf;
        case 0xE8: snprintf(buf, sizeof(buf), "%d (hours since last service, high byte)", val); return buf;
        case 0xEA: snprintf(buf, sizeof(buf), "%d (total runtime low byte)", val); return buf;
        case 0xEB: snprintf(buf, sizeof(buf), "%d (total runtime high byte)", val); return buf;
        case 0xEC: snprintf(buf, sizeof(buf), "%d working hours", val); return buf;
        case 0xEE: snprintf(buf, sizeof(buf), "%d (starts low byte)", val);  return buf;
        case 0xEF: snprintf(buf, sizeof(buf), "%d (starts high byte)", val); return buf;
        default:   return "";
    }
}

static String jsonEscape(const String& s)
{
    String out;
    out.reserve(s.length() + 4);
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

static void parseAddressList(const String& param, bool out[256])
{
    pc::parseAddressList(param.c_str(), out);
}

// Dump as JSON, streamed in chunks: {"RAM(0x00)":{"0x21":{Address_int, Value_int, Value_txt,
// Value_hex}, ...}, "EEPROM(0x20)":{...}}. get(type, addr) gives the value (-1 = read error):
// read from the bus right now (synchronous dump) or from a finished background dump.
static void dumpWriteJson(bool doRam, bool doEeprom, bool filterUnknown, const bool* addrs, std::function<int(byte, byte)> get)
{
    struct RegDef {
        bool        enabled;
        byte        type;
        const char* label;
        String      (*txtFn)(byte, int);
        bool        (*isKnownFn)(byte);
    } regs[] = {
        { doRam,    ram,    "RAM(0x00)",    getRamValueTxt,    isKnownRamAddr    },
        { doEeprom, eeprom, "EEPROM(0x20)", getEepromValueTxt, isKnownEepromAddr },
    };
    // Stream the response in chunks: a full dump of both banks is ~45 KB, and
    // building it as one String can fail on heap fragmentation, which silently
    // drops parts of the output and produces truncated / invalid JSON.
    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "application/json", "");
    String out;
    out.reserve(2048);
    // A client that stops reading (tab closed, page reloaded, weak WiFi) would block
    // the write for up to 10 s: check first, and stop the dump after 3 s.
    bool aborted = false;
    auto flushOut = [&out, &aborted](bool force) {
        if (aborted || !(force || out.length() > 1500)) return;
        httpSendChunk(out);   // non-blocking, stops the answer when the client takes no data
        if (httpStalled) aborted = true;
    };
    out += "{";
    bool firstReg = true;
    for (auto& r : regs) {
        if (!r.enabled || aborted) continue;
        if (!firstReg) out += ",";
        firstReg = false;
        out += "\"";
        out += r.label;
        out += "\":{";
        bool firstAddr = true;
        for (int addr = 0; addr <= 0xFF && !aborted; addr++) {
            if (!addrs[addr]) continue;
            if (filterUnknown && r.isKnownFn((byte)addr)) continue;
            esp_task_wdt_reset();
            int val = get(r.type, (byte)addr);
            uint8_t uval = (val >= 0) ? (uint8_t)val : 0;
            String  txt  = jsonEscape(r.txtFn((byte)addr, val));
            char header[72];
            snprintf(header, sizeof(header),
                "%s\"0x%02X\":{\"Address_int\":%d,\"Value_int\":%d,\"Value_txt\":\"",
                firstAddr ? "" : ",",
                addr, addr, (val >= 0 ? val : -1));
            out += header;
            out += txt;
            char footer[24];
            snprintf(footer, sizeof(footer), "\",\"Value_hex\":\"0x%02X\"}", uval);
            out += footer;
            firstAddr = false;
            flushOut(false);
        }
        out += "}";
    }
    if (aborted) return;
    out += "}";
    flushOut(true);
    httpEndChunks();  // terminate the chunked response
}

// ── Background dump (GET /api/dump?...&async=1) ─────────────────────────────────────
// A synchronous dump runs in the loop task: while it reads (~45 EEPROM registers take ~8 s)
// the web server, telnet and MQTT wait, and the loop takes the bus lock back right after every
// register. The background dump reads in its own task with a pause after every register, so
// the sensor task and other requests get the bus in between; the request answers at once:
//   202 {"state":"running","done":12,"total":45}  until it is done, then the dump itself (once;
//   the next request with the same parameters starts a new dump). Another dump while one runs: 409.
struct DumpJob {
    volatile uint8_t state = 0;   // 0 none, 1 running, 2 done
    bool    delivered = false;
    bool    doRam = false, doEeprom = false, filterUnknown = false;
    bool    addrs[256];
    int16_t val[2][256];          // [0] RAM, [1] EEPROM; -1 = read error
    volatile int done = 0;
    int     total = 0;
    String  key;                  // the parameters it was started with
    char    src[96];
    uint32_t tookMs = 0;
};
static DumpJob* dumpJob = nullptr;   // allocated at the first background dump (~1.5 KB), kept
static const uint32_t DUMP_PAUSE_MS = 20;

static void dumpTask(void*)
{
    DumpJob& j = *dumpJob;
    uint32_t t0 = millis();
    int failedInRow = 0;
    for (int bank = 0; bank < 2; bank++) {
        if (!(bank ? j.doEeprom : j.doRam)) continue;
        byte type = bank ? eeprom : ram;
        for (int addr = 0; addr <= 0xFF; addr++) {
            if (!j.addrs[addr]) continue;
            if (j.filterUnknown && (bank ? isKnownEepromAddr((byte)addr) : isKnownRamAddr((byte)addr))) continue;
            int v = -1;
            // stove gone: the rest are read errors without trying (as the synchronous dump)
            if (failedInRow < 3 && !stove.linkDown && stoveLockTake(3000)) {
                { CmdSource cs(j.src); v = stove.read(type, (byte)addr); }   // source only set while the bus is ours
                if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
                failedInRow = v < 0 ? failedInRow + 1 : 0;
            }
            j.val[bank][addr] = (int16_t)v;
            j.done++;
            vTaskDelay(pdMS_TO_TICKS(DUMP_PAUSE_MS));   // the sensor task and web routes get the bus
        }
    }
    j.tookMs = millis() - t0;
    LOGD("http", "background dump: %d register(s) in %lu ms", j.total, (unsigned long)j.tookMs);
    j.state = 2;
    vTaskDelete(nullptr);
}

void handleRouteDump()
{
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    // --- register param ---
    String regParam = wm.server->arg("register");
    if (regParam.length() == 0) regParam = "ram";
    regParam.toLowerCase();
    bool doRam         = regParam.indexOf("all") >= 0 || regParam.indexOf("ram")    >= 0;
    bool doEeprom      = regParam.indexOf("all") >= 0 || regParam.indexOf("eeprom") >= 0;
    bool filterUnknown = regParam.indexOf("unknown") >= 0;
    bool async = pc::parseOnOff(wm.server->arg("async").c_str()) == 1;
    String key = regParam + "|" + wm.server->arg("address");

    if (async && dumpJob && dumpJob->key == key && dumpJob->state == 2 && !dumpJob->delivered) {
        // the background dump of these parameters is done: send it (once)
        dumpJob->delivered = true;
        DumpJob* j = dumpJob;
        dumpWriteJson(j->doRam, j->doEeprom, j->filterUnknown, j->addrs, [j](byte type, byte addr) { return (int)j->val[type == eeprom ? 1 : 0][addr]; });
        return;
    }
    if (async && dumpJob && dumpJob->state == 1) {
        if (dumpJob->key != key) { wm.server->send(409, "application/json", "{\"error\":\"Another dump is running - try again in a few seconds\"}"); return; }
        char b[80]; snprintf(b, sizeof(b), "{\"state\":\"running\",\"done\":%d,\"total\":%d}", dumpJob->done, dumpJob->total);
        wm.server->send(202, "application/json", b);
        return;
    }
    if (stove.linkDown) {
        wm.server->send(503, "application/json", "{\"error\":\"The stove does not answer (serial connection) - try again later\"}");
        return;
    }
    // --- address param ---
    bool addrs[256];
    parseAddressList(wm.server->arg("address"), addrs);
    char src[96];
    snprintf(src, sizeof(src), "web/API %s (GET %s)", clientIp().c_str(), wm.server->uri().c_str());

    if (async) {   // start a background dump
        if (!dumpJob) dumpJob = new (std::nothrow) DumpJob();
        if (!dumpJob) { wm.server->send(503, "application/json", "{\"error\":\"Not enough memory\"}"); return; }
        DumpJob& j = *dumpJob;
        j.doRam = doRam; j.doEeprom = doEeprom; j.filterUnknown = filterUnknown;
        memcpy(j.addrs, addrs, sizeof(addrs));
        j.total = 0;
        for (int bank = 0; bank < 2; bank++) {
            if (!(bank ? doEeprom : doRam)) continue;
            for (int a = 0; a <= 0xFF; a++)
                if (addrs[a] && !(filterUnknown && (bank ? isKnownEepromAddr((byte)a) : isKnownRamAddr((byte)a)))) j.total++;
        }
        j.done = 0; j.delivered = false; j.key = key; j.tookMs = 0;
        strncpy(j.src, src, sizeof(j.src) - 1); j.src[sizeof(j.src) - 1] = 0;
        j.state = 1;
        if (xTaskCreatePinnedToCore(dumpTask, "dump", 4096, nullptr, 1, nullptr, 1) != pdPASS) {
            j.state = 0;
            wm.server->send(503, "application/json", "{\"error\":\"Could not start the dump task (memory)\"}");
            return;
        }
        char b[80]; snprintf(b, sizeof(b), "{\"state\":\"running\",\"done\":0,\"total\":%d}", j.total);
        wm.server->send(202, "application/json", b);
        return;
    }
    // Synchronous: read while answering. The bus lock is taken per register (not for the whole
    // dump), so the sensor task can read in between; the loop task is busy all that time.
    int failedInRow = 0;
    dumpWriteJson(doRam, doEeprom, filterUnknown, addrs, [&failedInRow, &src](byte type, byte addr) {
        int val = -1;
        // stove gone during the dump: the remaining registers are reported as
        // read errors (-1) without trying them, instead of 3 attempts each
        if (failedInRow < 3 && !stove.linkDown && stoveLockTake(3000)) {
            CmdSource cs(src);
            val = stove.read(type, addr);
            if (stoveMutex) xSemaphoreGiveRecursive(stoveMutex);
            failedInRow = val < 0 ? failedInRow + 1 : 0;
        }
        return val;
    });
}

int planRemoteMenuFresh();   // stoveCache.h
int planStoveLanguage();
void handleRouteRemote()
{
    StaticJsonDocument<200> json;
    json.clear();

    if (wm.server->method() != HTTP_GET)
    {
        wm.server->send(405);
        return;
    }

    // menu and language from the read planner when it has them (stoveCache.h): on the main screen this
    // route then needs no bus read at all; the selected parameter and its value only while a menu is open
    int menuId = planRemoteMenuFresh();
    if (menuId < 0) menuId = stove.read(ram, displayMenuAddr);
    if (menuId < 0)
    {
        json["error"] = "Failed to read remote menu - RS232 error";
        sendJson(500, json);
        return;
    }

    const char* menuName = getRemoteMenuName(menuId);
    json["displayed_menu"] = menuName;
    json["diplayed_menu"]  = menuName;  // deprecated misspelled alias, kept for existing clients

    json["menuId"] = menuId;
    int paramId = menuId > 0 ? stove.read(ram, selectedParamAddr) : 0;
    json["selected_parameter"] = (paramId >= 0) ? getSelectedParameterName(paramId) : "Unknown";
    if (paramId >= 0) json["parameterId"] = paramId;          // RAM 0x18, 0 = not editing
    int editVal = menuId > 0 ? stove.read(ram, menuEditValueAddr) : -1;
    if (editVal >= 0) json["editValue"] = editVal;            // RAM 0x19: value the remote shows while editing

    int langId = planStoveLanguage();
    if (langId < 0) langId = stove.read(eeprom, languageAddr);
    if (langId < 0)
    {
        json["language"] = "Unknown";
    }
    else
    {
        switch (langId)
        {
            case 1: json["language"] = "ENG"; break;
            case 2: json["language"] = "FRA"; break;
            case 3: json["language"] = "DEU"; break;
            case 4: json["language"] = "NLD"; break;
            case 5: json["language"] = "POR"; break;
            case 6: json["language"] = "SPA"; break;
            case 7: json["language"] = "HRV"; break;
            case 8: json["language"] = "DAN"; break;
            default: json["language"] = "Unknown"; break;
        }
    }

    sendJson(200, json);
}
