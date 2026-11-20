#pragma once
// Telnet chart (telnetMenu.h): the history as a line chart in the terminal.
//
// Drawn with Unicode braille characters: every character is a cell of 2 x 4 dots, so a
// chart of 72 x 8 characters has 144 x 32 dots (v: 20 rows, 80 dots high). ANSI colours tell the series apart (a
// cell has one colour: where two lines cross, the first series of the chart wins). Below
// the chart: the time axis, a strip with the stove state and the pellet / error events.
//
//   3 h - 24 h   the 2-minute samples of the 24 h ring buffer (history.h)
//   7 days       the hourly records (history.h, week[])
//
// One measure per chart, one scale (no second y-axis): temperatures, fumes, auger, flame
// and room fan level, fume fan. Keys: 1 - 5 chart, + / - zoom, left / right move in time,
// n back to now, v taller. Needs a UTF-8 terminal (macOS Terminal, PuTTY, Windows Terminal).
//
// Memory: the dot grid and the column averages are allocated while drawing (about 4 KB,
// 6 KB in the tall view) and freed right after.

#ifndef A_MAG
#define A_MAG "\x1B[35m"
#endif
#ifndef A_BLU
#define A_BLU "\x1B[34m"
#endif

static const int      CH_W = 72;                                        // characters
static const uint32_t CH_WIN[] = { 3 * 3600, 6 * 3600, 12 * 3600, 24 * 3600, 7 * 24 * 3600 };
static const char*    CH_WIN_NAME[] = { "3 h", "6 h", "12 h", "24 h", "7 days" };
static const int      CH_NWIN = 5;
static uint8_t  chWin = 0, chSet = 0;
static bool     chTall = false;
static uint32_t chBack = 0;   // seconds the right edge lies before now (0 = now)

struct ChSeries { const char* name; const char* color; };
struct ChSet {
    const char* title;
    const char* unit;
    uint8_t     n;
    ChSeries    s[3];
    float       fixLo, fixHi;   // fixed scale when fixHi > fixLo, otherwise from the data
    float       pad;            // margin above and below the data (data scale)
    uint8_t     dec;            // decimals of the axis labels
};
static const ChSet CH_SETS[] = {
    { "Temperatures", "C",   3, { { "room", A_CYN }, { "set point", A_YEL }, { "stove sensor", A_MAG } }, 0, 0, 1, 1 },
    { "Fumes",        "C",   1, { { "fume temperature", A_RED } },                                     0, 0, 5, 0 },
    { "Auger",        "%",   1, { { "auger (time it runs)", A_GRN } },                    0, 100, 0, 0 },
    { "Flame and room fan", "", 2, { { "flame power (actual)", A_RED }, { "room fan (actual)", A_BLU } }, 0, 5, 0, 0 },
    { "Fume fan",     "rpm", 1, { { "fume fan", A_MAG } },                                             0, 0, 50, 0 },
};
static const int CH_NSETS = sizeof(CH_SETS) / sizeof(CH_SETS[0]);

// Value of series si of chart cs; NAN = not known
static float chFine(const HistSample& h, int cs, int si)
{
    switch (cs * 4 + si) {
    case 0:  return h.room10 / 10.0f;
    case 1:  return h.setpoint;
    case 2:  return h.stove10 / 10.0f;
    case 4:  return h.fume;
    case 8:  return h.auger10 >= 0 ? (float)pc::augerPercent(h.auger10 / 10.0f) : NAN;
    case 12: return h.flameActual >= 0 ? (float)h.flameActual : NAN;
    case 13: return h.fanActual >= 0 ? (float)h.fanActual : NAN;
    case 16: return h.fumeRpm != 0xFFFF ? (float)h.fumeRpm : NAN;
    }
    return NAN;
}
static float chHour(const pc::HourRecord& r, int cs, int si)
{
    switch (cs * 4 + si) {
    case 0:  return r.room10 / 10.0f;
    case 1:  return r.setpoint;
    case 2:  return r.stove10 / 10.0f;
    case 4:  return r.fume;
    case 8:  return r.auger10 >= 0 ? (float)pc::augerPercent(r.auger10 / 10.0f) : NAN;   // hourly average
    case 12: return r.flameAct10 != 0xFF ? r.flameAct10 / 10.0f : NAN;
    case 13: return r.fanAct10 != 0xFF ? r.fanAct10 / 10.0f : NAN;
    case 16: return r.fumeRpm != 0xFFFF ? (float)r.fumeRpm : NAN;
    }
    return NAN;
}
// Which state wins when a character column covers several: alarm, burning, start-up,
// cleaning / shut-down, stand-by, off
static int chStatePrio(int s)
{
    if (s >= 8) return 6;
    if (s == 4) return 5;
    if (s >= 1 && s <= 3) return 4;
    if (s == 5 || s == 6) return 3;
    if (s == 7) return 2;
    if (s == 0) return 1;
    return 0;
}
// One braille character (U+2800 + dots) in UTF-8
static void chBraille(uint8_t dots)
{
    uint16_t cp = 0x2800 + dots;
    char b[3] = { (char)(0xE0 | (cp >> 12)), (char)(0x80 | ((cp >> 6) & 0x3F)), (char)(0x80 | (cp & 0x3F)) };
    tnWrite(b, 3);
}

