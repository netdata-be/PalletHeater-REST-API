#pragma once
// HTTPS on port 443, in front of the HTTP server on port 80.
//
// A task accepts TLS connections on 443, decrypts them and passes the plain HTTP stream on
// to the existing web server over 127.0.0.1:80 (and the answer back). All routes, the API
// key check and the large-answer handling stay exactly as they are; HTTP on port 80 keeps
// working too.
//
// Certificate: self-signed, made on the device itself the first time (no seed in the
// firmware: the firmware is public, a key derived from it would be known to everyone).
//   - key: ECDSA P-256, generated with the hardware RNG, kept in NVS ("https"/"key"); it
//     never leaves the device
//   - certificate: CN = hostname, subjectAltName = hostname, hostname.local and the current
//     IP address; serverAuth; valid 820 days (Apple accepts at most 825)
//   - re-issued with the same key when the hostname or the IP address changes or when less
//     than 30 days are left; a new key only on request (POST /api/https regenerate=true)
// The browser warns once (self-signed): compare the SHA-256 fingerprint in the web app
// (Device) with the one the browser shows, or install the certificate (GET
// /api/https?cert=pem) as trusted on your own devices.
//
// Memory: a TLS session needs two record buffers of ~16.7 KB each, in two separate blocks.
// Allocated per connection that failed more and more as the heap got fragmented (78 KB free
// but no second 17 KB block), so they are reserved once at start-up, while the heap is still
// in one piece, and reused for every connection (mbedtls_ssl_session_reset). They are
// handed back only while the GitHub update runs (it needs its own TLS connection) and taken
// again right after. One session at a time; other connections wait in the listen queue (the
// HTTP server answers one request at a time anyway). Session resumption (cache) keeps the
// following connections of a browser cheap: no new ECDSA handshake.
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cache.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecp.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <mbedtls/error.h>

#define HTTPS_PORT       443
#define HTTPS_BUF_LEN    1460    // relay buffer between TLS and the local HTTP server
#define HTTPS_HDR_MAX    1024    // answer header collected to rewrite "Connection: close"
#define HTTPS_REQ_MAX_MS 150000UL    // one request + answer at most (the web server stops itself after 120 s)
#define HTTPS_UPLOAD_MAX_MS 600000UL // a large upload (firmware, > 64 KB)
#define HTTPS_KEEP_IDLE_MS 15000 // keep-alive: wait this long for the next request (if nobody else waits)
#define HTTPS_DAYS       820
#define HTTPS_RENEW_DAYS 30

static bool httpsEnabled = true;            // NVS "https"/"enabled", default on
static bool httpsRedirect = false;          // NVS "https"/"redirect": web app pages on HTTP -> HTTPS (default off)
static String httpsAlt;                     // NVS "https"/"alt": extra subjectAltNames, comma-separated
#define HTTPS_MAX_ALT 8
static volatile bool httpsRegen = false;    // POST regenerate: new key + certificate
static volatile bool httpsReload = false;   // enabled changed
static volatile bool httpsCheck = false;    // alt names changed: check / re-issue the certificate now
static bool httpsListening = false;
static uint8_t* httpsCrt = nullptr;        // certificate (DER), exactly httpsCrtLen bytes on the heap
#define HTTPS_CRT_MAX 2048
static size_t   httpsCrtLen = 0;
static uint32_t httpsFrom = 0, httpsUntil = 0;   // validity (unix time)
static String   httpsSan;                   // "hostname|a.b.c.d|alt1,alt2" the certificate was made for
static String   httpsMsg;                   // last problem, for /api/https
static struct { uint32_t sessions, requests, refused, failed, lastHandshakeMs, maxHandshakeMs, minFreeDuring; } httpsStats = {};
static TaskHandle_t httpsTaskHandle = nullptr;

static mbedtls_ssl_config        httpsConf;
static mbedtls_x509_crt          httpsCert;
static mbedtls_pk_context        httpsKey;
static mbedtls_ssl_cache_context httpsCache;
static bool httpsConfigured = false;
static mbedtls_ssl_context httpsSsl;        // reused for every connection (buffers reserved once)
static bool     httpsSslReady = false;
static uint8_t* httpsBuf = nullptr;
static uint8_t* httpsHdr = nullptr;         // HTTPS_HDR_MAX bytes

