// Unit tests for lib/pelletcore (pure logic of the firmware).
// Run on the development machine: pio test -e native
#include <unity.h>
#include <pelletcore.h>

void setUp() {}
void tearDown() {}

// ── Chrono timer times ──────────────────────────────────────────────────────
static void test_timer_from_str()
{
    TEST_ASSERT_EQUAL_INT(0,   pc::timerValueFromStr("00:00"));
    TEST_ASSERT_EQUAL_INT(42,  pc::timerValueFromStr("07:00"));
    TEST_ASSERT_EQUAL_INT(51,  pc::timerValueFromStr("08:30"));
    TEST_ASSERT_EQUAL_INT(143, pc::timerValueFromStr("23:50"));
    TEST_ASSERT_EQUAL_INT(144, pc::timerValueFromStr("off"));
    TEST_ASSERT_EQUAL_INT(42,  pc::timerValueFromStr("7:00"));
}
static void test_timer_from_str_invalid()
{
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr("07:05"));   // not on a 10 minute boundary
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr("24:00"));
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr("12:60"));
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr("-1:00"));
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr("noon"));
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr("07:00x"));
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr("OFF"));      // only lower case "off", as documented
    TEST_ASSERT_EQUAL_INT(-1, pc::timerValueFromStr(nullptr));
}
static void test_timer_to_str()
{
    char b[6];
    TEST_ASSERT_EQUAL_STRING("00:00", pc::timerValueToStr(0, b));
    TEST_ASSERT_EQUAL_STRING("07:00", pc::timerValueToStr(42, b));
    TEST_ASSERT_EQUAL_STRING("23:50", pc::timerValueToStr(143, b));
    TEST_ASSERT_EQUAL_STRING("off",   pc::timerValueToStr(144, b));
    TEST_ASSERT_EQUAL_STRING("off",   pc::timerValueToStr(200, b));
    TEST_ASSERT_EQUAL_STRING("off",   pc::timerValueToStr(-1, b));
}
static void test_timer_round_trip()
{
    char b[6];
    for (int v = 0; v <= 144; v++)
        TEST_ASSERT_EQUAL_INT(v, pc::timerValueFromStr(pc::timerValueToStr(v, b)));
}

// ── Clock ───────────────────────────────────────────────────────────────────
static void test_bcd()
{
    TEST_ASSERT_EQUAL_HEX8(0x00, pc::toBcd(0));
    TEST_ASSERT_EQUAL_HEX8(0x16, pc::toBcd(16));
    TEST_ASSERT_EQUAL_HEX8(0x59, pc::toBcd(59));
    TEST_ASSERT_EQUAL_HEX8(0x99, pc::toBcd(99));
    for (int i = 0; i <= 99; i++) TEST_ASSERT_EQUAL_INT(i, pc::fromBcd(pc::toBcd(i)));
}
static void test_extrapolate_time()
{
    TEST_ASSERT_EQUAL_INT(0, pc::extrapolateTime(0, 1000, 99000));                  // unknown stays unknown
    TEST_ASSERT_EQUAL_INT(1000, pc::extrapolateTime(1000, 5000, 5999));             // < 1 s: no change
    TEST_ASSERT_EQUAL_INT(1300, pc::extrapolateTime(1000, 5000, 305000));           // 5 min later
    TEST_ASSERT_EQUAL_INT(1010, pc::extrapolateTime(1000, 0xFFFFF000UL, 0x00001710UL)); // across a millis() wrap: 10 s
}
static void test_valid_stove_clock()
{
    TEST_ASSERT_TRUE(pc::validStoveClock(0, 0, 0, 1, 1, 0));
    TEST_ASSERT_TRUE(pc::validStoveClock(59, 59, 23, 31, 12, 99));
    TEST_ASSERT_FALSE(pc::validStoveClock(60, 0, 0, 1, 1, 26));
    TEST_ASSERT_FALSE(pc::validStoveClock(0, 75, 0, 1, 1, 26));
    TEST_ASSERT_FALSE(pc::validStoveClock(0, 0, 24, 1, 1, 26));
    TEST_ASSERT_FALSE(pc::validStoveClock(0, 0, 0, 0, 1, 26));
    TEST_ASSERT_FALSE(pc::validStoveClock(0, 0, 0, 1, 13, 26));
    TEST_ASSERT_FALSE(pc::validStoveClock(-1, 0, 0, 1, 1, 26));   // read error
}

