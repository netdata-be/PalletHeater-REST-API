#pragma once
// Building blocks of the telnet menu (telnetMenu.h): output helpers, the page
// stack and the generic pages every screen is made of.
//
//   menu     a table of items (TmItem): label, current value, action, help line;
//            optionally an info block above the items and an automatic refresh
//   picker   choose a value with the arrow keys, Enter writes it (TmPick)
//   input    a line of text with a small line editor (TmInput)
//   confirm  a yes / no question before an action (TmConfirm)
//   view     text that is drawn once or refreshed (status, network, logs)
//   stream   text that scrolls: live values, the firmware log
//
// Every screen starts with the same status header (tmHeader). Pages are drawn from
// the top of the screen: a new page clears the screen, a redraw of the same page
// (arrow key, refresh) overwrites it in place without flicker.
//
// Writes to the stove only happen in the actions of a picker (Enter), a confirm
// (y) or an input (Enter), or in menu actions that are a toggle the user selected.
// Those run with the stove bus lock held and the source set to "telnet menu (<ip>)",
// so the log shows who changed what (TmBus).

// ── Output ────────────────────────────────────────────────────────────
#define A_RST  "\x1B[0m"
#define A_BOLD "\x1B[1m"
#define A_DIM  "\x1B[2m"
#define A_INV  "\x1B[7m"
#define A_RED  "\x1B[31m"
#define A_GRN  "\x1B[32m"
#define A_YEL  "\x1B[33m"
#define A_CYN  "\x1B[36m"
#define A_EOL  "\x1B[K\r\n"   // end of line: clear the rest of it (redraw in place)