// Reserve the TLS record buffers (mbedtls_ssl_setup allocates them) and the relay buffer
static bool httpsReserve()
{
    if (httpsSslReady) return true;
    if (!httpsBuf) httpsBuf = (uint8_t*)malloc(HTTPS_BUF_LEN);
    if (!httpsHdr) httpsHdr = (uint8_t*)malloc(HTTPS_HDR_MAX);
    if (!httpsHdr) return false;
    mbedtls_ssl_init(&httpsSsl);
    int r = httpsBuf ? mbedtls_ssl_setup(&httpsSsl, &httpsConf) : MBEDTLS_ERR_SSL_ALLOC_FAILED;
    if (r) { mbedtls_ssl_free(&httpsSsl); return false; }
    httpsSslReady = true; httpsHoldsTls = true;
    LOGI("https", "TLS buffers reserved (%u bytes free now, largest block %u)",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return true;
}
static void httpsRelease(const char* why)
{
    if (!httpsSslReady) return;
    mbedtls_ssl_free(&httpsSsl);
    free(httpsBuf); httpsBuf = nullptr;
    free(httpsHdr); httpsHdr = nullptr;
    httpsSslReady = false; httpsHoldsTls = false;
    LOGI("https", "TLS buffers handed back (%s)", why);
}

static int httpsRng(void*, unsigned char* out, size_t len) { esp_fill_random(out, len); return 0; }

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
static int32_t httpsDays(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static void httpsTimeStr(uint32_t t, char* out)   // "YYYYMMDDhhmmss" (UTC), for mbedtls
{
    time_t tt = t; struct tm tm; gmtime_r(&tt, &tm);
    strftime(out, 16, "%Y%m%d%H%M%S", &tm);
}

static String httpsIso(uint32_t t)
{
    time_t tt = t; struct tm tm; gmtime_r(&tt, &tm);
    char b[24]; strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

static bool httpsTimeValid() { return time(nullptr) > 1700000000; }

// start of the validity: now (NTP) or, before NTP, the build time; a day earlier for clocks
// that are a bit behind
static uint32_t httpsStart()
{
    if (httpsTimeValid()) return (uint32_t)time(nullptr) - 86400;
    int y = 2026, mo = 1, d = 1, h = 0, mi = 0, s = 0;
    sscanf(BUILD_TIME, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    return (uint32_t)(httpsDays(y, mo, d) * 86400L + h * 3600L + mi * 60L + s) - 86400;
}

static bool httpsSetCrt(const uint8_t* der, size_t len)
{
    uint8_t* c = (uint8_t*)realloc(httpsCrt, len);
    if (!c) return false;
    memcpy(c, der, len); httpsCrt = c; httpsCrtLen = len;
    return true;
}

static String httpsHostname() { return String(WiFi.getHostname() ? WiFi.getHostname() : "openpelletbridge"); }
static String httpsSanNow() { return httpsHostname() + "|" + WiFi.localIP().toString() + "|" + httpsAlt; }

// An extra alt name: an IPv4 address, or a DNS name (labels of letters, digits and '-', at
// most 100 characters; '*.' in front for a wildcard)
static bool httpsValidAlt(const String& a)
{
    if (a.length() < 1 || a.length() > 100) return false;
    IPAddress ip; if (ip.fromString(a)) return true;
    int start = a.startsWith("*.") ? 2 : 0, label = 0;
    for (int i = start; i <= (int)a.length(); i++) {
        char c = i < (int)a.length() ? a[i] : '.';
        if (c == '.') {
            if (label == 0 || a[i - 1] == '-' || a[i - label] == '-') return false;
            label = 0;
        } else if (isalnum((unsigned char)c) || c == '-') { if (++label > 63) return false; }
        else return false;
    }
    return true;
}

// subjectAltName: dNSName hostname, dNSName hostname.local, iPAddress of the device, then
// the extra names (DER, built by hand: mbedtls 2.28 has no writer for it)
static size_t httpsSanDer(uint8_t* out, size_t cap)
{
    String h = httpsHostname(), hl = h + ".local";
    IPAddress ip = WiFi.localIP();
    uint8_t body[1024]; size_t n = 0;
    auto put = [&](uint8_t tag, const uint8_t* v, size_t len) {   // every value < 128 bytes
        if (n + 2 + len > sizeof(body)) return;
        body[n++] = tag; body[n++] = (uint8_t)len; memcpy(body + n, v, len); n += len;
    };
    put(0x82, (const uint8_t*)h.c_str(), h.length());
    put(0x82, (const uint8_t*)hl.c_str(), hl.length());
    uint8_t a[4] = { ip[0], ip[1], ip[2], ip[3] };
    if (ip != IPAddress(0, 0, 0, 0)) put(0x87, a, 4);
    int from = 0;
    while (from < (int)httpsAlt.length()) {
        int comma = httpsAlt.indexOf(',', from); if (comma < 0) comma = httpsAlt.length();
        String alt = httpsAlt.substring(from, comma); from = comma + 1;
        IPAddress aip;
        if (aip.fromString(alt)) { uint8_t b[4] = { aip[0], aip[1], aip[2], aip[3] }; put(0x87, b, 4); }
        else if (alt.length()) put(0x82, (const uint8_t*)alt.c_str(), alt.length());
    }
    size_t hdr = n < 128 ? 2 : n < 256 ? 3 : 4;   // SEQUENCE with a short or long length
    if (hdr + n > cap) return 0;
    out[0] = 0x30;
    if (hdr == 2) out[1] = (uint8_t)n;
    else if (hdr == 3) { out[1] = 0x81; out[2] = (uint8_t)n; }
    else { out[1] = 0x82; out[2] = (uint8_t)(n >> 8); out[3] = (uint8_t)n; }
    memcpy(out + hdr, body, n);
    return hdr + n;
}

static String httpsFingerprint()
{
    if (!httpsCrtLen) return "";
    uint8_t h[32];
    mbedtls_sha256_ret(httpsCrt, httpsCrtLen, h, 0);
    String s;
    for (int i = 0; i < 32; i++) { char b[4]; snprintf(b, sizeof(b), i ? ":%02X" : "%02X", h[i]); s += b; }
    return s;
}

// Make the certificate; with newKey (or no stored key) also a new key. Stores both in NVS.
static bool httpsMakeCert(bool newKey)
{
    Preferences p; p.begin("https", false);
    uint8_t keyDer[200]; size_t keyLen = p.getBytesLength("key");
    mbedtls_pk_context key; mbedtls_pk_init(&key);
    int r = 0;
    if (!newKey && keyLen > 0 && keyLen <= sizeof(keyDer)) {
        p.getBytes("key", keyDer, keyLen);
        r = mbedtls_pk_parse_key(&key, keyDer, keyLen, nullptr, 0);
        if (r) LOGW("https", "stored key unreadable (-0x%04X) - making a new one", -r);
    }
    if (newKey || keyLen == 0 || r) {
        uint32_t t0 = millis();
        r = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
        if (!r) r = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), httpsRng, nullptr);
        if (r) { httpsMsg = "key generation failed"; LOGE("https", "key generation failed (-0x%04X)", -r); mbedtls_pk_free(&key); p.end(); return false; }
        uint8_t buf[200];
        int len = mbedtls_pk_write_key_der(&key, buf, sizeof(buf));   // written at the end of buf
        if (len <= 0) { httpsMsg = "key export failed"; mbedtls_pk_free(&key); p.end(); return false; }
        p.putBytes("key", buf + sizeof(buf) - len, len);
        LOGI("https", "new ECDSA P-256 key generated in %lu ms", (unsigned long)(millis() - t0));
    }

    mbedtls_x509write_cert crt; mbedtls_x509write_crt_init(&crt);
    mbedtls_mpi serial; mbedtls_mpi_init(&serial);
    uint8_t sn[16]; esp_fill_random(sn, sizeof(sn)); sn[0] &= 0x7F; sn[0] |= 0x01;   // positive, 16 bytes
    mbedtls_mpi_read_binary(&serial, sn, sizeof(sn));
    String host = httpsHostname();
    String subject = "CN=" + host + ",O=OpenPelletBridge";
    uint32_t from = httpsStart(), until = from + HTTPS_DAYS * 86400UL;
    char nb[16], na[16]; httpsTimeStr(from, nb); httpsTimeStr(until, na);
    // temporary buffers, only while the certificate is made
    uint8_t* san = (uint8_t*)malloc(1100);
    uint8_t* der = (uint8_t*)malloc(HTTPS_CRT_MAX);
    size_t sanLen = san ? httpsSanDer(san, 1100) : 0;
    if (!san || !der || !sanLen) r = MBEDTLS_ERR_X509_ALLOC_FAILED;
    static const uint8_t eku[] = { 0x30, 0x0A, 0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01 };   // serverAuth

    if (!r) mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    r = mbedtls_x509write_crt_set_subject_name(&crt, subject.c_str());
    if (!r) r = mbedtls_x509write_crt_set_issuer_name(&crt, subject.c_str());
    if (!r) r = mbedtls_x509write_crt_set_serial(&crt, &serial);
    if (!r) r = mbedtls_x509write_crt_set_validity(&crt, nb, na);
    if (!r) r = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    if (!r) r = mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if (!r) r = mbedtls_x509write_crt_set_subject_key_identifier(&crt);
    if (!r) r = mbedtls_x509write_crt_set_extension(&crt, MBEDTLS_OID_SUBJECT_ALT_NAME, MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME), 0, san, sanLen);
    if (!r) r = mbedtls_x509write_crt_set_extension(&crt, MBEDTLS_OID_EXTENDED_KEY_USAGE, MBEDTLS_OID_SIZE(MBEDTLS_OID_EXTENDED_KEY_USAGE), 0, eku, sizeof(eku));
    int len = r ? r : mbedtls_x509write_crt_der(&crt, der, HTTPS_CRT_MAX, httpsRng, nullptr);
    mbedtls_x509write_crt_free(&crt); mbedtls_mpi_free(&serial); mbedtls_pk_free(&key);
    bool stored = len > 0 && httpsSetCrt(der + HTTPS_CRT_MAX - len, len);   // the DER is written at the end
    free(san); free(der);
    if (!stored) {
        httpsMsg = "certificate could not be made"; LOGE("https", "certificate could not be made (-0x%04X)", len < 0 ? -len : 0);
        p.end(); return false;
    }
    httpsFrom = from; httpsUntil = until; httpsSan = httpsSanNow();
    p.putBytes("crt", httpsCrt, httpsCrtLen);
    p.putUInt("from", from); p.putUInt("until", until); p.putString("san", httpsSan);
    p.end();
    httpsMsg = "";
    LOGI("https", "certificate for %s (%s) valid until %s, SHA-256 %s", host.c_str(), WiFi.localIP().toString().c_str(),
         httpsIso(until).c_str(), httpsFingerprint().c_str());
    return true;
}

// Load the stored certificate; make a new one when there is none, or re-issue it (same key)
// when the hostname / IP changed or it expires within HTTPS_RENEW_DAYS
static bool httpsEnsureCert(bool forceNewKey)
{
    if (forceNewKey) return httpsMakeCert(true);
    Preferences p; p.begin("https", true);
    size_t len = p.getBytesLength("crt");
    bool ok = len > 0 && len <= HTTPS_CRT_MAX && p.getBytesLength("key") > 0;
    if (ok) {
        uint8_t* c = (uint8_t*)realloc(httpsCrt, len);
        ok = c != nullptr;
        if (ok) { httpsCrt = c; httpsCrtLen = len; p.getBytes("crt", httpsCrt, httpsCrtLen); }
    }
    if (ok) {
        httpsFrom = p.getUInt("from", 0); httpsUntil = p.getUInt("until", 0); httpsSan = p.getString("san", "");
    }
    p.end();
    if (!ok) { httpsCrtLen = 0; LOGI("https", "no certificate yet - making one"); return httpsMakeCert(false); }
    if (httpsSan != httpsSanNow()) {
        LOGI("https", "hostname / IP / alt names changed (%s -> %s): certificate re-issued with the same key", httpsSan.c_str(), httpsSanNow().c_str());
        return httpsMakeCert(false);
    }
    if (httpsTimeValid() && (uint32_t)time(nullptr) + HTTPS_RENEW_DAYS * 86400UL > httpsUntil) {
        LOGI("https", "certificate expires %s: re-issued with the same key", httpsIso(httpsUntil).c_str());
        return httpsMakeCert(false);
    }
    LOGI("https", "certificate loaded: %s, valid %s - %s, SHA-256 %s", httpsSan.c_str(), httpsIso(httpsFrom).c_str(), httpsIso(httpsUntil).c_str(), httpsFingerprint().c_str());
    return true;
}

static void httpsUnconfigure()
{
    if (!httpsConfigured) return;
    mbedtls_ssl_config_free(&httpsConf); mbedtls_x509_crt_free(&httpsCert);
    mbedtls_pk_free(&httpsKey); mbedtls_ssl_cache_free(&httpsCache);
    httpsConfigured = false;
}

static bool httpsConfigure()
{
    // the reserved TLS context only refers to httpsConf by address, and no session runs now:
    // the configuration can be rebuilt in place, the buffers stay
    httpsUnconfigure();
    mbedtls_ssl_config_init(&httpsConf); mbedtls_x509_crt_init(&httpsCert);
    mbedtls_pk_init(&httpsKey); mbedtls_ssl_cache_init(&httpsCache);
    httpsConfigured = true;
    Preferences p; p.begin("https", true);
    uint8_t keyDer[200]; size_t keyLen = p.getBytesLength("key");
    if (keyLen > sizeof(keyDer)) keyLen = 0;
    if (keyLen) p.getBytes("key", keyDer, keyLen);
    p.end();
    int r = keyLen ? mbedtls_pk_parse_key(&httpsKey, keyDer, keyLen, nullptr, 0) : -1;
    memset(keyDer, 0, sizeof(keyDer));
    if (!r) r = mbedtls_x509_crt_parse_der(&httpsCert, httpsCrt, httpsCrtLen);
    if (!r) r = mbedtls_ssl_config_defaults(&httpsConf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (!r) {
        mbedtls_ssl_conf_rng(&httpsConf, httpsRng, nullptr);
        mbedtls_ssl_conf_min_version(&httpsConf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);   // TLS 1.2
        mbedtls_ssl_cache_set_max_entries(&httpsCache, 8);   // a few browsers / devices
        // key exchange: X25519 first (about twice as fast as P-256 in software, every browser
        // has it), P-256 for the rest; the certificate itself stays ECDSA P-256
        static const mbedtls_ecp_group_id curves[] = { MBEDTLS_ECP_DP_CURVE25519, MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_NONE };
        mbedtls_ssl_conf_curves(&httpsConf, curves);
        mbedtls_ssl_cache_set_timeout(&httpsCache, 3600);
        mbedtls_ssl_conf_session_cache(&httpsConf, &httpsCache, mbedtls_ssl_cache_get, mbedtls_ssl_cache_set);
        r = mbedtls_ssl_conf_own_cert(&httpsConf, &httpsCert, &httpsKey);
    }
    if (r) { httpsMsg = "TLS setup failed"; LOGE("https", "TLS setup failed (-0x%04X)", -r); httpsUnconfigure(); return false; }
    if (!fwBootCheckPending && !fwBusy) httpsReserve();   // else the task reserves them after the GitHub check
    return true;
}

// ── one connection ──────────────────────────────────────────────────────────
static int httpsBioSend(void* ctx, const unsigned char* buf, size_t len)
{
    int n = lwip_send(*(int*)ctx, buf, len, 0);
    if (n < 0) return MBEDTLS_ERR_NET_SEND_FAILED;   // blocking socket: EAGAIN = send timeout (client stopped reading)
    return n;
}
static int httpsBioRecv(void* ctx, unsigned char* buf, size_t len)
{
    int n = lwip_recv(*(int*)ctx, buf, len, 0);
    if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
    if (n == 0) return MBEDTLS_ERR_NET_CONN_RESET;
    return n;
}

static void httpsTimeouts(int fd, int ms)
{
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1; lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

static bool httpsSendAll(int fd, const uint8_t* b, int n)
{
    while (n > 0) { int w = lwip_send(fd, b, n, 0); if (w <= 0) return false; b += w; n -= w; }
    return true;
}

static bool httpsSslWriteAll(mbedtls_ssl_context& ssl, const uint8_t* b, size_t n)
{
    while (n > 0) {
        int w = mbedtls_ssl_write(&ssl, b, n);
        if (w == MBEDTLS_ERR_SSL_WANT_WRITE || w == MBEDTLS_ERR_SSL_WANT_READ) continue;
        if (w <= 0) return false;
        b += w; n -= w;
    }
    return true;
}

// case-insensitive search for needle in p[0 .. len)
static const char* httpsFindCI(const char* p, size_t len, const char* needle)
{
    size_t nl = strlen(needle);
    for (size_t i = 0; i + nl <= len; i++) if (strncasecmp(p + i, needle, nl) == 0) return p + i;
    return nullptr;
}

// The web server ends every answer with "Connection: close" and closes. For keep-alive the
// header says "Connection: keep-alive" instead - but only when the browser can find the end
// of the answer without the connection closing: Content-Length, chunked, or no body at all.
static String httpsRewriteHeader(const char* h, size_t len, bool allowKeep, bool* keep)
{
    String out; out.reserve(len + 48);
    bool hasLen = false, chunked = false, noBody = false, first = true;
    for (size_t pos = 0; pos < len;) {
        const char* p = h + pos;
        const char* e = httpsFindCI(p, len - pos, "\r\n");
        size_t ll = e ? (size_t)(e - p) : len - pos;
        pos += ll + 2;
        if (ll == 0) break;                      // the empty line: end of the header
        if (first) {
            first = false;
            const char* sp = (const char*)memchr(p, ' ', ll);
            int code = sp ? atoi(sp + 1) : 0;
            noBody = code == 204 || code == 304 || (code >= 100 && code < 200);
        } else if (!strncasecmp(p, "connection:", 11) || !strncasecmp(p, "keep-alive:", 11)) {
            continue;                            // replaced below
        } else {
            if (!strncasecmp(p, "content-length:", 15)) hasLen = true;
            if (!strncasecmp(p, "transfer-encoding:", 18) && httpsFindCI(p, ll, "chunked")) chunked = true;
        }
        out.concat(p, ll); out += "\r\n";
    }
    *keep = allowKeep && (hasLen || chunked || noBody);
    out += *keep ? "Connection: keep-alive\r\nKeep-Alive: timeout=15\r\n\r\n" : "Connection: close\r\n\r\n";
    return out;
}

// HTTP over the TLS session, with keep-alive: every request gets its own connection to the
// web server (127.0.0.1:80, which closes after each answer); the TLS session stays open for
// the next request of the same browser. Returns why the session ended.
static const char* httpsRelay(mbedtls_ssl_context& ssl, int fd, int ls, uint32_t& bytesIn, uint32_t& bytesOut, uint32_t& requests)
{
    uint8_t* buf = httpsBuf;
    for (;;) {
        // 1. wait for the next request; while idle between requests, give way to a client
        //    that waits in the listen queue (one TLS session at a time)
        uint32_t t0 = millis();
        for (;;) {
            if (mbedtls_ssl_get_bytes_avail(&ssl) > 0) break;
            fd_set rd; FD_ZERO(&rd); FD_SET(fd, &rd);
            if (requests) FD_SET(ls, &rd);
            struct timeval tv = { 0, 200000 };
            if (lwip_select((fd > ls ? fd : ls) + 1, &rd, nullptr, nullptr, &tv) < 0) return "select error";
            if (FD_ISSET(fd, &rd)) break;
            if (requests && FD_ISSET(ls, &rd)) return "keep-alive ended: another client waits";
            if (requests && (fwBusy || httpsRegen || httpsCheck)) return "keep-alive ended: update / new certificate";
            if (millis() - t0 > (requests ? HTTPS_KEEP_IDLE_MS : 10000)) return requests ? "keep-alive idle" : "no request";
        }
        int n = mbedtls_ssl_read(&ssl, buf, HTTPS_BUF_LEN);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (n <= 0) return n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ? "closed by the client" : "TLS read error";
        // large uploads (firmware) close after the answer, as before keep-alive
        bool allowKeep = true;
        const char* cl = httpsFindCI((const char*)buf, n, "\r\ncontent-length:");
        if (cl && atol(cl + 17) > 65536) allowKeep = false;

        // 2. a connection to the web server for this request
        int lfd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (lfd < 0) return "no socket for the local HTTP server";
        struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(80); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        httpsTimeouts(lfd, 10000);
        if (lwip_connect(lfd, (struct sockaddr*)&a, sizeof(a)) != 0) { lwip_close(lfd); return "local HTTP server not reachable"; }
        proxyPeerSet(0, lfd, true, httpsPeerText);   // logs show the real client
        const char* why = nullptr;
        if (!httpsSendAll(lfd, buf, n)) why = "local HTTP server stopped reading";
        bytesIn += n;
        bool answering = false, hdrDone = false, keep = false;
        size_t hlen = 0;
        uint32_t idle = millis(), reqStart = millis();
        uint32_t reqMax = allowKeep ? HTTPS_REQ_MAX_MS : HTTPS_UPLOAD_MAX_MS;
        while (!why) {
            // once the answer started, no client bytes: the next request must not end up in this
            // local connection (it waits in the TLS socket until the answer is complete)
            bool readClient = !answering;
            bool sslPending = readClient && mbedtls_ssl_get_bytes_avail(&ssl) > 0;
            fd_set rd; FD_ZERO(&rd); FD_SET(lfd, &rd);
            if (readClient) FD_SET(fd, &rd);
            struct timeval tv = { 0, sslPending ? 0 : 500000 };
            if (lwip_select((fd > lfd ? fd : lfd) + 1, &rd, nullptr, nullptr, &tv) < 0) { why = "select error"; break; }
            if (readClient && (sslPending || FD_ISSET(fd, &rd))) {   // rest of the request (body)
                int m = mbedtls_ssl_read(&ssl, buf, HTTPS_BUF_LEN);
                if (m == MBEDTLS_ERR_SSL_WANT_READ || m == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
                if (m <= 0) { why = m == 0 || m == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ? "closed by the client" : "TLS read error"; break; }
                if (!httpsSendAll(lfd, buf, m)) { why = "local HTTP server stopped reading"; break; }
                bytesIn += m; idle = millis();
            }
            if (FD_ISSET(lfd, &rd)) {                                  // the answer
                int m = lwip_recv(lfd, buf, HTTPS_BUF_LEN, 0);
                if (m <= 0) {                                          // web server closed: answer complete
                    if (!hdrDone && hlen && !httpsSslWriteAll(ssl, httpsHdr, hlen)) why = "client stopped reading (send timeout)";
                    if (!hdrDone) keep = false;
                    break;
                }
                answering = true; bytesOut += m; idle = millis();
                if (hdrDone) {
                    if (!httpsSslWriteAll(ssl, buf, m)) why = "client stopped reading (send timeout)";
                    continue;
                }
                size_t take = m; if (hlen + take > HTTPS_HDR_MAX) take = HTTPS_HDR_MAX - hlen;
                memcpy(httpsHdr + hlen, buf, take); hlen += take;
                const char* end = httpsFindCI((const char*)httpsHdr, hlen, "\r\n\r\n");
                if (end) {
                    size_t hl = end - (const char*)httpsHdr + 4;
                    String h = httpsRewriteHeader((const char*)httpsHdr, hl, allowKeep, &keep);
                    hdrDone = true;
                    if (!httpsSslWriteAll(ssl, (const uint8_t*)h.c_str(), h.length()) ||
                        !httpsSslWriteAll(ssl, httpsHdr + hl, hlen - hl) ||     // body bytes read with the header
                        !httpsSslWriteAll(ssl, buf + take, m - take))           // rest of this read
                        why = "client stopped reading (send timeout)";
                } else if (hlen >= HTTPS_HDR_MAX) {                    // header too long: unchanged, close after
                    hdrDone = true; keep = false;
                    if (!httpsSslWriteAll(ssl, httpsHdr, hlen) || !httpsSslWriteAll(ssl, buf + take, m - take))
                        why = "client stopped reading (send timeout)";
                }
            }
            if (millis() - idle > 30000) { why = "idle for 30 s"; break; }   // nothing moves: give up
            if (millis() - reqStart > reqMax) { why = "request took too long (total time limit)"; break; }   // trickling client
        }
        proxyPeerClear(0);
        lwip_close(lfd);
        if (why) return why;
        requests++; httpsStats.requests++;
        if (!keep) return "answer complete (no keep-alive)";
    }
}

static void httpsServe(int fd, uint32_t peer, const String& who, int ls)
{
    static uint32_t lastFailLog = 0;
    uint32_t tStart = millis(), bytesIn = 0, bytesOut = 0, requests = 0;
    const char* why = "";
    httpsTimeouts(fd, 10000);
    mbedtls_ssl_context& ssl = httpsSsl;
    int r = mbedtls_ssl_session_reset(&ssl);   // fresh state, buffers kept
    if (r) { httpsStats.failed++; LOGW("https", "%s: TLS reset failed (-0x%04X)", who.c_str(), -r); lwip_close(fd); return; }
    mbedtls_ssl_set_bio(&ssl, &fd, httpsBioSend, httpsBioRecv, nullptr);
    {
        uint32_t t0 = millis();
        while ((r = mbedtls_ssl_handshake(&ssl)) != 0) {
            if ((r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) || millis() - t0 > 10000) {
                httpsStats.failed++;
                // a browser that does not trust the certificate yet (alert) or that gives up (reset)
                // closes the handshake: normal the first time, debug only; anything else a warning
                if (r == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE || r == MBEDTLS_ERR_NET_CONN_RESET || r == MBEDTLS_ERR_NET_RECV_FAILED)
                    LOGD("https", "%s: handshake ended by the client (-0x%04X; e.g. certificate not trusted yet)", who.c_str(), -r);
                else if (millis() - lastFailLog > 60000) {
                    lastFailLog = millis();
                    LOGW("https", "%s: handshake failed (-0x%04X)%s", who.c_str(), -r, millis() - t0 > 10000 ? " - timeout" : "");
                }
                lwip_close(fd); return;
            }
        }
        httpsStats.lastHandshakeMs = millis() - t0;
        if (httpsStats.lastHandshakeMs > httpsStats.maxHandshakeMs) httpsStats.maxHandshakeMs = httpsStats.lastHandshakeMs;
        uint32_t fh = heap_caps_get_free_size(MALLOC_CAP_8BIT);   // lowest free heap with a session open
        if (!httpsStats.minFreeDuring || fh < httpsStats.minFreeDuring) httpsStats.minFreeDuring = fh;
        if (httpsStats.lastHandshakeMs > 3000)
            LOGW("https", "%s: slow TLS handshake (%lu ms)", who.c_str(), (unsigned long)httpsStats.lastHandshakeMs);
    }
    httpsStats.sessions++;
    httpsPeer = peer;
    strlcpy(httpsPeerText, who.c_str(), sizeof(httpsPeerText));
    why = httpsRelay(ssl, fd, ls, bytesIn, bytesOut, requests);
    httpsPeer = 0; httpsPeerText[0] = 0;
    mbedtls_ssl_close_notify(&ssl);
    LOGD("https", "%s: %s, handshake %lu ms, %lu request(s), %lu bytes in, %lu out, %lu ms - %s", who.c_str(), mbedtls_ssl_get_ciphersuite(&ssl),
         (unsigned long)httpsStats.lastHandshakeMs, (unsigned long)requests, (unsigned long)bytesIn, (unsigned long)bytesOut,
         (unsigned long)(millis() - tStart), why);
    lwip_close(fd);
}

// ── the task ────────────────────────────────────────────────────────────────
static void httpsTask(void*)
{
    int ls = -1;
    uint32_t lastCheck = 0, lastRefuseLog = 0;
    bool haveCert = false;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        if (!haveCert || httpsRegen || httpsCheck || millis() - lastCheck > 3600000UL) {   // hourly: hostname / IP / expiry
            bool regen = httpsRegen; httpsRegen = false; httpsCheck = false; lastCheck = millis();
            String before = httpsFingerprint();
            haveCert = httpsEnsureCert(regen);
            if (haveCert && (!httpsConfigured || httpsFingerprint() != before)) haveCert = httpsConfigure();
            if (!haveCert) { vTaskDelay(pdMS_TO_TICKS(60000)); continue; }
        }
        if (httpsReload) { httpsReload = false; if (ls >= 0 && !httpsEnabled) { lwip_close(ls); ls = -1; httpsListening = false; LOGI("https", "HTTPS stopped (setting)"); } }
        if (!httpsEnabled) { httpsRelease("HTTPS off"); vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        if (ls < 0) {
            // dual-stack: an IPv6 socket on "::" that takes IPv4 too (IPV6_V6ONLY off; IPv4
            // clients show up as ::ffff:a.b.c.d)
            ls = lwip_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
            int one = 1, zero = 0;
            lwip_setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            lwip_setsockopt(ls, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
            struct sockaddr_in6 a = {}; a.sin6_family = AF_INET6; a.sin6_port = htons(HTTPS_PORT); a.sin6_addr = in6addr_any;
            if (ls < 0 || lwip_bind(ls, (struct sockaddr*)&a, sizeof(a)) != 0 || lwip_listen(ls, 4) != 0) {
                if (ls >= 0) lwip_close(ls);
                ls = -1; httpsMsg = "port 443 not available"; LOGE("https", "cannot listen on port %d", HTTPS_PORT);
                vTaskDelay(pdMS_TO_TICKS(10000)); continue;
            }
            httpsListening = true;
            LOGI("https", "HTTPS on port %d (https://%s.local/), SHA-256 %s", HTTPS_PORT, httpsHostname().c_str(), httpsFingerprint().c_str());
        }
        // the GitHub update check / install has its own TLS connection: do not accept now, the
        // browser's connection waits in the listen queue and is served right after
        if (fwBusy) { httpsRelease("GitHub update needs the memory"); vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        // start-up: the GitHub check first (at most ~90 s), then the buffers; connections wait
        if (fwBootCheckPending) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
        if (!httpsSslReady && !httpsReserve()) {
            // could not get the buffers (fragmented heap): try again in 5 s; connections wait
            if (millis() - lastRefuseLog > 60000) {
                lastRefuseLog = millis();
                LOGW("https", "TLS buffers could not be reserved (%u free, largest block %u) - retrying",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            }
            httpsStats.refused++;
            vTaskDelay(pdMS_TO_TICKS(5000)); continue;
        }
        fd_set rd; FD_ZERO(&rd); FD_SET(ls, &rd);
        struct timeval tv = { 1, 0 };
        if (lwip_select(ls + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
        struct sockaddr_storage peer = {}; socklen_t pl = sizeof(peer);
        int fd = lwip_accept(ls, (struct sockaddr*)&peer, &pl);
        uint32_t peer4 = 0; String who;
        if (peer.ss_family == AF_INET) peer4 = ((struct sockaddr_in*)&peer)->sin_addr.s_addr;
        else if (peer.ss_family == AF_INET6) {
            const struct sockaddr_in6* p6 = (const struct sockaddr_in6*)&peer;
            const uint8_t* b = p6->sin6_addr.s6_addr;
            static const uint8_t mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
            if (memcmp(b, mapped, 12) == 0) memcpy(&peer4, b + 12, 4);   // IPv4 client
            else { char t[48]; inet6_ntoa_r(p6->sin6_addr, t, sizeof(t)); who = t; }
        }
        if (peer4) who = IPAddress(peer4).toString();
        if (fd < 0) continue;
        httpsSessionActive = true;
        httpsServe(fd, peer4, who, ls);
        httpsSessionActive = false;
    }
}

void httpsSetup()
{
    Preferences p; p.begin("https", true);
    httpsEnabled = p.getBool("enabled", true);
    httpsRedirect = p.getBool("redirect", false);   // default off since 20/11/2026: HTTPS costs ~34 KB per session
    httpsAlt = p.getString("alt", "");
    p.end();
    httpsTlsOn = httpsEnabled;
    // reserve the TLS buffers now, while the heap is still in one piece (the certificate is
    // added to the configuration once WiFi is up; the buffers do not depend on it)
    if (httpsEnabled) {
        mbedtls_ssl_config_init(&httpsConf);
        mbedtls_ssl_config_defaults(&httpsConf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_rng(&httpsConf, httpsRng, nullptr);
        if (fwBootCheckPending) LOGI("https", "TLS buffers are reserved after the GitHub update check at start-up");
        else if (!httpsReserve()) LOGW("https", "TLS buffers could not be reserved at start-up");
    }
    // 9 KB stack: the ECDSA handshake used 6.7 KB at most (measured, esp32_task_stack_free_bytes)
    // Priority 0 = the idle task's: a full TLS handshake is 1.5 - 7 s of pure computation, and
    // a browser opens several connections at once. At priority 1 that kept IDLE0 from running
    // for more than 10 s and the task watchdog (it watches IDLE0) aborted the controller
    // (core dump 19/11/2026: abort() in IDLE0). Sharing priority 0 FreeRTOS gives the idle
    // task its turn every tick; the sensor task and WiFi (higher priority) still go first.
    xTaskCreatePinnedToCore(httpsTask, "https", 9216, nullptr, tskIDLE_PRIORITY, &httpsTaskHandle, 0);
}

// HTTP -> HTTPS for the web app pages only: the API (/api/..., /metrics, firmware upload)
// stays on HTTP for Home Assistant, Prometheus, scripts and PlatformIO, which do not follow a
// redirect to a self-signed certificate. 307 (temporary): browsers do not remember it, so
// switching the redirect or HTTPS off works at once. Not while HTTPS is not ready, and never
// for the requests HTTPS itself passes on (127.0.0.1).
static bool httpsRedirectNow()
{
    return httpsRedirect && httpsEnabled && httpsListening && httpsCrtLen &&
           !(proxyPeerOfRequest() && proxyPeerOfRequest()->tls);   // not for requests HTTPS itself passes on
}
static std::function<void()> httpsOr(std::function<void()> handler)
{
    return [handler]() {
        if (!httpsRedirectNow()) { handler(); return; }
        String host = wm.server->hostHeader();
        if (host.startsWith("[")) { int e = host.indexOf(']'); if (e > 0) host = host.substring(0, e + 1); }   // IPv6 literal
        else { int colon = host.indexOf(':'); if (colon >= 0) host = host.substring(0, colon); }
        if (!host.length()) host = WiFi.localIP().toString();
        wm.server->sendHeader("Location", "https://" + host + wm.server->uri());
        wm.server->sendHeader("Cache-Control", "no-store");
        wm.server->send(307, "text/plain; charset=utf-8", "HTTPS: https://" + host + wm.server->uri());
    };
}

static String httpsPem()
{
    // PEM of the certificate (public), for installing it as trusted
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String s = "-----BEGIN CERTIFICATE-----\n";
    int col = 0;
    for (size_t i = 0; i < httpsCrtLen; i += 3) {
        uint32_t v = httpsCrt[i] << 16 | (i + 1 < httpsCrtLen ? httpsCrt[i + 1] << 8 : 0) | (i + 2 < httpsCrtLen ? httpsCrt[i + 2] : 0);
        char q[5] = { b64[v >> 18 & 63], b64[v >> 12 & 63], i + 1 < httpsCrtLen ? b64[v >> 6 & 63] : '=', i + 2 < httpsCrtLen ? b64[v & 63] : '=', 0 };
        s += q; col += 4;
        if (col == 64) { s += "\n"; col = 0; }
    }
    if (col) s += "\n";
    return s + "-----END CERTIFICATE-----\n";
}

// GET /api/https: state and certificate; ?cert=pem downloads the certificate
// Settings, shared by /api/https and the telnet menu (who = client address or telnet session)
static void httpsSetEnabled(bool on, const char* who)
{
    httpsEnabled = on; httpsTlsOn = on;
    Preferences p; p.begin("https", false); p.putBool("enabled", on); p.end();
    httpsReload = true;
    LOGI("https", "HTTPS %s by %s", on ? "enabled" : "disabled", who);
}
static void httpsSetRedirect(bool on, const char* who)
{
    httpsRedirect = on;
    Preferences p; p.begin("https", false); p.putBool("redirect", on); p.end();
    LOGI("https", "HTTP -> HTTPS redirect %s by %s", on ? "on" : "off", who);
}
static void httpsRequestNewCert(const char* who) { httpsRegen = true; LOGI("https", "new key and certificate requested by %s", who); }
// Extra subjectAltNames: comma-separated host names / IPv4 addresses, "" removes them all.
// Returns an error text, "" = done.
static String httpsSetAltNames(const String& in, const char* who)
{
    String list; int count = 0, from = 0;
    while (from <= (int)in.length()) {
        int comma = in.indexOf(',', from); if (comma < 0) comma = in.length();
        String a = in.substring(from, comma); a.trim(); a.toLowerCase(); from = comma + 1;
        if (!a.length()) continue;
        if (!httpsValidAlt(a)) return "not a host name or IPv4 address: " + a;
        if (("," + list + ",").indexOf("," + a + ",") >= 0) continue;   // duplicate
        if (++count > HTTPS_MAX_ALT) return "at most 8 extra names";
        list += (list.length() ? "," : "") + a;
    }
    if (list != httpsAlt) {
        httpsAlt = list;
        Preferences p; p.begin("https", false); p.putString("alt", httpsAlt); p.end();
        httpsCheck = true;
        LOGI("https", "extra alt names set to '%s' by %s - certificate is re-issued", httpsAlt.c_str(), who);
    }
    return "";
}

// POST /api/https: enabled=on|off, regenerate=true (new key and certificate)
void handleRouteHttps()
{
    if (wm.server->method() == HTTP_GET && wm.server->arg("cert") == "pem") {
        if (!httpsCrtLen) { wm.server->send(404, "application/json; charset=utf-8", "{\"result\":\"no certificate yet\"}"); return; }
        wm.server->sendHeader("Content-Disposition", "attachment; filename=\"" + httpsHostname() + ".crt\"");
        wm.server->send(200, "application/x-x509-ca-cert", httpsPem());
        return;
    }
    DynamicJsonDocument json(2048);
    if (wm.server->method() == HTTP_POST) {
        int on = wm.server->hasArg("enabled") ? pc::parseOnOff(wm.server->arg("enabled").c_str()) : -1;
        int redir = wm.server->hasArg("redirect") ? pc::parseOnOff(wm.server->arg("redirect").c_str()) : -1;
        bool regen = pc::parseOnOff(wm.server->arg("regenerate").c_str()) == 1;
        bool alt = wm.server->hasArg("altNames");
        if (on < 0 && redir < 0 && !regen && !alt) { wm.server->send(400, "application/json; charset=utf-8", "{\"result\":\"failed - enabled, redirect (on / off), altNames or regenerate=true\"}"); return; }
        String who = clientIp();
        if (alt) {
            String err = httpsSetAltNames(wm.server->arg("altNames"), who.c_str());
            if (err.length()) { wm.server->send(400, "application/json; charset=utf-8", "{\"result\":\"failed - " + err + "\"}"); return; }
        }
        if (redir >= 0) httpsSetRedirect(redir == 1, who.c_str());
        if (on >= 0) httpsSetEnabled(on == 1, who.c_str());
        if (regen) httpsRequestNewCert(who.c_str());
        json["result"] = regen ? "success - a new key and certificate are being made (a few seconds); the browser will warn again" : "success";
    } else if (wm.server->method() != HTTP_GET) { wm.server->send(405); return; }
    json["enabled"] = httpsEnabled;
    json["redirect"] = httpsRedirect;
    JsonArray alts = json.createNestedArray("altNames");
    for (int from = 0; from < (int)httpsAlt.length();) {
        int comma = httpsAlt.indexOf(',', from); if (comma < 0) comma = httpsAlt.length();
        alts.add(httpsAlt.substring(from, comma)); from = comma + 1;
    }
    json["listening"] = httpsListening && httpsEnabled;
    json["port"] = HTTPS_PORT;
    json["url"] = "https://" + httpsHostname() + ".local/";
    if (httpsCrtLen) {
        JsonObject c = json.createNestedObject("certificate");
        c["subject"] = "CN=" + httpsHostname();
        c["key"] = "ECDSA P-256";
        c["sha256"] = httpsFingerprint();
        c["validFrom"] = httpsIso(httpsFrom);
        c["validUntil"] = httpsIso(httpsUntil);
        // what the certificate was made for: "hostname|ip|alt1,alt2"
        JsonArray san = c.createNestedArray("san");
        int b1 = httpsSan.indexOf('|'), b2 = httpsSan.indexOf('|', b1 + 1);
        String host = httpsSan.substring(0, b1), certIp = httpsSan.substring(b1 + 1, b2 < 0 ? httpsSan.length() : b2);
        san.add(host); san.add(host + ".local"); san.add(certIp);
        if (b2 >= 0) for (int from = b2 + 1; from < (int)httpsSan.length();) {
            int comma = httpsSan.indexOf(',', from); if (comma < 0) comma = httpsSan.length();
            san.add(httpsSan.substring(from, comma)); from = comma + 1;
        }
    } else json["certificate"] = nullptr;
    JsonObject st = json.createNestedObject("stats");
    st["sessions"] = httpsStats.sessions;
    st["requests"] = httpsStats.requests;   // > sessions: keep-alive reused the TLS session
    st["failedHandshakes"] = httpsStats.failed;
    st["refusedLowMemory"] = httpsStats.refused;
    st["lastHandshakeMs"] = httpsStats.lastHandshakeMs;
    st["maxHandshakeMs"] = httpsStats.maxHandshakeMs;
    st["minFreeHeapDuringSession"] = httpsStats.minFreeDuring;
    st["freeHeap"] = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    st["largestBlock"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (httpsTaskHandle) st["stackFreeMin"] = uxTaskGetStackHighWaterMark(httpsTaskHandle);   // bytes never used of the 9 KB
    if (httpsMsg.length()) json["problem"] = httpsMsg; else json["problem"] = nullptr;
    String out; serializeJson(json, out);
    wm.server->send(200, "application/json; charset=utf-8", out);
}