// ── Fans ────────────────────────────────────────────────────────────────────
static void test_fume_fan_rpm()
{
    TEST_ASSERT_EQUAL_INT(0,    pc::fumeFanRpm(0));      // stopped: not 250
    TEST_ASSERT_EQUAL_INT(-1,   pc::fumeFanRpm(-1));     // read error passed through
    TEST_ASSERT_EQUAL_INT(1590, pc::fumeFanRpm(134));    // flame 1 (measured 16/11/2026)
    TEST_ASSERT_EQUAL_INT(1800, pc::fumeFanRpm(155));
    TEST_ASSERT_EQUAL_INT(2630, pc::fumeFanRpm(238));    // brazier cleaning
}
static void test_room_fan()
{
    // power (RAM 0x0A): -1 unknown, 0 off, > 0 on (21/23/25 at flame 2, 9/14/19 at flame 1, 27 at flame 3)
    TEST_ASSERT_EQUAL_INT(-1, pc::roomFanActual(-1, 2));
    TEST_ASSERT_EQUAL_INT(0,  pc::roomFanActual(0, 2));
    TEST_ASSERT_EQUAL_INT(2,  pc::roomFanActual(23, 2));
    TEST_ASSERT_EQUAL_INT(1,  pc::roomFanActual(9, 1));      // reduced power at flame 1 is still speed 1
    TEST_ASSERT_NULL(pc::roomFanMode(-1));
    TEST_ASSERT_EQUAL_STRING("off", pc::roomFanMode(0));
    TEST_ASSERT_EQUAL_STRING("on",  pc::roomFanMode(27));
    TEST_ASSERT_EQUAL_INT(1,  pc::roomFanIsRunning(21, 0));
    TEST_ASSERT_EQUAL_INT(0,  pc::roomFanIsRunning(0, 1));   // 0xD3 stays 1 when stopped: ignored once 0x0A is known
    TEST_ASSERT_EQUAL_INT(1,  pc::roomFanIsRunning(-1, 2));  // fallback to 0xD3
    TEST_ASSERT_EQUAL_INT(-1, pc::roomFanIsRunning(-1, -1));
}

// ── Pellets and errors ──────────────────────────────────────────────────────
static void test_pellets_low()
{
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(0, 20));
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(5, 20));
    TEST_ASSERT_EQUAL_INT(1, pc::pelletsLow(5, 1));
    TEST_ASSERT_EQUAL_INT(1, pc::pelletsLow(5, 0));
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(5, -1));   // counter not read
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(0, 0));
    TEST_ASSERT_EQUAL_INT(1, pc::pelletsLow(4, 0));    // 4: seen briefly during the reserve
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(4, 20));
    TEST_ASSERT_EQUAL_INT(1, pc::pelletsLow(3, 0));    // 3: seen during the refill (5 -> 3 -> 0)
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(6, 0));    // never seen: not low
}
static void test_error_confirm_prewarning_ignored()
{
    // E12 (129) shows for a few seconds at brazier cleaning: never confirmed
    pc::ErrorConfirm ec;
    TEST_ASSERT_EQUAL_INT(0, ec.update(129, 5, 1000, 30000));
    TEST_ASSERT_EQUAL_INT(0, ec.update(129, 5, 4000, 30000));
    TEST_ASSERT_EQUAL_INT(0, ec.update(0, 4, 6000, 30000));
    TEST_ASSERT_EQUAL_INT(0, ec.update(129, 4, 50000, 30000));   // new occurrence restarts the timer
}
static void test_error_confirm_after_delay()
{
    pc::ErrorConfirm ec;
    TEST_ASSERT_EQUAL_INT(0,   ec.update(129, 4, 1000, 30000));
    TEST_ASSERT_EQUAL_INT(0,   ec.update(129, 4, 30999, 30000));
    TEST_ASSERT_EQUAL_INT(129, ec.update(129, 4, 31000, 30000));
    TEST_ASSERT_EQUAL_INT(0,   ec.update(0, 4, 32000, 30000));   // cleared at once
}
static void test_error_confirm_startup_check()
{
    // E1 (32) for ~45 s during the start-up check (state 1): not an error
    pc::ErrorConfirm ec;
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 1, 1000, 30000, 180000));
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 1, 46000, 30000, 180000));
    TEST_ASSERT_EQUAL_INT(0,  ec.update(0, 2, 47000, 30000, 180000));
    // still there after 3 minutes in a transitional state: counts
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 3, 100000, 30000, 180000));
    TEST_ASSERT_EQUAL_INT(32, ec.update(32, 3, 280000, 30000, 180000));
}
static void test_error_confirm_after_transition()
{
    // seen during ignition, still there 30 s after first seen once the stove works: counts
    pc::ErrorConfirm ec;
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 3, 1000, 30000, 180000));
    TEST_ASSERT_EQUAL_INT(32, ec.update(32, 4, 31000, 30000, 180000));
}
static void test_error_confirm_alarm_state_immediate()
{
    pc::ErrorConfirm ec;
    TEST_ASSERT_EQUAL_INT(4, ec.update(4, 9, 1000, 30000));   // E9 with "ignition failure" state
}

