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
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(3, 0));    // 3 - 1: a refill is being confirmed (23/11/2026)
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(1, 0));
    TEST_ASSERT_TRUE(pc::reserveRefilling(3));
    TEST_ASSERT_FALSE(pc::reserveRefilling(4));        // one 4 can be pellets sliding past the sensor
    TEST_ASSERT_FALSE(pc::reserveRefilling(0));
    TEST_ASSERT_TRUE(pc::reserveSensorLow(2));         // not confirmed by the stove yet
    TEST_ASSERT_EQUAL_INT(0, pc::pelletsLow(6, 0));    // never seen: not low
}
static void test_reserve_step_meter()
{
    pc::ReserveStepMeter m;
    TEST_ASSERT_EQUAL_INT(60, m.stepS);
    // flickers 5 -> 4 -> 5 are not steps
    m.update(5, 1000); m.update(4, 1005); m.update(5, 1011);
    TEST_ASSERT_EQUAL_INT(0, m.measured);
    // the refill of 24/11/2026: 4 at 21:10:27, then 58, 63, 58, 64 s
    m.update(4, 2000); m.update(3, 2058);
    TEST_ASSERT_EQUAL_INT(58, m.stepS);
    m.update(2, 2121); m.update(1, 2179); m.update(0, 2243);
    TEST_ASSERT_EQUAL_INT(4, m.measured);
    TEST_ASSERT_INT_WITHIN(3, 61, m.stepS);
    // a jump 5 -> 3 within seconds (after a switch-off) is not a step
    m.update(5, 9000); m.update(3, 9004);
    TEST_ASSERT_EQUAL_INT(4, m.measured);
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
static void test_error_confirm_waits_for_countdown()
{
    // E1 (32) while refilling: the stove counts down 51 s, the lid is closed after 36 s
    pc::ErrorConfirm ec;
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 4, 1000, 30000, 180000, 51));
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 4, 31000, 30000, 180000, 21));   // 30 s: still counting down
    TEST_ASSERT_EQUAL_INT(0,  ec.update(0, 4, 37000, 30000, 180000, -1));
    // the countdown ran out and the error stays: counts 5 s after its end
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 4, 100000, 30000, 180000, 51));
    TEST_ASSERT_EQUAL_INT(0,  ec.update(32, 4, 155999, 30000, 180000, 0));
    TEST_ASSERT_EQUAL_INT(32, ec.update(32, 4, 156000, 30000, 180000, 0));
    // the stove goes into alarm: at once
    pc::ErrorConfirm e2;
    TEST_ASSERT_EQUAL_INT(32, e2.update(32, 8, 1000, 30000, 180000, 40));
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
    // on: starting .. brazier cleaning, standby; alarms count as off (nothing is written then)
    const bool on[] = { false, true, true, true, true, true, false, true, false, false, false };
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
// Every asset of a release (.github/workflows/firmware.yml)
static const char* const RELEASE_ASSETS[] = {
    "openpelletbridge-firmware.bin", "openpelletbridge-factory.bin", "openpelletbridge-firmware.elf",
    "openpelletbridge-opb-s3-firmware.bin", "openpelletbridge-opb-s3-factory.bin", "openpelletbridge-opb-s3-firmware.elf",
    "SHA256SUMS"};
