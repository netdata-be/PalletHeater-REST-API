#pragma once
// Telnet server for the debug menu (port 23), on plain lwIP sockets.
//
// The ESPTelnet library used before (WiFiServer) only listens on IPv4: a connect to
// the IPv6 address of the controller got "Connection refused". This server listens on
// one dual-stack socket (AF_INET6 with IPV6_V6ONLY off), which takes IPv6 clients and
// IPv4 clients alike (lwIP hands the latter over as ::ffff:a.b.c.d, shown as a.b.c.d).
// At start it checks that the IPv4 side is really covered: an IPv4 socket on the same
// port must then fail to bind. If it binds, that IPv4 socket listens as well, so IPv4
// keeps working in any case.
//
// One session at a time. A second client is not dropped: it is told who uses the menu
// and can take the session over (the first client is told and disconnected) or leave.
// A third client meanwhile gets a one-line message and is closed. When the session ends
// while a second client waits, that client gets the menu.
//
// Runs in the loop task (telnetLoop() -> tnLoop()), non-blocking: no task of its own,
// no extra stack. Output is collected in a 512 byte buffer (allocated while a client is
// connected) and sent without blocking; a client that takes no data for 3 s is closed.
// Telnet commands (IAC ...) and ANSI escape sequences are decoded into keys (TK_*) and
// handed to the menu (telnetMenu.h): tmStart(), tmKey(), tmTick(), tmEnd().
#include <lwip/sockets.h>

#define TN_PORT          23
#define TN_OUT_LEN       512
#define TN_IDLE_MS       1800000UL   // a session without a key for 30 min is closed
#define TN_TAKEOVER_MS   30000UL     // a waiting second client decides within 30 s
#define TN_SEND_STALL_MS 3000UL      // the client takes no data for 3 s: closed

// Keys handed to the menu: printable characters as themselves, plus these
enum {
    TK_CTRL_C = 3, TK_CTRL_D = 4, TK_BS = 8, TK_TAB = 9, TK_ENTER = 13, TK_ESC = 27,
    TK_UP = 0x100, TK_DOWN, TK_RIGHT, TK_LEFT, TK_HOME, TK_END, TK_PGUP, TK_PGDN
};

struct TnConn {
    int      fd = -1;
    char     peer[48] = "";
    uint32_t since = 0;       // millis() of the connect
    uint32_t lastKey = 0;     // millis() of the last key
    uint8_t  iac = 0;         // telnet command parser state
    uint8_t  esc = 0;         // ANSI escape parser state
    uint8_t  escNum = 0;      // number in ESC [ n ~
    bool     lastCR = false;  // CR LF / CR NUL count as one Enter
};

static int      tnListen[2] = { -1, -1 };   // [0] IPv6 (dual-stack), [1] IPv4 when needed
static bool     tnRunning = false;          // telnetBegin() called, telnetStop() not
static bool     tnDualStack = false;        // [0] also takes IPv4
static uint32_t tnRetryAt = 0;
static TnConn   tnS;                        // the session
static TnConn   tnW;                        // a second client, asked whether to take over
static char*    tnOut = nullptr;            // output buffer (TN_OUT_LEN) while a client is connected
static size_t   tnOutLen = 0;
static bool     tnDrop = false;             // the session failed (send): closed at the end of the loop
static uint32_t tnSessions = 0, tnTakeovers = 0;

// The menu (telnetMenu.h)
static void tmStart();
static void tmKey(int k);
static void tmTick();
static void tmEnd();

// ── Sending ───────────────────────────────────────────────────────────
// Send without blocking: what fits now, then wait for room in steps of 100 ms
// (feeding the watchdog), up to TN_SEND_STALL_MS without progress.
static bool tnSendRaw(int fd, const char* p, size_t n)
{
    if (fd < 0) return false;
    uint32_t last = millis();
    while (n) {
        int r = lwip_send(fd, p, n, MSG_DONTWAIT);
        if (r > 0) { p += r; n -= r; last = millis(); continue; }
        if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (millis() - last >= TN_SEND_STALL_MS) return false;
        fd_set wr; FD_ZERO(&wr); FD_SET(fd, &wr);
        struct timeval tv = { 0, 100000 };
        lwip_select(fd + 1, nullptr, &wr, nullptr, &tv);
        esp_task_wdt_reset();
    }
    return true;
}
static void tnSendStr(int fd, const char* s) { tnSendRaw(fd, s, strlen(s)); }

static void tnFlush()
{
    if (!tnOutLen) return;
    if (tnS.fd >= 0 && !tnDrop && !tnSendRaw(tnS.fd, tnOut, tnOutLen)) tnDrop = true;
    tnOutLen = 0;
}