// ── Quality labels ──────────────────────────────────────────────────────────
static void test_combustion_quality()
{
    TEST_ASSERT_EQUAL_STRING("+G", pc::combustionQualityLabel(76));
    TEST_ASSERT_EQUAL_STRING("-1", pc::combustionQualityLabel(4));
    TEST_ASSERT_NULL(pc::combustionQualityLabel(9));
    TEST_ASSERT_EQUAL_INT(76, pc::combustionQualityFromLabel("+G"));
    TEST_ASSERT_EQUAL_INT(76, pc::combustionQualityFromLabel("g"));
    TEST_ASSERT_EQUAL_INT(4,  pc::combustionQualityFromLabel("-1"));
    TEST_ASSERT_EQUAL_INT(6,  pc::combustionQualityFromLabel("+1"));
    TEST_ASSERT_EQUAL_INT(6,  pc::combustionQualityFromLabel(" 1 "));
    TEST_ASSERT_EQUAL_INT(-1, pc::combustionQualityFromLabel("4"));
    TEST_ASSERT_EQUAL_INT(-1, pc::combustionQualityFromLabel(""));
    for (int v = 2; v <= 8; v++) TEST_ASSERT_EQUAL_INT(v, pc::combustionQualityFromLabel(pc::combustionQualityLabel(v)));
}
static void test_pellet_quality()
{
    TEST_ASSERT_EQUAL_STRING("-5", pc::pelletQualityLabel(0));
    TEST_ASSERT_EQUAL_STRING("0",  pc::pelletQualityLabel(5));
    TEST_ASSERT_EQUAL_STRING("5",  pc::pelletQualityLabel(10));
    TEST_ASSERT_NULL(pc::pelletQualityLabel(11));
    TEST_ASSERT_NULL(pc::pelletQualityLabel(-1));
    for (int v = 0; v <= 10; v++) TEST_ASSERT_EQUAL_INT(v, pc::pelletQualityFromLabel(pc::pelletQualityLabel(v)));
    TEST_ASSERT_EQUAL_INT(7,  pc::pelletQualityFromLabel("+2"));
    TEST_ASSERT_EQUAL_INT(-1, pc::pelletQualityFromLabel("6"));
}

// ── Address lists ───────────────────────────────────────────────────────────
static void test_address_list()
{
    bool a[256];
    TEST_ASSERT_EQUAL_INT(256, pc::parseAddressList("", a));
    TEST_ASSERT_TRUE(a[0] && a[255]);
    TEST_ASSERT_EQUAL_INT(3, pc::parseAddressList("0x21, 52,0X35", a));
    TEST_ASSERT_TRUE(a[0x21]); TEST_ASSERT_TRUE(a[52]); TEST_ASSERT_TRUE(a[0x35]);
    TEST_ASSERT_EQUAL_INT(1, pc::parseAddressList("0x34,52", a));   // 0x34 == 52: counted once
    TEST_ASSERT_FALSE(a[0x22]);
    TEST_ASSERT_EQUAL_INT(1, pc::parseAddressList("0x21,0x21,300,-1,abc,,0x1G", a));   // duplicates and invalid tokens skipped
    TEST_ASSERT_TRUE(a[0x21]);
}

