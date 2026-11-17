// Unit tests for lib/palletcore (pure logic of the firmware).
// Run on the development machine: pio test -e native
#include <unity.h>
#include <palletcore.h>

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
    TEST_ASSERT_TRUE(pc::validHostname("palletheater"));
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
    return UNITY_END();
}