// Output of the menu: to the session only
static void tnWrite(const char* s, size_t n)
{
    if (tnS.fd < 0 || tnDrop) return;
    if (!tnOut) { if (!tnSendRaw(tnS.fd, s, n)) tnDrop = true; return; }   // no buffer (memory): send directly
    while (n) {
        size_t k = TN_OUT_LEN - tnOutLen;
        if (k > n) k = n;
        memcpy(tnOut + tnOutLen, s, k);
        tnOutLen += k; s += k; n -= k;
        if (tnOutLen == TN_OUT_LEN) tnFlush();
    }
}
static void tnWriteStr(const char* s) { tnWrite(s, strlen(s)); }

// ── Connections ───────────────────────────────────────────────────────
static void tnPeerText(const struct sockaddr_storage& ss, char* out, size_t n)
{
    if (ss.ss_family == AF_INET) {
        inet_ntoa_r(((const struct sockaddr_in*)&ss)->sin_addr, out, n);
        return;
    }
    const struct sockaddr_in6* a = (const struct sockaddr_in6*)&ss;
    const uint32_t* w = a->sin6_addr.un.u32_addr;
    if (w[0] == 0 && w[1] == 0 && w[2] == PP_HTONL(0xffffUL)) {   // IPv4 client on the dual-stack socket
        struct in_addr v4; v4.s_addr = w[3];
        inet_ntoa_r(v4, out, n);
        return;
    }
    inet6_ntoa_r(a->sin6_addr, out, n);
}

static void tnClose(TnConn& c)
{
    if (c.fd >= 0) lwip_close(c.fd);
    c = TnConn();
}

// Telnet options: the server echoes (our line editor) and the client sends every key
// at once instead of whole lines (character mode)
static void tnNegotiate(int fd)
{
    static const char seq[] = {
        (char)0xFF, (char)0xFB, 0x01,   // IAC WILL ECHO
        (char)0xFF, (char)0xFB, 0x03,   // IAC WILL SUPPRESS-GO-AHEAD
        (char)0xFF, (char)0xFD, 0x03    // IAC DO   SUPPRESS-GO-AHEAD
    };
    tnSendRaw(fd, seq, sizeof(seq));
}

static void tnStartSession(const TnConn& c)
{
    tnS = c;
    tnS.lastKey = millis();
    tnDrop = false;
    tnOutLen = 0;
    if (!tnOut) tnOut = (char*)malloc(TN_OUT_LEN);
    tnSessions++;
    // Reaching the controller over telnet counts like an HTTP request for the
    // confirmation of a new static IP / WiFi network (wifiInfo.h): it is reachable
    netHttpSeen = true;
    LOGI("sys", "telnet: session from %s", tnS.peer);
    tmStart();
}

static void tnEndSession(const char* why)
{
    if (tnS.fd < 0) return;
    tmEnd();
    tnFlush();
    LOGI("sys", "telnet: session from %s ended (%s, %lu min)", tnS.peer, why, (unsigned long)((millis() - tnS.since) / 60000));
    tnClose(tnS);
    tnDrop = false;
    if (tnW.fd >= 0) {   // somebody waits: give them the menu
        tnSendStr(tnW.fd, "\r\nThe other session ended - starting the menu.\r\n");
        TnConn w = tnW; tnW = TnConn();
        tnStartSession(w);
    } else if (tnOut) { free(tnOut); tnOut = nullptr; }
}

static void tnAskTakeover()
{
    char b[300];
    snprintf(b, sizeof(b),
             "\r\nOpenPelletBridge telnet menu\r\n\r\n"
             "The menu is in use by %s (connected %lu min, last key %lu s ago).\r\n\r\n"
             "  [t]  take over that session (it is disconnected)\r\n"
             "  any other key: leave\r\n",
             tnS.peer, (unsigned long)((millis() - tnS.since) / 60000), (unsigned long)((millis() - tnS.lastKey) / 1000));
    tnSendStr(tnW.fd, b);
}

static void tnTakeover()
{
    char b[96];
    snprintf(b, sizeof(b), "\r\n\r\n\x1B[0mSession taken over by %s. Bye.\r\n", tnW.peer);
    tnFlush();
    tnSendStr(tnS.fd, b);
    LOGI("sys", "telnet: session of %s taken over by %s", tnS.peer, tnW.peer);
    tnTakeovers++;
    tmEnd();
    tnClose(tnS);
    TnConn w = tnW; tnW = TnConn();
    tnStartSession(w);
}