// ── MQTT payloads ───────────────────────────────────────────────────────────
static void test_parse_on_off()
{
    TEST_ASSERT_EQUAL_INT(1,  pc::parseOnOff("ON"));
    TEST_ASSERT_EQUAL_INT(1,  pc::parseOnOff("on"));
    TEST_ASSERT_EQUAL_INT(1,  pc::parseOnOff("1"));
    TEST_ASSERT_EQUAL_INT(1,  pc::parseOnOff("true"));
    TEST_ASSERT_EQUAL_INT(0,  pc::parseOnOff("OFF"));
    TEST_ASSERT_EQUAL_INT(0,  pc::parseOnOff("0"));
    TEST_ASSERT_EQUAL_INT(0,  pc::parseOnOff(" false "));
    TEST_ASSERT_EQUAL_INT(-1, pc::parseOnOff("2"));
    TEST_ASSERT_EQUAL_INT(-1, pc::parseOnOff(""));
    TEST_ASSERT_EQUAL_INT(-1, pc::parseOnOff(nullptr));
}
static void test_parse_int_range()
{
    int v = 0;
    TEST_ASSERT_TRUE(pc::parseIntRange("21", 7, 30, v));    TEST_ASSERT_EQUAL_INT(21, v);
    TEST_ASSERT_TRUE(pc::parseIntRange("21.0", 7, 30, v));  TEST_ASSERT_EQUAL_INT(21, v);   // Home Assistant number entity
    TEST_ASSERT_TRUE(pc::parseIntRange(" 7 ", 7, 30, v));   TEST_ASSERT_EQUAL_INT(7, v);
    TEST_ASSERT_FALSE(pc::parseIntRange("21.5", 7, 30, v));
    TEST_ASSERT_FALSE(pc::parseIntRange("6", 7, 30, v));
    TEST_ASSERT_FALSE(pc::parseIntRange("31", 7, 30, v));
    TEST_ASSERT_FALSE(pc::parseIntRange("abc", 7, 30, v));
    TEST_ASSERT_FALSE(pc::parseIntRange("21abc", 7, 30, v));
    TEST_ASSERT_FALSE(pc::parseIntRange("", 7, 30, v));
}
static void test_parse_fan_speed()
{
    int v = -1;
    TEST_ASSERT_TRUE(pc::parseFanSpeed("off", v)); TEST_ASSERT_EQUAL_INT(0, v);
    TEST_ASSERT_TRUE(pc::parseFanSpeed("OFF", v)); TEST_ASSERT_EQUAL_INT(0, v);
    TEST_ASSERT_TRUE(pc::parseFanSpeed("3", v));   TEST_ASSERT_EQUAL_INT(3, v);
    TEST_ASSERT_FALSE(pc::parseFanSpeed("4", v));
    TEST_ASSERT_FALSE(pc::parseFanSpeed("high", v));
}