// firmwareAssetRank as released up to v1.4.7 (git show v1.4.7:lib/pelletcore/src/pelletcore.h):
// the firmware in the field picks its asset with this
static int releasedAssetRank(const char* name)
{
    if (!name) return 0;
    if (strcmp(name, "openpelletbridge-firmware.bin") == 0) return 2;
    if (strcmp(name, "palletheater-firmware.bin") == 0) return 1;
    return 0;
}
static const char* pickAsset(const char* board)
{
    const char* best = nullptr; int r = 0;
    for (const char* n : RELEASE_ASSETS) { int x = pc::firmwareAssetRank(n, board); if (x > r) { r = x; best = n; } }
    return best;
}
static void test_firmware_asset()
{
    const char* dk = "esp32-devkit";
    TEST_ASSERT_EQUAL_INT(2, pc::firmwareAssetRank("openpelletbridge-firmware.bin", dk));
    TEST_ASSERT_EQUAL_INT(1, pc::firmwareAssetRank("palletheater-firmware.bin", dk));   // controllers up to v1.0.0
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank("openpelletbridge-factory.bin", dk)); // never the full flash image
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank("SHA256SUMS", dk));
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank("", dk));
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank(nullptr, dk));
    // the old name must stay a fallback: it may not win over the current one
    TEST_ASSERT_TRUE(pc::firmwareAssetRank(PC_FW_ASSET, dk) > pc::firmwareAssetRank(PC_FW_ASSET_OLD, dk));
    // an S3 asset is never chosen by the DevKit, a DevKit asset never by the S3
    for (const char* n : RELEASE_ASSETS) {
        if (strstr(n, "opb-s3")) TEST_ASSERT_EQUAL_INT_MESSAGE(0, pc::firmwareAssetRank(n, dk), n);
        else TEST_ASSERT_EQUAL_INT_MESSAGE(0, pc::firmwareAssetRank(n, "opb-s3"), n);
    }
    TEST_ASSERT_EQUAL_INT(0, pc::firmwareAssetRank(PC_FW_ASSET_OLD, "opb-s3"));
    TEST_ASSERT_EQUAL_INT(2, pc::firmwareAssetRank("openpelletbridge-opb-s3-firmware.bin", "opb-s3"));
    TEST_ASSERT_EQUAL_STRING("openpelletbridge-firmware.bin", pickAsset(dk));
    TEST_ASSERT_EQUAL_STRING("openpelletbridge-opb-s3-firmware.bin", pickAsset("opb-s3"));
    // unknown board: nothing
    TEST_ASSERT_NULL(pickAsset("esp32-c3"));
    TEST_ASSERT_NULL(pickAsset(nullptr));
    TEST_ASSERT_NULL(pc::firmwareAssetName("x"));
    TEST_ASSERT_EQUAL_STRING(PC_FW_ASSET_S3, pc::firmwareAssetName("opb-s3"));
    // released firmware: still finds the DevKit asset, never an S3 one; and the DevKit picks the same
    for (const char* n : RELEASE_ASSETS) {
        if (strstr(n, "opb-s3")) TEST_ASSERT_EQUAL_INT_MESSAGE(0, releasedAssetRank(n), n);
        TEST_ASSERT_EQUAL_INT_MESSAGE(releasedAssetRank(n), pc::firmwareAssetRank(n, dk), n);
    }
    TEST_ASSERT_EQUAL_INT(2, releasedAssetRank(PC_FW_ASSET));
}