static void tnAccept(int ls)
{
    struct sockaddr_storage peer = {};
    socklen_t pl = sizeof(peer);
    int fd = lwip_accept(ls, (struct sockaddr*)&peer, &pl);
    if (fd < 0) return;
    int one = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // A client that disappears without closing (NAT timeout, laptop asleep) is found by
    // TCP keepalive after about 2 min instead of blocking the menu until the idle timeout
    lwip_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#ifdef TCP_KEEPIDLE
    int idle = 60, intvl = 15, cnt = 4;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
    TnConn c;
    c.fd = fd;
    c.since = c.lastKey = millis();
    tnPeerText(peer, c.peer, sizeof(c.peer));
    if (tnS.fd < 0) {
        tnNegotiate(fd);
        tnStartSession(c);
    } else if (tnW.fd < 0) {
        tnW = c;
        tnNegotiate(fd);
        LOGI("sys", "telnet: %s connected while %s uses the menu - asking whether to take over", tnW.peer, tnS.peer);
        tnAskTakeover();
    } else {
        tnSendStr(fd, "\r\nOpenPelletBridge: the telnet menu is in use and another client waits. Try again later.\r\n");
        LOGI("sys", "telnet: %s refused (menu in use by %s, %s waiting)", c.peer, tnS.peer, tnW.peer);
        lwip_close(fd);
    }
}

// ── Input: telnet commands and ANSI escape sequences -> keys ──────────
// Returns the key for byte b, or -1 when b is part of a sequence (or ignored).
static int tnDecode(TnConn& c, uint8_t b)
{
    // telnet commands: IAC (255) cmd [option], IAC SB ... IAC SE; IAC IAC = byte 255
    switch (c.iac) {
    case 1:   // after IAC
        if (b == 255) { c.iac = 0; break; }                    // escaped 255: data
        c.iac = (b >= 251 && b <= 254) ? 2 : b == 250 ? 3 : 0;  // WILL/WONT/DO/DONT take an option, SB a block
        return -1;
    case 2: c.iac = 0; return -1;                              // the option
    case 3: if (b == 255) c.iac = 4; return -1;                // sub-negotiation: until IAC SE
    case 4: c.iac = b == 240 ? 0 : 3; return -1;
    default:
        if (b == 255) { c.iac = 1; return -1; }
    }
    // ANSI: ESC [ A..D (arrows), ESC [ n ~ (home, end, page up / down), ESC O A..D
    if (c.esc == 1) {
        if (b == '[') { c.esc = 2; c.escNum = 0; return -1; }
        if (b == 'O') { c.esc = 3; return -1; }
        c.esc = 0;   // ESC followed by something else: the ESC key was meant, b is lost
        return TK_ESC;
    }
    if (c.esc == 2 && b >= '0' && b <= '9') { c.escNum = c.escNum * 10 + (b - '0'); return -1; }
    if (c.esc == 2 || c.esc == 3) {
        c.esc = 0;
        switch (b) {
        case 'A': return TK_UP;
        case 'B': return TK_DOWN;
        case 'C': return TK_RIGHT;
        case 'D': return TK_LEFT;
        case 'H': return TK_HOME;
        case 'F': return TK_END;
        case '~':
            switch (c.escNum) {
            case 1: case 7: return TK_HOME;
            case 4: case 8: return TK_END;
            case 5: return TK_PGUP;
            case 6: return TK_PGDN;
            }
        }
        return -1;
    }
    if (b == 27) { c.esc = 1; return -1; }
    // Enter: CR, CR LF, CR NUL or LF
    if (b == '\r') { c.lastCR = true; return TK_ENTER; }
    bool afterCR = c.lastCR;
    c.lastCR = false;
    if (b == '\n') return afterCR ? -1 : TK_ENTER;
    if (b == 0) return -1;
    if (b == 127) return TK_BS;
    return b;
}

// Read what the client sent; false when the connection is gone
static bool tnRead(TnConn& c, bool session)
{
    uint8_t buf[64];
    const int fd = c.fd;   // c is tnS / tnW, which change when the session ends or is taken over
    int n = lwip_recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n == 0) return false;
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK;
    for (int i = 0; i < n; i++) {
        int k = tnDecode(c, buf[i]);
        if (c.esc == 1 && i == n - 1) { c.esc = 0; k = TK_ESC; }   // a lone ESC at the end: the ESC key
        if (k < 0) continue;
        c.lastKey = millis();
        if (session) {
            tmKey(k);
            if (tnS.fd != fd) return true;   // the menu ended the session
        } else {
            // the waiting client: take over or leave
            if (k == 't' || k == 'T') { tnTakeover(); return true; }
            tnSendStr(fd, "\r\nOK - bye.\r\n");
            return false;
        }
    }
    return true;
}