// ── Network settings ────────────────────────────────────────────────────────
static void test_hostname()
{
    TEST_ASSERT_TRUE(pc::validHostname("openpelletbridge"));
    TEST_ASSERT_TRUE(pc::validHostname("kachel-2"));
    TEST_ASSERT_FALSE(pc::validHostname(""));
    TEST_ASSERT_FALSE(pc::validHostname("-kachel"));
    TEST_ASSERT_FALSE(pc::validHostname("kachel-"));
    TEST_ASSERT_FALSE(pc::validHostname("kachel woonkamer"));
    TEST_ASSERT_FALSE(pc::validHostname("kachel.local"));
    TEST_ASSERT_FALSE(pc::validHostname("abcdefghijklmnopqrstuvwxyz0123456"));   // 33 chars
}
static void test_ipv4()
{
    uint32_t v = 0;
    TEST_ASSERT_TRUE(pc::ipv4FromStr("192.168.50.222", v)); TEST_ASSERT_EQUAL_HEX32(0xC0A832DE, v);
    TEST_ASSERT_FALSE(pc::ipv4FromStr("192.168.50", v));
    TEST_ASSERT_FALSE(pc::ipv4FromStr("192.168.50.256", v));
    TEST_ASSERT_FALSE(pc::ipv4FromStr("192.168.50.1x", v));
    TEST_ASSERT_TRUE(pc::validNetmask(0xFFFFFF00));
    TEST_ASSERT_TRUE(pc::validNetmask(0xFFFF0000));
    TEST_ASSERT_FALSE(pc::validNetmask(0xFF00FF00));
    TEST_ASSERT_FALSE(pc::validNetmask(0));
}
static void test_check_static_ip()
{
    TEST_ASSERT_NULL(pc::checkStaticIp("192.168.50.222", "255.255.255.0", "192.168.50.1", "192.168.50.1"));
    TEST_ASSERT_NOT_NULL(pc::checkStaticIp("192.168.50.222", "255.255.255.0", "192.168.1.1", "1.1.1.1"));   // gateway elsewhere
    TEST_ASSERT_NOT_NULL(pc::checkStaticIp("192.168.50.0", "255.255.255.0", "192.168.50.1", "1.1.1.1"));    // network address
    TEST_ASSERT_NOT_NULL(pc::checkStaticIp("192.168.50.255", "255.255.255.0", "192.168.50.1", "1.1.1.1"));  // broadcast
    TEST_ASSERT_NOT_NULL(pc::checkStaticIp("192.168.50.1", "255.255.255.0", "192.168.50.1", "1.1.1.1"));    // = gateway
    TEST_ASSERT_NOT_NULL(pc::checkStaticIp("192.168.50.222", "255.0.255.0", "192.168.50.1", "1.1.1.1"));
    TEST_ASSERT_NOT_NULL(pc::checkStaticIp("192.168.50.222", "255.255.255.0", "192.168.50.1", "dns"));
}
static void test_wifi_credentials()
{
    TEST_ASSERT_TRUE(pc::validSsid("Thuis"));
    TEST_ASSERT_FALSE(pc::validSsid(""));
    TEST_ASSERT_FALSE(pc::validSsid("abcdefghijklmnopqrstuvwxyz0123456"));   // 33
    TEST_ASSERT_TRUE(pc::validWifiPassword(""));          // open network
    TEST_ASSERT_TRUE(pc::validWifiPassword("12345678"));
    TEST_ASSERT_FALSE(pc::validWifiPassword("1234567"));   // WPA2 needs 8+
    TEST_ASSERT_FALSE(pc::validWifiPassword(nullptr));
}
static void test_rssi_quality()
{
    TEST_ASSERT_EQUAL_INT(100, pc::rssiQuality(-40));
    TEST_ASSERT_EQUAL_INT(60,  pc::rssiQuality(-70));
    TEST_ASSERT_EQUAL_INT(0,   pc::rssiQuality(-100));
    TEST_ASSERT_EQUAL_INT(0,   pc::rssiQuality(0));
}
static void test_parse_version()
{
    pc::Version v;
    TEST_ASSERT_TRUE(pc::parseVersion("v1.2.3", v));
    TEST_ASSERT_EQUAL_INT(1, v.major); TEST_ASSERT_EQUAL_INT(2, v.minor); TEST_ASSERT_EQUAL_INT(3, v.patch);
    TEST_ASSERT_EQUAL_INT(0, v.ahead); TEST_ASSERT_FALSE(v.dirty);
    TEST_ASSERT_TRUE(pc::parseVersion("v1.2.0-3-gabc1234-dirty", v));
    TEST_ASSERT_EQUAL_INT(3, v.ahead); TEST_ASSERT_TRUE(v.dirty);
    TEST_ASSERT_TRUE(pc::parseVersion("2.10", v));
    TEST_ASSERT_EQUAL_INT(10, v.minor); TEST_ASSERT_EQUAL_INT(0, v.patch);
    TEST_ASSERT_FALSE(pc::parseVersion("da34168", v));       // bare hash: no tags yet
    TEST_ASSERT_FALSE(pc::parseVersion("1234abc", v));       // hash starting with digits
    TEST_ASSERT_FALSE(pc::parseVersion("unknown", v));
    TEST_ASSERT_FALSE(pc::parseVersion(nullptr, v));
}
static void test_update_available()
{
    TEST_ASSERT_TRUE (pc::updateAvailable("v1.0.0", "v1.0.1"));
    TEST_ASSERT_TRUE (pc::updateAvailable("v1.9.0", "v1.10.0"));   // numeric, not text order
    TEST_ASSERT_TRUE (pc::updateAvailable("v1.0.0-5-gabc1234", "v1.1.0"));
    TEST_ASSERT_FALSE(pc::updateAvailable("v1.1.0", "v1.1.0"));
    TEST_ASSERT_FALSE(pc::updateAvailable("v1.1.0-2-gabc1234", "v1.1.0"));   // newer than the release
    TEST_ASSERT_FALSE(pc::updateAvailable("v2.0.0", "v1.9.9"));
    TEST_ASSERT_FALSE(pc::updateAvailable("da34168", "v1.0.0"));    // development build
    TEST_ASSERT_FALSE(pc::updateAvailable("v1.0.0", "nightly"));
}

