#pragma once
// Stove clock: reading RAM 0x7A - 0x80 and the 5 min re-read schedule.
//
// Split out of main.cpp; included there in the original order (single
// translation unit: the globals live in constants.h).

bool getStoveDateTime()
{
    if (stoveMutex) {
        xSemaphoreTakeRecursive(stoveMutex, portMAX_DELAY);
    }

    int cur_min = stove.read(ram, stoveTimeMinAddr);
    int cur_hour = stove.read(ram, stoveTimeHourAddr);
    int cur_sec = stove.read(ram, stoveTimeSecAddr);
    int cur_day = stove.read(ram, stoveTimeDayAddr);
    int cur_month = stove.read(ram, stoveTimeMonthAddr);
    int cur_year = stove.read(ram, stoveTimeYearAddr);

    if (stoveMutex) {
        xSemaphoreGiveRecursive(stoveMutex);
    }

    // Keep the previous (extrapolated) time when a read failed or returned nonsense
    if (!pc::validStoveClock(cur_sec, cur_min, cur_hour, cur_day, cur_month, cur_year)) return false;

    char stoveTimeBuffer[32];
    struct tm stove_tm = {0};

    snprintf(stoveTimeBuffer, sizeof(stoveTimeBuffer), "20%i-%02d-%02d %02d:%02d:%02d",
             cur_year, cur_month, cur_day, cur_hour, cur_min, cur_sec);

    strptime(stoveTimeBuffer, "%Y-%m-%d %H:%M:%S", &stove_tm);
    // Take the current NTP based DST setting in order to interprete the stove it's Time
    // The stove does not support DST so a time change is required
    stove_tm.tm_isdst = currentDst;
    time_t newStoveTime = mktime(&stove_tm);
    if (newStoveTime != (time_t)-1) {
        stoveTime = newStoveTime;
        stoveTimeReadMs = millis();
        return true;
    }
    return false;
}

// The stove clock is read from the stove every 5 min once a valid time is known
// (in between stoveTimeNow() extrapolates it), and every 10 s until then.
static void stoveTimeTick()
{
    static unsigned long lastAttempt = 0;
    static bool known = false;
    unsigned long interval = known ? STOVE_TIME_READ_MS : STOVE_TIME_RETRY_MS;
    if (lastAttempt != 0 && millis() - lastAttempt < interval) return;
    lastAttempt = millis();
    if (lastAttempt == 0) lastAttempt = 1;
    known = getStoveDateTime();
}