static void chRender()
{
    const ChSet& cs = CH_SETS[chSet];
    time_t now = time(nullptr);
    if (now <= 1700000000) { tpl("   No clock yet (needs NTP time)."); return; }
    const bool hourly = chWin == CH_NWIN - 1;
    const int  rows = chTall ? 20 : 8, W = CH_W, PW = W * 2, PH = rows * 4, n = cs.n;
    const uint32_t win = CH_WIN[chWin], t1 = (uint32_t)now - chBack, t0 = t1 - win;
    const int count = hourly ? weekCount : histCount;

    // one allocation: column sums and counts per series, dots and colour per cell, state
    size_t need = sizeof(float) * PW * n + PW * n + rows * W * 2 + W;
    uint8_t* mem = (uint8_t*)calloc(1, need);
    if (!mem) { tpl(A_RED "   Not enough memory for the chart right now." A_RST); return; }
    float*   sum   = (float*)mem;
    uint8_t* cnt   = mem + sizeof(float) * PW * n;
    uint8_t* dots  = cnt + PW * n;
    uint8_t* color = dots + rows * W;
    int8_t*  st    = (int8_t*)(color + rows * W);
    for (int c = 0; c < W; c++) st[c] = -2;   // no data

    // averages per dot column
    for (int i = 0; i < count; i++) {
        uint32_t t; int state;
        float v[3];
        if (hourly) {
            const pc::HourRecord& r = week[(weekHead - weekCount + i + WEEK_SIZE) % WEEK_SIZE];
            t = r.t + 1800; state = r.state;
            for (int s = 0; s < n; s++) v[s] = chHour(r, chSet, s);
        } else {
            const HistSample& h = hist[(histHead - histCount + i + HIST_SIZE) % HIST_SIZE];
            t = h.t; state = h.state;
            for (int s = 0; s < n; s++) v[s] = chFine(h, chSet, s);
        }
        if (t < t0 || t >= t1) continue;
        int x = (int)((uint64_t)(t - t0) * PW / win);
        for (int s = 0; s < n; s++) {
            if (isnan(v[s]) || cnt[s * PW + x] == 255) continue;
            sum[s * PW + x] += v[s]; cnt[s * PW + x]++;
        }
        int c = x / 2;
        if (st[c] == -2 || chStatePrio(state) > chStatePrio(st[c])) st[c] = state;
    }
    // scale
    float lo = INFINITY, hi = -INFINITY, last[3], mn[3], mx[3], avg[3];
    int got[3] = { 0, 0, 0 };
    for (int s = 0; s < n; s++) {
        mn[s] = INFINITY; mx[s] = -INFINITY; avg[s] = 0; last[s] = NAN;
        for (int x = 0; x < PW; x++) {
            if (!cnt[s * PW + x]) continue;
            float v = sum[s * PW + x] / cnt[s * PW + x];
            sum[s * PW + x] = v;   // from here on: the average
            if (v < mn[s]) mn[s] = v;
            if (v > mx[s]) mx[s] = v;
            avg[s] += v; got[s]++; last[s] = v;
        }
        if (got[s]) { avg[s] /= got[s]; if (mn[s] < lo) lo = mn[s]; if (mx[s] > hi) hi = mx[s]; }
    }
    bool any = lo <= hi;
    if (cs.fixHi > cs.fixLo) { lo = cs.fixLo; hi = cs.fixHi; }
    else if (any) {
        lo = floorf(lo - cs.pad); hi = ceilf(hi + cs.pad);
        if (cs.pad >= 5) { lo = floorf(lo / cs.pad) * cs.pad; hi = ceilf(hi / cs.pad) * cs.pad; }
        if (hi - lo < 2) hi = lo + 2;
    } else { lo = 0; hi = 1; }

    // plot: series in reverse order, so the first one wins where lines cross
    const int gapPx = max(2, (int)((uint64_t)(hourly ? 3 * 3600 : 3 * HIST_INTERVAL_S) * PW / win));
    auto plot = [&](int x, int y, int s) {
        if (x < 0 || x >= PW || y < 0 || y >= PH) return;
        static const uint8_t BIT[2][4] = { { 0x01, 0x02, 0x04, 0x40 }, { 0x08, 0x10, 0x20, 0x80 } };
        int cell = (y / 4) * W + x / 2;
        dots[cell] |= BIT[x & 1][y & 3];
        color[cell] = s + 1;
    };
    for (int s = n - 1; s >= 0; s--) {
        int px = -1, py = 0;
        for (int x = 0; x < PW; x++) {
            if (!cnt[s * PW + x]) continue;
            int y = PH - 1 - (int)lroundf((sum[s * PW + x] - lo) / (hi - lo) * (PH - 1));
            if (y < 0) y = 0;
            if (y >= PH) y = PH - 1;
            if (px >= 0 && x - px <= gapPx) {   // line from the previous point (Bresenham)
                int dx = x - px, dy = abs(y - py), sy = py < y ? 1 : -1, err = dx - dy, cx = px, cy = py;
                while (true) {
                    plot(cx, cy, s);
                    if (cx == x && cy == y) break;
                    int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; cx++; }
                    if (e2 < dx)  { err += dx; cy += sy; }
                }
            } else plot(x, y, s);
            px = x; py = y;
        }
    }

    // window line
    String when = chBack ? fmtTime(t0, hourly ? "%d/%m %H:%M" : "%d/%m %H:%M") + " - " + fmtTime(t1, "%d/%m %H:%M")
                         : "last " + String(CH_WIN_NAME[chWin]) + " (until now)";
    tpf("   " A_BOLD "%d %s" A_RST "  %s%s" A_EOL, chSet + 1, cs.title, when.c_str(), hourly ? A_DIM "  hourly averages" A_RST : "");
    // chart rows with the y-axis
    for (int r = 0; r < rows; r++) {
        bool lbl = r == 0 || r == rows - 1 || r == rows / 2;
        if (lbl) {
            float v = hi - (hi - lo) * (r * 4 + (r == rows - 1 ? 3 : r == 0 ? 0 : 1.5f)) / (PH - 1);
            tpf("%7.*f " A_DIM "\xE2\x94\xA4" A_RST, cs.dec, v);   // ┤
        } else tp("        " A_DIM "\xE2\x94\x82" A_RST);              // │
        const char* cur = nullptr;
        for (int c = 0; c < W; c++) {
            uint8_t d = dots[r * W + c];
            if (!d) { tnWrite(" ", 1); continue; }
            const char* want = cs.s[color[r * W + c] - 1].color;
            if (want != cur) { tp(want); cur = want; }
            chBraille(d);
        }
        tp(A_RST A_EOL);
    }
    // time axis
    tp("        " A_DIM "\xE2\x94\x94");   // └
    for (int c = 0; c < W; c++) tp(c % 12 == 0 ? "\xE2\x94\xAC" : "\xE2\x94\x80");   // ┬ ─
    tp(A_RST A_EOL);
    char axis[CH_W + 12];
    memset(axis, ' ', sizeof(axis)); axis[sizeof(axis) - 1] = 0;
    for (int c = 0; c < W; c += 12) {
        time_t t = t0 + (time_t)((uint64_t)c * win / W);
        String l = fmtTime(t, win >= 2 * 24 * 3600 ? "%d/%m" : "%H:%M");
        int at = 9 + c - (int)l.length() / 2;
        if (at < 0) at = 0;
        for (size_t j = 0; j < l.length() && at + j < sizeof(axis) - 1; j++) axis[at + j] = l[j];
    }
    tpf(A_DIM "%s" A_RST A_EOL, axis);
    // stove state strip
    tp("  state " A_DIM "\xE2\x94\x82" A_RST);
    const char* cur = nullptr;
    for (int c = 0; c < W; c++) {
        int s = st[c];
        const char* col; const char* ch;
        if (s == -2)               { col = A_RST; ch = " "; }
        else if (s >= 8)           { col = A_RED; ch = "\xE2\x96\x88"; }   // █ alarm
        else if (s == 4)           { col = A_GRN; ch = "\xE2\x96\x88"; }   // █ burning
        else if (s >= 1 && s <= 3) { col = A_YEL; ch = "\xE2\x96\x93"; }   // ▓ start-up
        else if (s == 5 || s == 6) { col = A_YEL; ch = "\xE2\x96\x92"; }   // ▒ cleaning, shut-down
        else if (s == 7)           { col = A_CYN; ch = "\xE2\x96\x91"; }   // ░ stand-by
        else                       { col = A_DIM; ch = "\xC2\xB7"; }       // · off
        if (col != cur) { tp(A_RST); tp(col); cur = col; }
        tp(ch);
    }
    tp(A_RST A_EOL);
    // events: pellet low, refilled, errors
    char ev[CH_W]; memset(ev, ' ', sizeof(ev));
    for (int i = 0; i < evtCount; i++) {
        const StateEvent& e = evts[(evtHead - evtCount + i + EVT_SIZE) % EVT_SIZE];
        if (e.t < t0 || e.t >= t1) continue;
        int c = (int)((uint64_t)(e.t - t0) * W / win);
        if (e.kind == EVT_PELLET && e.code == palletSensorAddr) {
            if (pc::reserveSensorLow(e.to) && !pc::reserveSensorLow(e.from)) ev[c] = 'L';
            else if (e.to == 0 && pc::reserveSensorLow(e.from)) ev[c] = 'R';
        } else if (e.kind == EVT_ERROR && e.code) ev[c] = 'E';
    }
    tp(" events " A_DIM "\xE2\x94\x82" A_RST);
    for (int c = 0; c < W; c++) {
        if (ev[c] == 'L' || ev[c] == 'E') tpf(A_RED "%c" A_RST, ev[c]);
        else if (ev[c] == 'R') tp(A_GRN "R" A_RST);
        else tnWrite(" ", 1);
    }
    tpl();
    // key of the strip and the events (fits 80 columns)
    tpl(A_DIM "         " A_GRN "\xE2\x96\x88" A_RST A_DIM " burning " A_YEL "\xE2\x96\x93" A_RST A_DIM " start " A_YEL "\xE2\x96\x92" A_RST A_DIM
        " cleaning " A_CYN "\xE2\x96\x91" A_RST A_DIM " stand-by \xC2\xB7 off " A_RED "\xE2\x96\x88" A_RST A_DIM " alarm  "
        A_RED "L" A_RST A_DIM " low " A_GRN "R" A_RST A_DIM " refilled " A_RED "E" A_RST A_DIM " error" A_RST);
    // legend with the values in the window
    for (int s = 0; s < n; s++) {
        tpf("   %s\xE2\x94\x81\xE2\x94\x81" A_RST " %-22.22s", cs.s[s].color, cs.s[s].name);   // ━━
        if (got[s]) tpf("now %.*f  min %.*f  max %.*f  avg %.*f %s" A_EOL, cs.dec, last[s], cs.dec, mn[s], cs.dec, mx[s], cs.dec, avg[s], cs.unit);
        else tpl(A_DIM "no data in this period" A_RST);
    }
    free(mem);
}