static void test_stove_state()
{
    TEST_ASSERT_EQUAL_STRING("Off", pc::stoveStateName(0));
    TEST_ASSERT_EQUAL_STRING("Working", pc::stoveStateName(4));
    TEST_ASSERT_EQUAL_STRING("Brazier cleaning", pc::stoveStateName(5));
    TEST_ASSERT_EQUAL_STRING("No answer (serial link down)", pc::stoveStateName(-1));
    TEST_ASSERT_EQUAL_STRING("Unknown", pc::stoveStateName(42));
    // on: starting .. brazier cleaning, standby, alarm (shutdown still needed)
    const bool on[] = { false, true, true, true, true, true, false, true, true, false, false };
    for (int st = 0; st <= 10; st++) TEST_ASSERT_EQUAL_MESSAGE(on[st], pc::stoveStateIsOn(st), pc::stoveStateName(st));
    TEST_ASSERT_FALSE(pc::stoveStateIsOn(-1));
}
static void test_error_table()
{
    TEST_ASSERT_EQUAL_STRING("E1",  pc::errorCode(32));    // false alarm during start-up check (state 1)
    TEST_ASSERT_EQUAL_STRING("E9",  pc::errorCode(4));
    TEST_ASSERT_EQUAL_STRING("E12", pc::errorCode(129));
    TEST_ASSERT_EQUAL_STRING("---", pc::errorCode(0));
    TEST_ASSERT_EQUAL_STRING("---", pc::errorCode(77));             // unknown value
    TEST_ASSERT_EQUAL_STRING("Unknown error", pc::errorDesc(77));
    TEST_ASSERT_EQUAL_STRING("No Error", pc::errorDesc(0));
    int n; const pc::ErrorInfo* t = pc::errorTable(n);
    TEST_ASSERT_TRUE(n >= 10);
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(t[i].code[0] && t[i].desc[0]);
        for (int j = i + 1; j < n; j++) {
            TEST_ASSERT_NOT_EQUAL(t[i].id, t[j].id);                  // ids unique
            if (strcmp(t[i].code, "---") != 0) TEST_ASSERT_TRUE(strcmp(t[i].code, t[j].code) != 0);   // codes unique
        }
        TEST_ASSERT_NULL(strstr(t[i].desc, " to high"));              // spelling regressions
        TEST_ASSERT_NULL(strstr(t[i].desc, " to low"));
        TEST_ASSERT_NULL(strstr(t[i].desc, "allet"));
    }
}
static void test_firmware_asset()
{
    TEST_ASSERT_EQUAL_INT(2, pc::firmwareAssetRank("openpelletbridge-firmware.bin"));
    TEST_ASSERT_EQUAL_INT(1, pc::firmwareAssetRank("palletheater-firmware.bin"));   // controllers up to v1.0.0
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank("openpelletbridge-factory.bin")); // never the full flash image
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank("SHA256SUMS"));
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank(""));
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank(nullptr));
    // the old name must stay a fallback: it may not win over the current one
    TEST_ASSERT_TRUE(pc::firmwareAssetRank(PC_FW_ASSET) > pc::firmwareAssetRank(PC_FW_ASSET_OLD));
}

