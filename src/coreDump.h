#pragma once
// Core dump after a crash (panic, task watchdog): the ESP-IDF writes it to the
// coredump partition (min_spiffs.csv, 64 KB, ELF format). At the start after a crash
// the summary (task, PC, backtrace) goes to the log; GET /api/coredump returns it,
// ?download=true the raw dump for
//   esp-coredump info_corefile -t raw -c coredump.bin firmware.elf
// (firmware.elf of the same build: CI artifact openpelletbridge-elf-<version>).
// The backtrace alone: xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf <addresses>.
// DELETE /api/coredump erases it.
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_spi_flash.h>

static String coreDumpText;   // summary of the dump found at the start, "" = none

// Summary of the stored dump; false when there is none (or it is not valid)
static bool coreDumpSummary(esp_core_dump_summary_t& s)
{
    if (esp_core_dump_image_check() != ESP_OK) return false;
    return esp_core_dump_get_summary(&s) == ESP_OK;
}
static bool coreDumpFromThisBuild(const esp_core_dump_summary_t& s)
{
    char sha[APP_ELF_SHA256_SZ] = "";
    esp_ota_get_app_elf_sha256(sha, sizeof(sha));
    return strncmp(sha, (const char*)s.app_elf_sha256, sizeof(sha) - 1) == 0;
}
static String coreDumpBacktrace(const esp_core_dump_summary_t& s)
{
    String bt;
    for (uint32_t i = 0; i < s.exc_bt_info.depth && i < 16; i++) {
        char a[12];
        snprintf(a, sizeof(a), "%s0x%08lx", i ? " " : "", (unsigned long)s.exc_bt_info.bt[i]);
        bt += a;
    }
    if (s.exc_bt_info.corrupted) bt += " (corrupted)";
    return bt;
}

// At the start (setup, after liveLogSetup): log the dump of the crash we restarted from
void coreDumpReport()
{
    if (!resetIsCrash(diagReason) || diagReason == ESP_RST_BROWNOUT) return;   // a brown-out writes no dump
    esp_core_dump_summary_t s;
    if (!coreDumpSummary(s)) { LOGW("sys", "no core dump stored for this crash"); return; }
    char b[96];
    snprintf(b, sizeof(b), "task %s, PC 0x%08lx, cause %lu", s.exc_task, (unsigned long)s.exc_pc, (unsigned long)s.ex_info.exc_cause);
    coreDumpText = String(b) + ", backtrace " + coreDumpBacktrace(s);
    LOGE("sys", "core dump of the crash: %s%s - GET /api/coredump?download=true for the full dump", coreDumpText.c_str(),
         coreDumpFromThisBuild(s) ? "" : " (from another firmware build)");
}

// GET  /api/coredump                -> summary
// GET  /api/coredump?download=true  -> raw dump (application/octet-stream)
// DELETE /api/coredump              -> erase it
void handleRouteCoreDump()
{
    if (wm.server->method() == HTTP_DELETE) {
        esp_err_t e = esp_core_dump_image_erase();
        coreDumpText = "";
        LOGI("sys", "core dump erased by %s", wm.server->client().remoteIP().toString().c_str());
        wm.server->send(e == ESP_OK ? 200 : 500, "application/json", e == ESP_OK ? "{\"result\":\"success\"}" : "{\"result\":\"failed - could not erase\"}");
        return;
    }
    if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    size_t addr = 0, size = 0;
    bool present = esp_core_dump_image_check() == ESP_OK && esp_core_dump_image_get(&addr, &size) == ESP_OK && size;
    if (pc::parseOnOff(wm.server->arg("download").c_str()) == 1) {
        if (!present) { wm.server->send(404, "application/json", "{\"result\":\"failed - no core dump stored\"}"); return; }
        wm.server->sendHeader("Content-Disposition", "attachment; filename=\"coredump.bin\"");
        wm.server->setContentLength(size);
        wm.server->send(200, "application/octet-stream", "");
        static uint8_t buf[1024];
        WiFiClient c = wm.server->client();
        for (size_t done = 0; done < size && !httpStalled; ) {
            size_t n = size - done < sizeof(buf) ? size - done : sizeof(buf);
            if (spi_flash_read(addr + done, buf, n) != ESP_OK) { c.stop(); break; }
            if (!httpClientWritable(3000)) { httpStall(); break; }
            if (c.write(buf, n) != n) { c.stop(); break; }
            done += n;
            esp_task_wdt_reset();
        }
        return;
    }
    DynamicJsonDocument doc(1024);
    esp_core_dump_summary_t s;
    if (present && coreDumpSummary(s)) {
        doc["present"] = true;
        doc["size"] = (uint32_t)size;
        doc["task"] = s.exc_task;
        char pcs[12]; snprintf(pcs, sizeof(pcs), "0x%08lx", (unsigned long)s.exc_pc);
        doc["pc"] = pcs;
        doc["cause"] = s.ex_info.exc_cause;
        doc["backtrace"] = coreDumpBacktrace(s);
        doc["thisBuild"] = coreDumpFromThisBuild(s);
        doc["elfSha256"] = (const char*)s.app_elf_sha256;
    } else {
        doc["present"] = false;
    }
    String out; serializeJson(doc, out);
    wm.server->send(200, "application/json", out);
}