// Start of an app image: header (24) + segment header (8) + app descriptor magic
static void makeImage(uint8_t* h, uint16_t chip)
{
    memset(h, 0, pc::IMG_HEAD);
    h[0] = 0xE9; h[12] = (uint8_t)chip; h[13] = (uint8_t)(chip >> 8);
    h[32] = 0x32; h[33] = 0x54; h[34] = 0xCD; h[35] = 0xAB;
}
static void test_firmware_image_check()
{
    uint8_t h[pc::IMG_HEAD]; char e[160];
    makeImage(h, pc::CHIP_ESP32);
    TEST_ASSERT_TRUE(pc::firmwareImageCheck(h, sizeof(h), pc::CHIP_ESP32, e, sizeof(e)));
    TEST_ASSERT_FALSE(pc::firmwareImageCheck(h, sizeof(h), pc::CHIP_ESP32S3, e, sizeof(e)));
    TEST_ASSERT_NOT_NULL(strstr(e, "wrong chip"));
    TEST_ASSERT_NOT_NULL(strstr(e, "image for ESP32,"));
    TEST_ASSERT_NOT_NULL(strstr(e, PC_FW_ASSET_S3));          // tells which asset to use
    makeImage(h, pc::CHIP_ESP32S3);
    TEST_ASSERT_TRUE(pc::firmwareImageCheck(h, sizeof(h), pc::CHIP_ESP32S3, e, sizeof(e)));
    TEST_ASSERT_FALSE(pc::firmwareImageCheck(h, sizeof(h), pc::CHIP_ESP32, e, sizeof(e)));
    TEST_ASSERT_NOT_NULL(strstr(e, "image for ESP32-S3,"));
    TEST_ASSERT_NOT_NULL(strstr(e, "this controller is ESP32 - use " PC_FW_ASSET));
    makeImage(h, 0x0005);                                      // ESP32-C3
    TEST_ASSERT_FALSE(pc::firmwareImageCheck(h, sizeof(h), pc::CHIP_ESP32, e, sizeof(e)));
    // ESP32 factory image: 0xFF up to the bootloader at 0x1000
    memset(h, 0xFF, sizeof(h));
    TEST_ASSERT_FALSE(pc::firmwareImageCheck(h, sizeof(h), pc::CHIP_ESP32, e, sizeof(e)));
    TEST_ASSERT_NOT_NULL(strstr(e, "not an app image"));
    // S3 factory image: starts with the bootloader (right chip, but no app descriptor)
    makeImage(h, pc::CHIP_ESP32S3); memset(h + 32, 0xFF, 4);
    TEST_ASSERT_FALSE(pc::firmwareImageCheck(h, sizeof(h), pc::CHIP_ESP32S3, e, sizeof(e)));
    TEST_ASSERT_NOT_NULL(strstr(e, "not an app image"));
    makeImage(h, pc::CHIP_ESP32);
    TEST_ASSERT_FALSE(pc::firmwareImageCheck(h, 20, pc::CHIP_ESP32, e, sizeof(e)));   // too short
    TEST_ASSERT_FALSE(pc::firmwareImageCheck(nullptr, 0, pc::CHIP_ESP32, e, sizeof(e)));
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

// One full cycle: refill (sensor clear), burn `secs` at `auger`, then the alarm
static uint32_t pelletCycle(pc::PelletTracker& p, uint32_t t, uint32_t secs, float auger)
{
    t = pelletRun(p, t, 60, 0, auger);    // refill confirmed
    t = pelletRun(p, t, secs, 0, auger);
    return pelletRun(p, t, 70, 5, auger); // alarm confirmed
}
// Each alarm corrects the estimate: the prediction error is kept per cycle and recent
// cycles weigh more, so a changed burn rate shows up quickly
static void test_pellet_self_correcting()
{
    pc::PelletTracker p;
    uint32_t t = 1800000000;
    t = pelletRun(p, t, 70, 5, 6.2f);                 // first alarm: start
    t = pelletCycle(p, t, 36000, 6.2f);               // 15 kg in 10 h
    TEST_ASSERT_EQUAL(1, p.nCycles);
    TEST_ASSERT_TRUE(std::isnan(p.predKg[0]));        // no estimate before the first cycle
    t = pelletCycle(p, t, 36000, 6.2f);               // the same again: predicted about right
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0, p.predKg[1]);
    double k2 = p.kgPerUnit();
    // other pellets: 15 kg last only 8 h -> the alarm comes early, ~3 kg still predicted
    t = pelletCycle(p, t, 28800, 6.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 3.0f, p.predKg[2]);
    TEST_ASSERT_TRUE(p.kgPerUnit() > k2);
    // weighted: closer to the new cycle than the plain average of the three
    double plain = 45.0 / (p.cycles[0].units + p.cycles[1].units + p.cycles[2].units);
    TEST_ASSERT_TRUE(p.kgPerUnit() > plain);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, (float)p.cycleWeight(2));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, (float)p.cycleWeight(1));
    // the other way round: the alarm comes late -> overdue while burning past the estimate
    t = pelletRun(p, t, 60, 0, 6.2f);                 // refill
    TEST_ASSERT_FALSE(p.overdue());
    t = pelletRun(p, t, 40000, 0, 6.2f);              // longer than any cycle so far
    TEST_ASSERT_TRUE(p.overdue());
    TEST_ASSERT_EQUAL_FLOAT(0, (float)p.remainingKg());
    t = pelletRun(p, t, 70, 5, 6.2f);
    TEST_ASSERT_FALSE(p.overdue());                   // low now
    TEST_ASSERT_TRUE(p.predKg[3] < -1.0f);            // came late
    // the oldest cycle drops out with its prediction
    for (int i = 0; i < 7; i++) t = pelletCycle(p, t, 36000, 6.2f);
    TEST_ASSERT_EQUAL(10, p.nCycles);
    TEST_ASSERT_FALSE(std::isnan(p.predKg[0]));       // the first cycle (NAN) is gone
}

