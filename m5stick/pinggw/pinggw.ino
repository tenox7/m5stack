#include <M5Unified.h>
#include <WiFi.h>
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/err.h"
#include "lwip/icmp.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

const char* ssid = "tenox";
const char* password = "dupadupa";

const int SCREEN_W = 240;
const int SCREEN_H = 135;
const int CHART_X = 0;
const int CHART_Y = 20;
const int CHART_W = SCREEN_W;
const int CHART_H = SCREEN_H - 40;
const int MAX_SAMPLES = CHART_W;
const int PING_INTERVAL = 1000;
const int MAX_PING_MS = 200;

int pingGW[MAX_SAMPLES];
int pingDNS[MAX_SAMPLES];
int sampleIndex = 0;
int sampleCount = 0;
IPAddress gateway;
IPAddress dns(1, 1, 1, 1);
bool wifiOk = false;

M5Canvas canvas(&M5.Display);

int pingHost(IPAddress ip, int timeout_ms = 1000) {
    int sock = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (sock < 0) return -1;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;

    struct icmp_echo_hdr echo;
    memset(&echo, 0, sizeof(echo));
    echo.type = ICMP_ECHO;
    echo.code = 0;
    echo.id = htons(random(0xFFFF));
    echo.seqno = htons(1);
    echo.chksum = inet_chksum(&echo, sizeof(echo));

    unsigned long start = millis();
    int sent = sendto(sock, &echo, sizeof(echo), 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent <= 0) {
        close(sock);
        return -1;
    }

    char buf[64];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    int recv_len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
    close(sock);

    if (recv_len <= 0) return -1;

    return millis() - start;
}

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED)
        return true;
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    for (int i = 40; i && WiFi.status() != WL_CONNECTED; --i)
        delay(250);
    return WiFi.status() == WL_CONNECTED;
}

int mapPingToY(int pingMs) {
    if (pingMs < 0) return -1;
    int clamped = min(pingMs, MAX_PING_MS);
    return CHART_Y + CHART_H - 1 - (clamped * (CHART_H - 1) / MAX_PING_MS);
}

void drawLine(int* data, uint16_t colorOk, uint16_t colorSlow) {
    if (sampleCount < 2) return;

    int drawCount = min(sampleCount, MAX_SAMPLES);
    int startIdx = (sampleIndex - drawCount + MAX_SAMPLES) % MAX_SAMPLES;

    int prevX = -1, prevY = -1;
    for (int i = 0; i < drawCount; i++) {
        int idx = (startIdx + i) % MAX_SAMPLES;
        int ping = data[idx];
        int x = CHART_W - drawCount + i;
        int y = mapPingToY(ping);

        uint16_t color = colorOk;
        if (ping < 0) {
            color = TFT_RED;
            y = CHART_Y + CHART_H - 1;
        } else if (ping > 50) {
            color = colorSlow;
        }

        if (prevX >= 0 && prevY >= 0)
            canvas.drawLine(prevX, prevY, x, y, color);

        prevX = x;
        prevY = y;
    }
}

void drawChart() {
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextSize(1);

    int lastGW = sampleCount > 0 ? pingGW[(sampleIndex - 1 + MAX_SAMPLES) % MAX_SAMPLES] : -1;
    int lastDNS = sampleCount > 0 ? pingDNS[(sampleIndex - 1 + MAX_SAMPLES) % MAX_SAMPLES] : -1;

    canvas.setTextColor(TFT_GREEN);
    canvas.setCursor(2, 2);
    canvas.printf("GW:%d", lastGW < 0 ? 0 : lastGW);

    canvas.setTextColor(TFT_CYAN);
    canvas.setCursor(70, 2);
    canvas.printf("DNS:%d", lastDNS < 0 ? 0 : lastDNS);

    int rssi = WiFi.RSSI();
    canvas.setTextColor(rssi > -50 ? TFT_GREEN : rssi > -70 ? TFT_YELLOW : TFT_RED);
    canvas.setCursor(SCREEN_W - 55, 2);
    canvas.printf("%ddBm", rssi);

    canvas.drawFastHLine(CHART_X, CHART_Y, CHART_W, TFT_DARKGREY);
    canvas.drawFastHLine(CHART_X, CHART_Y + CHART_H - 1, CHART_W, TFT_DARKGREY);
    canvas.drawFastHLine(CHART_X, mapPingToY(100), CHART_W, TFT_DARKGREY);

    drawLine(pingGW, TFT_GREEN, TFT_YELLOW);
    drawLine(pingDNS, TFT_CYAN, TFT_ORANGE);

    canvas.setTextColor(TFT_DARKGREY);
    canvas.setCursor(2, SCREEN_H - 12);
    canvas.printf("%d%%", M5.Power.getBatteryLevel());

    canvas.pushSprite(0, 0);
}

void showStatus(const char* msg) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, SCREEN_H / 2 - 10);
    M5.Display.print(msg);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(64);
    M5.Display.fillScreen(TFT_BLACK);

    canvas.createSprite(SCREEN_W, SCREEN_H);

    for (int i = 0; i < MAX_SAMPLES; i++) {
        pingGW[i] = -1;
        pingDNS[i] = -1;
    }

    Serial.begin(115200);
    Serial.println("PingGW starting...");

    showStatus("Connecting...");

    while (!wifiOk) {
        wifiOk = connectWiFi();
        if (!wifiOk) {
            showStatus("WiFi retry...");
            delay(2000);
        }
    }

    gateway = WiFi.gatewayIP();
    Serial.printf("Connected! GW: %s\n", gateway.toString().c_str());

    showStatus("Connected!");
    delay(500);
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        int br = M5.Display.getBrightness();
        br = (br >= 200) ? 32 : br + 40;
        M5.Display.setBrightness(br);
    }

    if (WiFi.status() != WL_CONNECTED) {
        wifiOk = false;
        showStatus("Reconnecting...");
        wifiOk = connectWiFi();
        if (wifiOk)
            gateway = WiFi.gatewayIP();
        return;
    }

    int gwTime = pingHost(gateway, 500);
    int dnsTime = pingHost(dns, 500);

    pingGW[sampleIndex] = gwTime;
    pingDNS[sampleIndex] = dnsTime;
    sampleIndex = (sampleIndex + 1) % MAX_SAMPLES;
    if (sampleCount < MAX_SAMPLES) sampleCount++;

    Serial.printf("GW:%dms DNS:%dms RSSI:%ddBm\n", gwTime, dnsTime, WiFi.RSSI());

    drawChart();

    delay(PING_INTERVAL);
}
