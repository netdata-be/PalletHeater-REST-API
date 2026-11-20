
#pragma once
// HTTP over IPv6 on port 80.
//
// The Arduino WebServer (WiFiServer) only listens on IPv4 and keeps its socket private. This
// task listens on IPv6 port 80 (IPV6_V6ONLY, next to the IPv4 socket of the web server on the
// same port) and passes every connection on to the web server over 127.0.0.1:80, the same way
// as the HTTPS relay (https.h) but without TLS: all routes, the API key and the large answers
// work unchanged. Clients that look up openpelletbridge.local get both addresses (mDNS A and
// AAAA) and may try IPv6 first; before this they got a refusal and fell back to IPv4.
//
// One connection at a time (the web server answers one request at a time anyway); others
// wait in the listen queue. The log shows the real IPv6 client (auth.h, proxyPeers).
#include <lwip/sockets.h>

#define HTTP6_BUF_LEN 1460
static bool http6Listening = false;
static uint32_t http6Connections = 0;

static void http6Serve(int fd, const char* who)
{
    static uint8_t buf[HTTP6_BUF_LEN];
    httpsTimeouts(fd, 10000);
    int lfd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lfd < 0) { lwip_close(fd); return; }
    struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(80); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    httpsTimeouts(lfd, 10000);
    if (lwip_connect(lfd, (struct sockaddr*)&a, sizeof(a)) != 0) {
        LOGW("http", "IPv6 relay: local HTTP server not reachable");
        lwip_close(lfd); lwip_close(fd); return;
    }
    proxyPeerSet(1, lfd, false, who);
    http6Connections++;
    uint32_t idle = millis(), start = millis(), in = 0;
    for (;;) {
        fd_set rd; FD_ZERO(&rd); FD_SET(fd, &rd); FD_SET(lfd, &rd);
        struct timeval tv = { 0, 500000 };
        if (lwip_select((fd > lfd ? fd : lfd) + 1, &rd, nullptr, nullptr, &tv) < 0) break;
        if (FD_ISSET(fd, &rd)) {                 // client -> web server
            int n = lwip_recv(fd, buf, sizeof(buf), 0);
            if (n <= 0 || !httpsSendAll(lfd, buf, n)) break;
            idle = millis(); in += n;
        }
        if (FD_ISSET(lfd, &rd)) {                // answer -> client
            int n = lwip_recv(lfd, buf, sizeof(buf), 0);
            if (n <= 0 || !httpsSendAll(fd, buf, n)) break;   // the web server closed: answer complete
            idle = millis();
        }
        if (millis() - idle > 30000) break;      // nothing moves: give up
        // total time limit (a client that trickles keeps "idle" low); more for an upload
        if (millis() - start > (in > 65536 ? HTTPS_UPLOAD_MAX_MS : HTTPS_REQ_MAX_MS)) {
            LOGW("http", "IPv6 %s: connection took too long - closed", who);
            break;
        }
    }
    proxyPeerClear(1);
    lwip_close(lfd);
    lwip_close(fd);
}

static void http6Task(void*)
{
    int ls = -1;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        if (ls < 0) {
            ls = lwip_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
            int one = 1;
            lwip_setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            lwip_setsockopt(ls, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));   // IPv4 stays with the web server
            struct sockaddr_in6 a = {}; a.sin6_family = AF_INET6; a.sin6_port = htons(80); a.sin6_addr = in6addr_any;
            if (ls < 0 || lwip_bind(ls, (struct sockaddr*)&a, sizeof(a)) != 0 || lwip_listen(ls, 4) != 0) {
                if (ls >= 0) lwip_close(ls);
                ls = -1;
                LOGE("http", "IPv6: cannot listen on port 80");
                vTaskDelay(pdMS_TO_TICKS(30000)); continue;
            }
            http6Listening = true;
            LOGI("http", "web server also on IPv6 port 80");
        }
        fd_set rd; FD_ZERO(&rd); FD_SET(ls, &rd);
        struct timeval tv = { 1, 0 };
        if (lwip_select(ls + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
        struct sockaddr_in6 peer = {}; socklen_t pl = sizeof(peer);
        int fd = lwip_accept(ls, (struct sockaddr*)&peer, &pl);
        if (fd < 0) continue;
        char who[48]; inet6_ntoa_r(peer.sin6_addr, who, sizeof(who));
        http6Serve(fd, who);
    }
}

void http6Setup()
{
    // 4 KB stack: 2.3 KB used (measured); a log line in the error paths needs ~0.5 KB more
    xTaskCreatePinnedToCore(http6Task, "http6", 4096, nullptr, 1, nullptr, 0);
}