static void test_pellet_usage()
{
    pc::PelletUsage u;
    TEST_ASSERT_FALSE(u.roll(20261121, 1000));        // first call: starts the day
    TEST_ASSERT_EQUAL_FLOAT(250, (float)u.unitsToday(1250));
    TEST_ASSERT_FALSE(u.roll(20261121, 1500));        // same day
    TEST_ASSERT_TRUE(u.roll(20261122, 1600));         // day closed with all its units
    TEST_ASSERT_EQUAL(1, u.n);
    TEST_ASSERT_EQUAL_UINT32(20261121, u.closed(0).day);
    TEST_ASSERT_EQUAL_FLOAT(600, u.closed(0).units);
    TEST_ASSERT_EQUAL_FLOAT(0, (float)u.unitsToday(1600));
    // off for two days: everything counted until then belongs to the last day it ran
    TEST_ASSERT_TRUE(u.roll(20261125, 2000));
    TEST_ASSERT_EQUAL_UINT32(20261122, u.closed(0).day);
    TEST_ASSERT_EQUAL_FLOAT(400, u.closed(0).units);
    int got = 0;
    TEST_ASSERT_EQUAL_FLOAT(1000, (float)u.unitsClosed(7, &got));
    TEST_ASSERT_EQUAL(2, got);
    // the ring keeps the last MAX_DAYS days
    for (int i = 0; i < pc::PelletUsage::MAX_DAYS + 5; i++) u.roll(30000000 + i, 3000.0 + i * 10);
    TEST_ASSERT_EQUAL(pc::PelletUsage::MAX_DAYS, u.n);
    TEST_ASSERT_EQUAL_FLOAT(10, u.closed(0).units);
    char t[11]; pc::dayText(20261123, t);
    TEST_ASSERT_EQUAL_STRING("2026-11-23", t);
}
static void test_pellet_cost()
{
    pc::PelletCost c;
    TEST_ASSERT_EQUAL_FLOAT(0, (float)c.pricePerKg());      // no price set
    c.pricePerBag = 6.0f; c.bagKg = 15;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.4f, (float)c.pricePerKg());
    c.take(15);                                             // stock not kept: nothing happens
    TEST_ASSERT_EQUAL_FLOAT(-1, c.stockBags);
    c.stockBags = 10; c.take(15);
    TEST_ASSERT_EQUAL_FLOAT(9, c.stockBags);
    c.take(7.5f);
    TEST_ASSERT_EQUAL_FLOAT(8.5f, c.stockBags);
    c.stockBags = 0.5f; c.take(15);
    TEST_ASSERT_EQUAL_FLOAT(0, c.stockBags);                // never below 0
}