// ── Listening ─────────────────────────────────────────────────────────
static int tnListenOn(int family)
{
    int ls = lwip_socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (ls < 0) return -1;
    int one = 1, zero = 0;
    lwip_setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    int rc;
    if (family == AF_INET6) {
        lwip_setsockopt(ls, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));   // dual-stack: IPv4 as well
        struct sockaddr_in6 a = {}; a.sin6_family = AF_INET6; a.sin6_port = htons(TN_PORT); a.sin6_addr = in6addr_any;
        rc = lwip_bind(ls, (struct sockaddr*)&a, sizeof(a));
    } else {
        struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(TN_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
        rc = lwip_bind(ls, (struct sockaddr*)&a, sizeof(a));
    }
    if (rc != 0 || lwip_listen(ls, 2) != 0) { lwip_close(ls); return -1; }
    return ls;
}

// Is IPv4 port 23 still free next to the dual-stack socket? Then lwIP did not bind
// that socket for IPv4 too: keep this IPv4 socket listening. (No SO_REUSEADDR here,
// so the bind fails when the dual-stack socket covers IPv4.)
static int tnIpv4Needed()
{
    int ls = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls < 0) return -1;
    struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(TN_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lwip_bind(ls, (struct sockaddr*)&a, sizeof(a)) != 0) { lwip_close(ls); return -1; }
    if (lwip_listen(ls, 2) != 0) { lwip_close(ls); return -1; }
    return ls;
}

static void tnOpen()
{
    tnListen[0] = tnListenOn(AF_INET6);
    if (tnListen[0] >= 0) {
        tnListen[1] = tnIpv4Needed();
        tnDualStack = tnListen[1] < 0;
    } else {
        tnListen[1] = tnListenOn(AF_INET);   // no IPv6 socket: at least IPv4
        tnDualStack = false;
    }
    if (tnListen[0] < 0 && tnListen[1] < 0) {
        LOGE("sys", "telnet could not start (port %d)", TN_PORT);
        tnRetryAt = millis() + 30000;
        return;
    }
    LOGI("sys", "telnet running on port %d (%s)", TN_PORT,
         tnListen[0] >= 0 ? (tnDualStack ? "IPv4 and IPv6, one dual-stack socket" : "IPv4 and IPv6, two sockets") : "IPv4 only");
}

static bool tnListening() { return tnListen[0] >= 0 || tnListen[1] >= 0; }
static const char* tnListenText()
{
    if (!tnRunning) return "off";
    if (!tnListening()) return "not listening (no network yet)";
    return tnListen[0] >= 0 ? "IPv4 + IPv6" : "IPv4 only";
}

static void tnLoop()
{
    if (!tnRunning) return;
    if (!tnListening()) {
        if (WiFi.status() != WL_CONNECTED || (int32_t)(millis() - tnRetryAt) < 0) return;
        tnOpen();
        if (!tnListening()) return;
    }
    fd_set rd; FD_ZERO(&rd);
    int mx = -1;
    auto add = [&](int fd) { if (fd >= 0) { FD_SET(fd, &rd); if (fd > mx) mx = fd; } };
    add(tnListen[0]); add(tnListen[1]); add(tnS.fd); add(tnW.fd);
    struct timeval tv = { 0, 0 };
    if (lwip_select(mx + 1, &rd, nullptr, nullptr, &tv) > 0) {
        for (int i = 0; i < 2; i++) if (tnListen[i] >= 0 && FD_ISSET(tnListen[i], &rd)) tnAccept(tnListen[i]);
        if (tnS.fd >= 0 && FD_ISSET(tnS.fd, &rd) && !tnRead(tnS, true)) tnEndSession("client closed");
        if (tnW.fd >= 0 && FD_ISSET(tnW.fd, &rd) && !tnRead(tnW, false)) tnClose(tnW);
    }
    if (tnW.fd >= 0 && millis() - tnW.since > TN_TAKEOVER_MS) {
        tnSendStr(tnW.fd, "\r\nNo answer - bye.\r\n");
        tnClose(tnW);
    }
    if (tnS.fd >= 0 && millis() - tnS.lastKey > TN_IDLE_MS) {
        tnWriteStr("\r\n\x1B[0mNo key for 30 min - closing the session.\r\n");
        tnEndSession("idle timeout");
    }
    if (tnS.fd >= 0) tmTick();
    tnFlush();
    if (tnDrop) tnEndSession("client takes no data");
}

static void tnStop()
{
    tnRunning = false;
    if (tnW.fd >= 0) { tnSendStr(tnW.fd, "\r\nTelnet switched off - bye.\r\n"); tnClose(tnW); }   // first: not promoted below
    if (tnS.fd >= 0) { tnWriteStr("\r\n\x1B[0mTelnet switched off - bye.\r\n"); tnEndSession("telnet switched off"); }
    for (int i = 0; i < 2; i++) if (tnListen[i] >= 0) { lwip_close(tnListen[i]); tnListen[i] = -1; }
    if (tnOut) { free(tnOut); tnOut = nullptr; }
}