static void test_text_ring()
{
    pc::TextRing<8> r; char out[16]; uint32_t from; bool lost;
    r.write("abc", 3);
    size_t n = r.read(0, out, sizeof(out), from, lost);
    TEST_ASSERT_EQUAL(3, n); TEST_ASSERT_EQUAL_MEMORY("abc", out, 3); TEST_ASSERT_FALSE(lost); TEST_ASSERT_EQUAL(0, from);
    TEST_ASSERT_EQUAL(0, r.read(3, out, sizeof(out), from, lost));          // up to date
    r.write("defghijk", 8);                                                  // total 11, keeps "defghijk"
    n = r.read(3, out, sizeof(out), from, lost);
    TEST_ASSERT_EQUAL(8, n); TEST_ASSERT_EQUAL_MEMORY("defghijk", out, 8); TEST_ASSERT_FALSE(lost);
    n = r.read(1, out, sizeof(out), from, lost);                             // cursor overwritten
    TEST_ASSERT_TRUE(lost); TEST_ASSERT_EQUAL(3, from); TEST_ASSERT_EQUAL(8, n);
    n = r.read(500, out, sizeof(out), from, lost);                           // cursor from before a restart
    TEST_ASSERT_FALSE(lost); TEST_ASSERT_EQUAL(3, from);
    n = r.read(3, out, 2, from, lost);                                        // max respected
    TEST_ASSERT_EQUAL(2, n); TEST_ASSERT_EQUAL_MEMORY("de", out, 2);
}
static void test_ring()
{
    pc::Ring<int, 4> r; uint32_t from; bool lost;
    for (int i = 0; i < 6; i++) r.push(i * 10);                              // keeps 20, 30, 40, 50
    size_t n = r.read(0, 100, from, lost);
    TEST_ASSERT_EQUAL(4, n); TEST_ASSERT_EQUAL(2, from); TEST_ASSERT_FALSE(lost);   // since 0 = "from the start", not lost
    TEST_ASSERT_EQUAL(20, r.at(from)); TEST_ASSERT_EQUAL(50, r.at(from + 3));
    n = r.read(1, 100, from, lost); TEST_ASSERT_TRUE(lost); TEST_ASSERT_EQUAL(4, n);
    n = r.read(5, 100, from, lost); TEST_ASSERT_EQUAL(1, n); TEST_ASSERT_EQUAL(50, r.at(from));
    n = r.read(6, 100, from, lost); TEST_ASSERT_EQUAL(0, n);
    n = r.read(2, 2, from, lost);   TEST_ASSERT_EQUAL(2, n);
}
static pc::HourInput hourIn(uint32_t t, int room10, int state, int flameAct, int auger10)
{
    pc::HourInput s = {}; s.t = t; s.room10 = room10; s.stove10 = 200; s.remote10 = 205;
    s.setpoint = 20; s.fume = 100; s.flame = 3; s.flameActual = flameAct; s.fan = 1; s.fanActual = 1;
    s.state = state; s.auger10 = auger10; s.fumeRpm = 1500;
    return s;
}
// Feed the tracker one reading per 10 s for `secs` seconds
static uint32_t pelletRun(pc::PelletTracker& p, uint32_t t, uint32_t secs, int sensor, float auger)
{
    for (uint32_t i = 0; i < secs; i += 10) { t += 10; p.update(t, sensor, auger); }
    return t;
}
// The auger value is the on-time per ~12 s cycle: the pre-load value 20.0 counts as the
// full cycle (the motor cannot run more than all the time)
static void test_pellet_units_capped()
{
    pc::PelletTracker a, b, c;
    uint32_t t = 1800000000;
    for (int i = 0; i <= 12; i++) { a.update(t + i * 10, 0, 20.0f); b.update(t + i * 10, 0, 12.0f); c.update(t + i * 10, 0, 6.2f); }
    TEST_ASSERT_EQUAL_FLOAT(12.0f * 120, (float)a.units);
    TEST_ASSERT_EQUAL_FLOAT((float)b.units, (float)a.units);
    TEST_ASSERT_EQUAL_FLOAT(6.2f * 120, (float)c.units);
}
static void test_pellet_tracker()
{
    pc::PelletTracker p;
    uint32_t t = 1800000000;
    p.update(t, 0, 6.2f);
    TEST_ASSERT_EQUAL_FLOAT(-1, (float)p.remainingKg());          // no trip seen yet
    // a short flicker of the sensor is no trip
    t = pelletRun(p, t, 30, 5, 6.2f);
    t = pelletRun(p, t, 30, 0, 6.2f);
    TEST_ASSERT_EQUAL(0, p.low);
    // first trip: starts the first cycle, nothing to calibrate yet
    uint32_t tripAt = t;
    t = pelletRun(p, t, 120, 5, 6.2f);
    TEST_ASSERT_EQUAL(1, p.low);
    TEST_ASSERT_EQUAL(0, p.nCycles);
    TEST_ASSERT_TRUE(p.cycleStart > 0);
    (void)tripAt;
    // refill: sensor back to normal for 30 s -> 15 kg
    t = pelletRun(p, t, 60, 0, 6.2f);
    TEST_ASSERT_EQUAL(0, p.low);
    TEST_ASSERT_EQUAL_FLOAT(15, p.cycleKg);
    TEST_ASSERT_EQUAL(t - 20, p.lastRefillT);                       // clear from t-50, confirmed 30 s later
    TEST_ASSERT_EQUAL_FLOAT(15, p.lastRefillKg);
    // burn 10 h at 6.2, then the next trip: 15 kg = units of the cycle
    double before = p.units;
    t = pelletRun(p, t, 36000, 0, 6.2f);
    t = pelletRun(p, t, 70, 5, 6.2f);
    TEST_ASSERT_EQUAL(1, p.nCycles);
    TEST_ASSERT_EQUAL(1, p.low);
    double k = p.kgPerUnit();
    TEST_ASSERT_TRUE(k > 0);
    // the cycle covers the 10 h plus the reserve burned before the refill
    TEST_ASSERT_TRUE(p.cycles[0].units > (float)(before - p.cycleStart));
    TEST_ASSERT_EQUAL_FLOAT(0, (float)p.remainingKg());           // low: at the sensor level
    // refill with a hand-entered amount while low: 10 kg instead of the default
    p.addKg(10, t);
    t = pelletRun(p, t, 60, 0, 6.2f);
    TEST_ASSERT_EQUAL_FLOAT(10, p.cycleKg);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 10, (float)p.remainingKg());
    // half of it burned: about 5 kg left; hours at 6.2 match the burn rate
    double rate = k * 6.2 * 3600.0;                                 // kg per hour
    t = pelletRun(p, t, (uint32_t)(5.0 / rate * 3600.0), 0, 6.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 5, (float)p.remainingKg());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, (float)(p.remainingKg() / rate), (float)p.hoursAt(6.2f));
    TEST_ASSERT_TRUE(p.hoursAt(0) < 0);                             // not burning
    // topping up early (by hand, not low) adds to the cycle
    p.addKg(5, t);
    TEST_ASSERT_EQUAL(t, p.lastRefillT);
    TEST_ASSERT_EQUAL_FLOAT(5, p.lastRefillKg);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 10, (float)p.remainingKg());
    // gaps without readings count as at most 30 s
    double u = p.units;
    p.update(t + 3600, 0, 6.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.2f * 30, (float)(p.units - u));
    p.resetCalibration();
    TEST_ASSERT_EQUAL(0, p.nCycles);
    TEST_ASSERT_EQUAL_FLOAT(-1, (float)p.remainingKg());
}