static void test_room_source()
{
    TEST_ASSERT_EQUAL(pc::RS_STOVE,  pc::roomSourceOf(20.5f, 20.5f, 21.5f, pc::RS_UNKNOWN));   // remote not heard: own sensor
    TEST_ASSERT_EQUAL(pc::RS_REMOTE, pc::roomSourceOf(16.0f, 20.0f, 16.0f, pc::RS_STOVE));     // 24/11/2026 08:41: remote switched on
    TEST_ASSERT_EQUAL(pc::RS_REMOTE, pc::roomSourceOf(20.0f, 20.0f, 20.0f, pc::RS_REMOTE));    // both equal: keep
    TEST_ASSERT_EQUAL(pc::RS_STOVE,  pc::roomSourceOf(20.0f, 20.0f, 20.0f, pc::RS_STOVE));
    TEST_ASSERT_EQUAL(pc::RS_UNKNOWN, pc::roomSourceOf(19.0f, 20.0f, 21.0f, pc::RS_UNKNOWN));  // neither: keep
    TEST_ASSERT_EQUAL(pc::RS_STOVE,  pc::roomSourceOf(20.5f, 21.0f, 16.0f, pc::RS_UNKNOWN));   // 27/11/2026: a step behind the stove sensor
    TEST_ASSERT_EQUAL(pc::RS_REMOTE, pc::roomSourceOf(20.5f, 20.0f, 21.0f, pc::RS_REMOTE));    // close to both: keep
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

// ── Buzzer ──────────────────────────────────────────────────────────────────
static void test_beep_presets()
{
    int n; const pc::BeepPreset* p = pc::beepPresets(n);
    TEST_ASSERT_TRUE(n >= 10);
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL(i, pc::beepPresetIndex(p[i].name));   // unique names
        TEST_ASSERT_TRUE(p[i].count > 0);
        for (int s = 0; s < p[i].count; s++) TEST_ASSERT_TRUE(p[i].steps[s].hz == 0 || (p[i].steps[s].hz >= 1000 && p[i].steps[s].hz <= 6000));
    }
    TEST_ASSERT_EQUAL(-1, pc::beepPresetIndex("nope"));
    TEST_ASSERT_EQUAL(-1, pc::beepPresetIndex(nullptr));
}
static const pc::BeepPreset& preset(const char* name) { int n; return pc::beepPresets(n)[pc::beepPresetIndex(name)]; }
static void test_beep_player_steps()
{
    pc::BeepPlayer b;
    TEST_ASSERT_EQUAL(0, b.tick(0));
    TEST_ASSERT_TRUE(b.play(&preset("rising"), 1, 1000));   // 2700 120/30, 4000 200/0
    TEST_ASSERT_EQUAL(2700, b.tick(1000));
    TEST_ASSERT_EQUAL(2700, b.tick(1119));
    TEST_ASSERT_EQUAL(0,    b.tick(1120));
    TEST_ASSERT_EQUAL(4000, b.tick(1150));
    TEST_ASSERT_EQUAL(4000, b.tick(1349));
    TEST_ASSERT_EQUAL(0,    b.tick(1350));   // played once: done
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_TRUE(b.play(&preset("rising"), 1, 2000));
    TEST_ASSERT_EQUAL(0, b.tick(2500));      // a late tick skips the whole pattern
    TEST_ASSERT_FALSE(b.active());
}
static void test_beep_player_repeat_and_priority()
{
    pc::BeepPlayer b;
    TEST_ASSERT_TRUE(b.play(&preset("alarm"), 5, 0));     // until stopped: 1.12 s per pass
    for (uint32_t t = 0; t < 1120u * 89; t += 10) b.tick(t);
    TEST_ASSERT_EQUAL(4000, b.tick(1120u * 89 + 10));     // 89 passes later, in phase
    TEST_ASSERT_EQUAL(0,    b.tick(1120u * 89 + 130));
    TEST_ASSERT_TRUE(b.active());
    TEST_ASSERT_FALSE(b.play(&preset("click"), 1, 100010));   // a click does not interrupt an alarm
    TEST_ASSERT_TRUE(b.play(&preset("beep"), 5, 100020));     // the same priority replaces it
    TEST_ASSERT_EQUAL(4000, b.tick(100139));
    TEST_ASSERT_EQUAL(0, b.tick(100140));
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_TRUE(b.play(&preset("click"), 1, 0, 3));  // 3 passes of 25 ms
    TEST_ASSERT_EQUAL(4000, b.tick(0));
    TEST_ASSERT_EQUAL(4000, b.tick(26));                  // second pass (no off time)
    TEST_ASSERT_EQUAL(0, b.tick(75));
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_TRUE(b.play(&preset("siren"), 1, 0xFFFFFF00u));   // across the millis() wrap
    TEST_ASSERT_EQUAL(4500, b.tick(0x00000000u + 100));
    b.stop();
    TEST_ASSERT_EQUAL(0, b.tick(200));
}
static void test_beep_duty()
{
    TEST_ASSERT_EQUAL_UINT32(0, pc::beepDuty(0, 10));
    TEST_ASSERT_EQUAL_UINT32(512, pc::beepDuty(100, 10));   // 50 %
    TEST_ASSERT_EQUAL_UINT32(512, pc::beepDuty(150, 10));
    TEST_ASSERT_EQUAL_UINT32(128, pc::beepDuty(50, 10));
    TEST_ASSERT_EQUAL_UINT32(1, pc::beepDuty(1, 10));       // never 0 when on
}
static void test_quiet_hours()
{
    TEST_ASSERT_TRUE(pc::inQuietHours(23 * 60, 22 * 60, 7 * 60));    // across midnight
    TEST_ASSERT_TRUE(pc::inQuietHours(3 * 60, 22 * 60, 7 * 60));
    TEST_ASSERT_FALSE(pc::inQuietHours(7 * 60, 22 * 60, 7 * 60));    // to is not included
    TEST_ASSERT_FALSE(pc::inQuietHours(12 * 60, 22 * 60, 7 * 60));
    TEST_ASSERT_TRUE(pc::inQuietHours(13 * 60, 12 * 60, 14 * 60));   // same day
    TEST_ASSERT_FALSE(pc::inQuietHours(14 * 60, 12 * 60, 14 * 60));
    TEST_ASSERT_FALSE(pc::inQuietHours(600, 600, 600));               // from == to: off
    TEST_ASSERT_EQUAL(22 * 60, pc::clockMinutes("22:00"));
    TEST_ASSERT_EQUAL(7 * 60 + 5, pc::clockMinutes("7:05"));
    TEST_ASSERT_EQUAL(-1, pc::clockMinutes("24:00"));
    TEST_ASSERT_EQUAL(-1, pc::clockMinutes("12:00x"));
    TEST_ASSERT_EQUAL(-1, pc::clockMinutes(nullptr));
}
static void test_error_countdown()
{
    TEST_ASSERT_EQUAL(54, pc::countdownLeft(54, 0, 5000));
    TEST_ASSERT_EQUAL(51, pc::countdownLeft(54, 3500, 5000));
    TEST_ASSERT_EQUAL(49, pc::countdownLeft(54, 60000, 5000));   // no reading for long: at most 5 s ahead
    TEST_ASSERT_EQUAL(0,  pc::countdownLeft(2, 4000, 5000));
    TEST_ASSERT_EQUAL(-1, pc::countdownLeft(-1, 0, 5000));
    TEST_ASSERT_EQUAL_UINT32(1000, pc::countdownBeepMs(54));
    TEST_ASSERT_EQUAL_UINT32(500,  pc::countdownBeepMs(10));
    TEST_ASSERT_EQUAL_UINT32(250,  pc::countdownBeepMs(5));
    TEST_ASSERT_EQUAL_UINT32(0,    pc::countdownBeepMs(0));
}