static void tp(const char* s) { tnWriteStr(s); }
static void tp(const String& s) { tnWrite(s.c_str(), s.length()); }
static void tpf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void tpf(const char* fmt, ...)
{
    char b[256];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n > 0) tnWrite(b, (size_t)n < sizeof(b) ? (size_t)n : sizeof(b) - 1);
}
static void tpl(const char* s = "") { tp(s); tp(A_EOL); }
static void tpl(const String& s) { tp(s); tp(A_EOL); }
// Text with "\n" line ends (log buffers) -> "\r\n"
static void tpText(const char* s, size_t n)
{
    size_t from = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '\n') continue;
        tnWrite(s + from, i - from);
        tp("\r\n");
        from = i + 1;
    }
    if (from < n) tnWrite(s + from, n - from);
}
// "   label          value" (an info line of a view)
static void tkv(const char* label, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void tkv(const char* label, const char* fmt, ...)
{
    char b[200];
    va_list ap; va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    tpf("   %-18s %s" A_EOL, label, b);
}

// ── Formatting ────────────────────────────────────────────────────────
static String fmtAgo(uint32_t s)   // a duration: "45 s", "12 min", "3 h 05 min", "2 d 4 h"
{
    char b[24];
    if (s < 120) snprintf(b, sizeof(b), "%lu s", (unsigned long)s);
    else if (s < 3600) snprintf(b, sizeof(b), "%lu min", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(b, sizeof(b), "%lu h %02lu min", (unsigned long)(s / 3600), (unsigned long)(s % 3600 / 60));
    else snprintf(b, sizeof(b), "%lu d %lu h", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600));
    return b;
}
static String fmtTime(time_t t, const char* f = "%d/%m/%Y %H:%M")   // local time, European order
{
    if (t <= 1700000000) return "-";
    char b[24]; struct tm tm; localtime_r(&t, &tm);
    strftime(b, sizeof(b), f, &tm);
    return b;
}
static String fmtTemp(float c) { return String(c, 1) + " C"; }

// ── Pages ─────────────────────────────────────────────────────────────
// What an item does when it is opened: shown with its own sign and colour, so a submenu
// does not look like a line of information (legend under every menu)
enum TmItemKind : uint8_t {
    TI_ACT = 0,   // »  does something right away
    TI_MENU,      // ▸  opens a submenu
    TI_VIEW,      // ≡  opens a screen
    TI_EDIT,      // ↕  change a value (picker or text)
    TI_TOGGLE,    // •  switches on / off
    TI_ASK,       // !  asks for confirmation first
};
struct TmItem {
    const char* label;
    void (*value)(int i, String& out);   // current value, nullptr = none
    void (*open)(int i);                 // Enter (i = index of the item)
    const char* help;                    // one line, shown while the item is selected
    TmItemKind  kind;
};
struct TmMenu {
    const char*   title;
    const TmItem* items;
    uint8_t       n;
    void        (*info)();               // lines above the items, nullptr = none
    uint16_t      refreshMs;             // redraw while shown, 0 = only after a key
};
struct TmView {
    const char* title;
    void      (*render)();               // the text (view), or the next lines (stream)
    uint16_t    refreshMs;               // view: redraw interval; stream: tick interval
    bool        stream;                  // scrolling output, any key stops it
    bool      (*key)(int k);             // own keys (true = handled, the view is redrawn); nullptr = none
    const char* keys;                    // footer for a view with own keys
};
struct TmPick {
    char  title[48];
    int   lo, hi, v;
    bool  wrap;
    void (*fmt)(int v, String& out);
    bool (*apply)(int v);                // the write; sets tmMsg; true = done
    const char* help;
};
struct TmInput {
    char   prompt[64];
    String buf;
    uint8_t max;
    bool   secret;                       // shown as * (passwords)
    void (*done)(const String& s);       // Enter (with the text, trimmed)
    const char* help;
};
struct TmConfirm {
    String question;
    void (*yes)();
};

enum TmKind : uint8_t { PK_MENU, PK_VIEW, PK_PICK, PK_INPUT, PK_CONFIRM };
struct TmPage { TmKind kind; const void* def; uint8_t cursor; };
static TmPage    tmStack[8];
static int       tmDepth = 0;
static bool      tmClear = true;          // next draw clears the screen (new page)
static uint32_t  tmLastDraw = 0;
static String    tmMsg;                   // result of the last action, shown once
static bool      tmMsgErr = false;
static TmPick    tmPick;
static TmInput   tmInput;
static TmConfirm tmConfirm;
static String    tmText;                  // content of a text result page (tmShowText)
static const char* tmTextTitle = "";

static TmPage& tmTop() { return tmStack[tmDepth - 1]; }
static void tmDraw();
static void tmMsgSet(bool ok, const String& s) { tmMsg = s; tmMsgErr = !ok; }

static void tmPush(TmKind kind, const void* def, uint8_t cursor = 0)
{
    if (tmDepth >= (int)(sizeof(tmStack) / sizeof(tmStack[0]))) tmDepth--;   // replace the deepest page
    tmStack[tmDepth++] = { kind, def, cursor };
    tmClear = true;
    tmDraw();
}
static void tmPop()
{
    if (tmDepth > 1) tmDepth--;
    // never back into a finished dialog
    while (tmDepth > 1 && (tmTop().kind == PK_INPUT || tmTop().kind == PK_PICK || tmTop().kind == PK_CONFIRM)) tmDepth--;
    tmText = "";
    tmClear = true;
    tmDraw();
}
static void tmMenu(const TmMenu* m, uint8_t cursor = 0) { tmPush(PK_MENU, m, cursor); }
static void tmView(const TmView* v) { tmPush(PK_VIEW, v); }

// Who writes to the stove: held while a write action runs (see the top of this file)
static const char* tmSource()
{
    static char src[72];
    snprintf(src, sizeof(src), "telnet menu (%s)", tnS.peer);
    return src;
}
struct TmBus {
    StoveBusLock lock;
    CmdSource cs;
    TmBus() : cs(tmSource()) {}
};

// A dialog: replaces nothing, returns to the page below it when done
static void tmPickStart(const char* title, int lo, int hi, int v, bool wrap, void (*fmt)(int, String&), bool (*apply)(int), const char* help = nullptr)
{
    strncpy(tmPick.title, title, sizeof(tmPick.title) - 1);
    tmPick.title[sizeof(tmPick.title) - 1] = 0;
    tmPick.lo = lo; tmPick.hi = hi; tmPick.v = v < lo ? lo : v > hi ? hi : v;
    tmPick.wrap = wrap; tmPick.fmt = fmt; tmPick.apply = apply; tmPick.help = help;
    tmPush(PK_PICK, &tmPick);
}
static void tmInputStart(const char* prompt, void (*done)(const String&), uint8_t max = 40, const char* help = nullptr)
{
    strncpy(tmInput.prompt, prompt, sizeof(tmInput.prompt) - 1);
    tmInput.prompt[sizeof(tmInput.prompt) - 1] = 0;
    tmInput.buf = ""; tmInput.max = max; tmInput.done = done; tmInput.help = help; tmInput.secret = false;
    tmPush(PK_INPUT, &tmInput);
}
// The same for a password: typed characters are shown as *
static void tmInputSecretStart(const char* prompt, void (*done)(const String&), uint8_t max = 64, const char* help = nullptr)
{
    tmInputStart(prompt, done, max, help);
    tmInput.secret = true;
}
static void tmConfirmStart(const String& question, void (*yes)())
{
    tmConfirm.question = question; tmConfirm.yes = yes;
    tmPush(PK_CONFIRM, &tmConfirm);
}
// A page with a block of text (result of a raw read / write, the bus debug buffer)
static void tmTextRender() { tpText(tmText.c_str(), tmText.length()); }
static const TmView TM_TEXT_VIEW = { nullptr, tmTextRender, 0, false };
static void tmShowText(const char* title, const String& text)
{
    tmText = text; tmTextTitle = title;
    tmView(&TM_TEXT_VIEW);
}

// ── Drawing ───────────────────────────────────────────────────────────
static void tmHeader();   // telnetMenu.h

static char tmHotkey(int i) { return i < 9 ? '1' + i : 'a' + (i - 9); }
static int  tmHotkeyIndex(int k)
{
    if (k >= '1' && k <= '9') return k - '1';
    if (k >= 'a' && k <= 'p') return 9 + (k - 'a');
    return -1;
}

static void tmTitle(const char* t) { tpf(A_BOLD " %s" A_RST A_EOL, t); tpl(); }
static void tmFooter(const char* keys, const char* help)
{
    tpl();
    if (tmMsg.length()) { tpf("   %s%s" A_RST A_EOL, tmMsgErr ? A_RED : A_GRN, tmMsg.c_str()); }
    if (help && *help) tpf(A_CYN "   %s" A_RST A_EOL, help);
    tpf(A_DIM "   %s" A_RST A_EOL, keys);
}

static const char* tmKindSign(TmItemKind k)
{
    static const char* S[] = { "\xC2\xBB", "\xE2\x96\xB8", "\xE2\x89\xA1", "\xE2\x86\x95", "\xE2\x80\xA2", "!" };   // » ▸ ≡ ↕ • !
    return S[k];
}
static const char* tmKindColor(TmItemKind k)
{
    static const char* C[] = { A_GRN, A_BOLD A_CYN, A_CYN, A_YEL, A_YEL, A_BOLD A_RED };
    return C[k];
}
static const char* const TM_KIND_NAME[] = { "action", "submenu", "screen", "value", "on / off", "asks first" };
// A value: on / yes / [x] in green, off / no / [ ] dimmed, a value you can change in yellow
static void tmValue(TmItemKind k, const String& v)
{
    if (!v.length()) return;
    String l = v; l.toLowerCase();
    int e = 0; while (e < (int)l.length() && isalpha((unsigned char)l[e])) e++;   // first word
    String w = l.substring(0, e);
    bool on  = w == "on" || w == "yes" || w == "required" || w == "connected" || l.startsWith("[x]");
    bool off = w == "off" || w == "no" || w == "not" || l.startsWith("[ ]");
    if (on) tp(A_GRN); else if (off) tp(A_DIM); else if (k == TI_EDIT || k == TI_TOGGLE) tp(A_YEL);
    tp(v);
}

static void tmDrawMenu(const TmMenu* m, uint8_t cursor)
{
    tmTitle(m->title);
    if (m->info) { m->info(); tpl(); }
    String v;
    bool kinds[6] = { false, false, false, false, false, false };
    for (int i = 0; i < m->n; i++) {
        const TmItem& it = m->items[i];
        bool sel = i == cursor;
        v = "";
        if (it.value) it.value(i, v);
        kinds[it.kind] = true;
        char row[48];
        snprintf(row, sizeof(row), " %c  %-26s", tmHotkey(i), it.label);
        tpf(" %s%s" A_RST " %s%s" A_RST " ", sel ? A_INV : "", row, tmKindColor(it.kind), tmKindSign(it.kind));
        tmValue(it.kind, v);
        tp(A_RST A_EOL);
    }
    // legend of the signs used in this menu
    tp(A_DIM "  ");
    for (int k = 0; k < 6; k++) if (kinds[k]) tpf("   " A_RST "%s%s" A_RST A_DIM " %s", tmKindColor((TmItemKind)k), tmKindSign((TmItemKind)k), TM_KIND_NAME[k]);
    tp(A_RST A_EOL);
    tmFooter("[up/down] move  [enter] select  [1-9] direct  [esc/left] back  [?] help", m->n ? m->items[cursor].help : nullptr);
}

static void tmDrawPick()
{
    String v; tmPick.fmt(tmPick.v, v);
    tmTitle(tmPick.title);
    tpf("      " A_INV "  <  %s  >  " A_RST A_EOL, v.c_str());
    tmFooter("[up/down or left/right] change  [enter] set  [esc] cancel", tmPick.help);
}

static void tmDrawInput()
{
    tmTitle(tmInput.prompt);
    tmFooter("[enter] OK  [backspace] delete  [esc] cancel", tmInput.help);
    tpl();
    if (tmInput.secret) { tp("   > "); for (size_t i = 0; i < tmInput.buf.length(); i++) tp("*"); }
    else tpf("   > %s", tmInput.buf.c_str());   // last: the cursor stays behind the text
}

static void tmDrawConfirm()
{
    tmTitle("Please confirm");
    tpText(tmConfirm.question.c_str(), tmConfirm.question.length());
    tpl();
    tpl();
    tpl("   " A_BOLD "[y]" A_RST " yes      any other key: no");
}

static void tmDrawView(const TmView* v)
{
    tmTitle(v->title ? v->title : tmTextTitle);
    v->render();
    tmFooter(v->keys ? v->keys : v->refreshMs ? "any key: back   (refreshes by itself)" : "[r] refresh   any other key: back", nullptr);
}

static void tmDraw()
{
    if (tnS.fd < 0 || !tmDepth) return;
    TmPage& p = tmTop();
    const TmView* view = p.kind == PK_VIEW ? (const TmView*)p.def : nullptr;
    if (view && view->stream) {   // scrolling: only the title, the lines follow from tmTick
        if (tmClear) { tp(A_RST "\x1B[2J\x1B[H"); tmHeader(); tmTitle(view->title); tpl(A_DIM "   any key: stop" A_RST); tpl(); }
        tmClear = false;
        tmLastDraw = millis();
        return;
    }
    tp(tmClear ? A_RST "\x1B[2J\x1B[H" : A_RST "\x1B[H");
    tmClear = false;
    tmHeader();
    switch (p.kind) {
    case PK_MENU:    tmDrawMenu((const TmMenu*)p.def, p.cursor); break;
    case PK_VIEW:    tmDrawView(view); break;
    case PK_PICK:    tmDrawPick(); break;
    case PK_CONFIRM: tmDrawConfirm(); break;
    case PK_INPUT:   tp("\x1B[J"); tmDrawInput(); tmLastDraw = millis(); return;
    }
    tp("\x1B[J");   // clear what is left of the previous page below
    tmLastDraw = millis();
}

// ── Keys ──────────────────────────────────────────────────────────────
static bool tmIsBack(int k) { return k == TK_ESC || k == TK_LEFT || k == TK_BS || k == 'q' || k == 'Q'; }
static void tmHelp();       // telnetMenu.h
static void tmExit();       // telnetMenu.h

static void tmKeyMenu(TmPage& p, int k)
{
    const TmMenu* m = (const TmMenu*)p.def;
    int n = m->n;
    int sel = -1;
    if (k == TK_UP)        { p.cursor = (p.cursor + n - 1) % n; tmDraw(); return; }
    if (k == TK_DOWN || k == TK_TAB) { p.cursor = (p.cursor + 1) % n; tmDraw(); return; }
    if (k == TK_HOME || k == TK_PGUP) { p.cursor = 0; tmDraw(); return; }
    if (k == TK_END || k == TK_PGDN)  { p.cursor = n - 1; tmDraw(); return; }
    if (k == '?') { tmHelp(); return; }
    if (k == 'r' || k == 'R') { tmDraw(); return; }
    if (k == TK_ENTER || k == TK_RIGHT || k == ' ') sel = p.cursor;
    else if (tmHotkeyIndex(k) >= 0 && tmHotkeyIndex(k) < n) sel = tmHotkeyIndex(k);
    else if (tmIsBack(k) || k == TK_CTRL_C || k == TK_CTRL_D) {
        if (tmDepth > 1) tmPop();
        else if (k == 'q' || k == 'Q' || k == TK_CTRL_D || k == TK_CTRL_C) tmExit();
        return;
    }
    if (sel < 0) return;
    p.cursor = sel;
    int depth = tmDepth;
    if (m->items[sel].open) m->items[sel].open(sel);
    if (tmDepth == depth && tnS.fd >= 0) tmDraw();   // stayed on this page (toggle, message): show the result
}

static void tmKeyPick(int k)
{
    int d = (k == TK_UP || k == TK_RIGHT || k == '+') ? 1 : (k == TK_DOWN || k == TK_LEFT || k == '-') ? -1 : 0;
    if (k == TK_PGUP) d = 6;
    if (k == TK_PGDN) d = -6;
    if (d) {
        int v = tmPick.v + d;
        if (v > tmPick.hi) v = tmPick.wrap ? tmPick.lo + (v - tmPick.hi - 1) : tmPick.hi;
        if (v < tmPick.lo) v = tmPick.wrap ? tmPick.hi - (tmPick.lo - v - 1) : tmPick.lo;
        tmPick.v = v;
        tmDraw();
        return;
    }
    if (k == TK_ENTER) {
        bool ok;
        { TmBus bus; ok = tmPick.apply(tmPick.v); }
        if (ok && !tmMsg.length()) tmMsgSet(true, "Done.");
        tmPop();
        return;
    }
    if (k == TK_ESC || k == 'q' || k == 'Q' || k == TK_CTRL_C) { tmMsgSet(true, "Cancelled - nothing written."); tmPop(); }
}

static void tmKeyInput(int k)
{
    if (k == TK_ENTER) {
        String s = tmInput.buf; s.trim();
        void (*done)(const String&) = tmInput.done;
        tmDepth--;                      // the input page is finished; done() may open the next one
        tmClear = true;
        { TmBus bus; done(s); }
        if (tnS.fd >= 0 && tmClear) tmDraw();   // done() opened nothing: back on the page below
        return;
    }
    if (k == TK_ESC || k == TK_CTRL_C) { tmMsgSet(true, "Cancelled."); tmPop(); return; }
    if (k == TK_BS) {
        if (tmInput.buf.length()) { tmInput.buf.remove(tmInput.buf.length() - 1); tp("\b \b"); }
        return;
    }
    if (k >= 32 && k < 127 && tmInput.buf.length() < tmInput.max) {
        tmInput.buf += (char)k;
        char c[2] = { tmInput.secret ? '*' : (char)k, 0 };
        tp(c);   // echo: the client does not (WILL ECHO)
    }
}

static void tmKeyConfirm(int k)
{
    if (k == 'y' || k == 'Y') {
        void (*yes)() = tmConfirm.yes;
        tmDepth--;
        tmClear = true;
        { TmBus bus; yes(); }
        if (tnS.fd >= 0 && tmClear) tmDraw();
        return;
    }
    tmMsgSet(true, "Cancelled - nothing done.");
    tmPop();
}

static void tmKeyView(const TmView* v, int k)
{
    if (v->key && v->key(k)) { tmDraw(); return; }
    if (!v->stream && (k == 'r' || k == 'R')) { tmDraw(); return; }
    if (v->stream) tp(A_RST "\r\n");
    tmPop();
}