static void test_hour_acc()
{
    pc::HourAcc a;
    TEST_ASSERT_TRUE(a.empty());
    const uint32_t h = 1763380800;                                           // a whole hour
    a.add(hourIn(h + 60,   200, 4, 3, 62));
    a.add(hourIn(h + 180,  210, 4, 2, -1));                                  // auger unknown
    a.add(hourIn(h + 300,  215, 0, -1, 0));                                  // off, flame actual unknown
    a.add(hourIn(h + 3599, 205, 4, 3, 62));
    TEST_ASSERT_EQUAL_UINT32(h, a.hour);
    pc::HourRecord r = a.result();
    TEST_ASSERT_EQUAL(4, r.n);
    TEST_ASSERT_EQUAL(208, r.room10);                                        // (200+210+215+205)/4 = 207.5 -> 208
    TEST_ASSERT_EQUAL(75, r.burnPct);                                        // 3 of 4 in state 4
    TEST_ASSERT_EQUAL(4, r.state);                                           // most frequent
    TEST_ASSERT_EQUAL(27, r.flameAct10);                                     // (3+2+3)/3 = 2.67 -> 27
    TEST_ASSERT_EQUAL(30, r.flame10);
    TEST_ASSERT_EQUAL(41, r.auger10);                                        // (62+0+62)/3 = 41.3
    TEST_ASSERT_EQUAL(1500, r.fumeRpm);
    TEST_ASSERT_EQUAL(100, r.fumeMax);
    a.reset(); TEST_ASSERT_TRUE(a.empty());
    a.add(hourIn(h + 7200 + 5, -15, 0, -1, -1));                             // negative temperature, all unknown
    r = a.result();
    TEST_ASSERT_EQUAL_UINT32(h + 7200, r.t);
    TEST_ASSERT_EQUAL(-15, r.room10);
    TEST_ASSERT_EQUAL(0xFF, r.flameAct10); TEST_ASSERT_EQUAL(-1, r.auger10); TEST_ASSERT_EQUAL(0, r.burnPct);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_timer_from_str);
    RUN_TEST(test_timer_from_str_invalid);
    RUN_TEST(test_timer_to_str);
    RUN_TEST(test_timer_round_trip);
    RUN_TEST(test_bcd);
    RUN_TEST(test_extrapolate_time);
    RUN_TEST(test_valid_stove_clock);
    RUN_TEST(test_fume_fan_rpm);
    RUN_TEST(test_room_fan);
    RUN_TEST(test_pellets_low);
    RUN_TEST(test_error_confirm_prewarning_ignored);
    RUN_TEST(test_error_confirm_after_delay);
    RUN_TEST(test_error_confirm_startup_check);
    RUN_TEST(test_error_confirm_after_transition);
    RUN_TEST(test_error_confirm_alarm_state_immediate);
    RUN_TEST(test_combustion_quality);
    RUN_TEST(test_pellet_quality);
    RUN_TEST(test_address_list);
    RUN_TEST(test_parse_on_off);
    RUN_TEST(test_parse_int_range);
    RUN_TEST(test_parse_fan_speed);
    RUN_TEST(test_hostname);
    RUN_TEST(test_ipv4);
    RUN_TEST(test_check_static_ip);
    RUN_TEST(test_wifi_credentials);
    RUN_TEST(test_rssi_quality);
    RUN_TEST(test_parse_version);
    RUN_TEST(test_update_available);
    RUN_TEST(test_stove_state);
    RUN_TEST(test_error_table);
    RUN_TEST(test_firmware_asset);
    RUN_TEST(test_text_ring);
    RUN_TEST(test_ring);
    RUN_TEST(test_hour_acc);
    RUN_TEST(test_pellet_tracker);
    RUN_TEST(test_pellet_units_capped);
    return UNITY_END();
}