static void test_button_decoder()
{
    pc::ButtonDecoder b; uint8_t n;
    // one short press: reported after the gap
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(true, 1000, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(false, 1100, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(false, 1300, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::PRESSES, b.update(false, 1500, n));
    TEST_ASSERT_EQUAL(1, n);
    // three quick presses
    for (uint32_t t = 2000; t < 2600; t += 200) { b.update(true, t, n); b.update(false, t + 80, n); }
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(false, 2600, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::PRESSES, b.update(false, 2900, n));
    TEST_ASSERT_EQUAL(3, n);
    // 2 s hold: nothing at all
    b.update(true, 5000, n);
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::HELD, b.stage(6500));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(false, 7000, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(false, 9000, n));
    // countdown, released: aborted
    b.update(true, 10000, n);
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::COUNTDOWN, b.stage(14000));
    TEST_ASSERT_EQUAL(4000u, b.resetInMs(14000));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::ABORTED, b.update(false, 14000, n));
    // held to the end: one reset, the release after it does nothing
    b.update(true, 20000, n);
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(true, 27999, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::RESET, b.update(true, 28000, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(true, 29000, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(false, 30000, n));
    TEST_ASSERT_EQUAL(pc::ButtonDecoder::NONE, b.update(false, 31000, n));
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
    RUN_TEST(test_error_confirm_waits_for_countdown);
    RUN_TEST(test_reserve_step_meter);
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
    RUN_TEST(test_firmware_image_check);
    RUN_TEST(test_text_ring);
    RUN_TEST(test_ring);
    RUN_TEST(test_hour_acc);
    RUN_TEST(test_pellet_tracker);
    RUN_TEST(test_pellet_units_capped);
    RUN_TEST(test_pellet_self_correcting);
    RUN_TEST(test_pellet_usage);
    RUN_TEST(test_pellet_cost);
    RUN_TEST(test_room_source);
    RUN_TEST(test_beep_presets);
    RUN_TEST(test_beep_player_steps);
    RUN_TEST(test_beep_player_repeat_and_priority);
    RUN_TEST(test_beep_duty);
    RUN_TEST(test_quiet_hours);
    RUN_TEST(test_error_countdown);
    RUN_TEST(test_button_decoder);
    return UNITY_END();
}