// Own keys of the chart view; false = not handled (back)
static bool chKey(int k)
{
    const uint32_t win = CH_WIN[chWin];
    if (k >= '1' && k < '1' + CH_NSETS) { chSet = k - '1'; return true; }
    if (k == '+' || k == '=') { if (chWin > 0) chWin--; return true; }
    if (k == '-' || k == '_') { if (chWin < CH_NWIN - 1) chWin++; if (chWin == CH_NWIN - 1) chBack = 0; return true; }
    if (k == 'v' || k == 'V') { chTall = !chTall; return true; }
    if (k == 'n' || k == 'N' || k == TK_END) { chBack = 0; return true; }
    if (k == TK_LEFT) {
        // not further back than the oldest data
        bool hourly = chWin == CH_NWIN - 1;
        uint32_t oldest = hourly ? (weekCount ? week[(weekHead - weekCount + WEEK_SIZE) % WEEK_SIZE].t : 0)
                                 : (histCount ? hist[(histHead - histCount + HIST_SIZE) % HIST_SIZE].t : 0);
        uint32_t now = (uint32_t)time(nullptr);
        uint32_t maxBack = oldest && now > oldest + win / 2 ? now - oldest - win / 2 : 0;
        chBack = min(chBack + win / 2, maxBack);
        return true;
    }
    if (k == TK_RIGHT) { chBack = chBack > win / 2 ? chBack - win / 2 : 0; return true; }
    return false;
}
static const TmView TM_CHART = { "Chart", chRender, 10000, false, chKey,
    "1-5 chart  +/- zoom  left/right time  n now  v taller  other key: back" };
