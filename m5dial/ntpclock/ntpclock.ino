#include <M5Dial.h>
#include <WiFi.h>

#if __has_include(<esp_sntp.h>)
#include <esp_sntp.h>
#define SNTP_ENABLED 1
#elif __has_include(<sntp.h>)
#include <sntp.h>
#define SNTP_ENABLED 1
#endif

#ifndef SNTP_ENABLED
#define SNTP_ENABLED 0
#endif

const char* ssid = "tenox";
const char* password = "";
const char* ntpServer1 = "time.google.com";
const char* ntpServer2 = "time.cloudflare.com";
const char* ntpServer3 = "pool.ntp.org";
const char* ntpTimezone = "PST8PDT,M3.2.0,M11.1.0";

const unsigned long STATUS_CHECK_INTERVAL = 30000;

volatile bool wifiOk = false;
volatile bool ntpOk = false;

const int clockRadius = 118;
M5Canvas canvas(&M5Dial.Display);

void drawHand(int cx, int cy, float angle, int length, int thickness, uint16_t color) {
    float rad = (angle - 90) * DEG_TO_RAD;
    int x = cx + length * cos(rad);
    int y = cy + length * sin(rad);
    canvas.drawWedgeLine(cx, cy, x, y, thickness, thickness, color);
}

void drawTriangleHand(int cx, int cy, float angle, int length, int baseWidth, uint16_t color) {
    float rad = (angle - 90) * DEG_TO_RAD;
    int tipX = cx + length * cos(rad);
    int tipY = cy + length * sin(rad);
    canvas.drawWedgeLine(cx, cy, tipX, tipY, baseWidth, 1, color);
}

void drawDashedHand(int cx, int cy, float angle, int length, int thickness, uint16_t color) {
    float rad = (angle - 90) * DEG_TO_RAD;
    float dx = cos(rad);
    float dy = sin(rad);
    for (int i = 0; i < length; i += 14) {
        int x1 = cx + i * dx;
        int y1 = cy + i * dy;
        int x2 = cx + min(i + 6, length) * dx;
        int y2 = cy + min(i + 6, length) * dy;
        canvas.drawWedgeLine(x1, y1, x2, y2, thickness, thickness, color);
    }
}

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED)
        return true;
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
        delay(500);
    return WiFi.status() == WL_CONNECTED;
}

void waitForNTP() {
#if SNTP_ENABLED
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        Serial.print(".");
        delay(1000);
    }
#else
    delay(1600);
    struct tm timeInfo;
    while (!getLocalTime(&timeInfo, 1000)) {
        Serial.print(".");
    }
#endif
}

void setRtcFromNtp() {
    time_t t = time(nullptr) + 1;
    while (t > time(nullptr));
    M5Dial.Rtc.setDateTime(localtime(&t));
}

void showStatus(const char* msg) {
    int cx = M5Dial.Display.width() / 2;
    int cy = M5Dial.Display.height() / 2;
    M5Dial.Display.clear();
    M5Dial.Display.drawString(msg, cx, cy);
}

void statusTask(void* param) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_CHECK_INTERVAL));

        wifiOk = (WiFi.status() == WL_CONNECTED);
        if (!wifiOk) {
            Serial.println("WiFi lost, reconnecting...");
            wifiOk = connectWiFi();
#if SNTP_ENABLED
            if (wifiOk) {
                sntp_restart();
                waitForNTP();
                setRtcFromNtp();
            }
#endif
        }
    }
}

void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);

    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n=== ACLOCK v4 ===");

    M5Dial.Display.setTextColor(YELLOW);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextFont(&fonts::Orbitron_Light_32);
    M5Dial.Display.setTextSize(0.8);

    int cx = M5Dial.Display.width() / 2;
    int cy = M5Dial.Display.height() / 2;

    if (!M5Dial.Rtc.isEnabled()) {
        Serial.println("RTC not found!");
        M5Dial.Display.clear();
        M5Dial.Display.drawString("RTC FAIL", cx, cy);
        for (;;) delay(500);
    }
    Serial.println("RTC found.");

    M5Dial.Display.setRotation(2);
    canvas.createSprite(M5Dial.Display.width(), M5Dial.Display.height());

    configTzTime(ntpTimezone, ntpServer1, ntpServer2, ntpServer3);
    Serial.println("configTzTime done.");

    while (!wifiOk) {
        showStatus("WiFi...");
        Serial.print("WiFi connecting...");
        wifiOk = connectWiFi();
        Serial.println(wifiOk ? " OK" : " retry");
        if (!wifiOk)
            delay(2000);
    }
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());

    showStatus("NTP...");
    Serial.print("NTP syncing");
    waitForNTP();
    Serial.println(" OK");
    ntpOk = true;

    setRtcFromNtp();
    Serial.println("RTC set from NTP.");

    xTaskCreate(statusTask, "status", 4096, NULL, 1, NULL);
    Serial.println("Status task started.");
}

void loop() {
    M5Dial.update();

    int cx = canvas.width() / 2;
    int cy = canvas.height() / 2;

    auto dt = M5Dial.Rtc.getDateTime();
    int hours = dt.time.hours % 12;
    int minutes = dt.time.minutes;
    int seconds = dt.time.seconds;

    float hourAngle = hours * 30 + minutes * 0.5;
    float minAngle = minutes * 6;
    float secAngle = seconds * 6;

    canvas.fillScreen(NAVY);

    for (int i = 0; i < 60; i++) {
        if (i % 5 != 0) {
            float angle = (i * 6 - 90) * DEG_TO_RAD;
            int dotX = cx + 117 * cos(angle);
            int dotY = cy + 117 * sin(angle);
            canvas.fillCircle(dotX, dotY, 2, 0x6B4D);
        }
    }

    for (int i = 0; i < 12; i++) {
        float angle = (i * 30 - 90) * DEG_TO_RAD;
        float perpAngle = angle + PI / 2;
        int tipX = cx + 112 * cos(angle);
        int tipY = cy + 112 * sin(angle);
        int baseX1 = cx + 118 * cos(angle) + 3 * cos(perpAngle);
        int baseY1 = cy + 118 * sin(angle) + 3 * sin(perpAngle);
        int baseX2 = cx + 118 * cos(angle) - 3 * cos(perpAngle);
        int baseY2 = cy + 118 * sin(angle) - 3 * sin(perpAngle);
        canvas.fillTriangle(tipX, tipY, baseX1, baseY1, baseX2, baseY2, YELLOW);
    }

    drawTriangleHand(cx, cy, hourAngle, 85, 12, RED);
    drawHand(cx, cy, minAngle, 100, 4, GREEN);
    drawDashedHand(cx, cy, secAngle, 118, 2, ORANGE);
    canvas.fillCircle(cx, cy, 6, YELLOW);

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", dt.time.hours, dt.time.minutes);
    canvas.setTextDatum(middle_center);
    canvas.setTextFont(&fonts::Orbitron_Light_32);
    canvas.setTextSize(1);
    canvas.setTextColor(YELLOW);
    canvas.drawString(timeBuf, cx, cy + 45);

    canvas.setTextFont(&fonts::FreeMonoBold12pt7b);
    canvas.setTextColor(wifiOk ? GREEN : RED);
    canvas.drawString("WiFi", cx - 35, cy - 35);
    canvas.setTextColor(ntpOk ? GREEN : RED);
    canvas.drawString("NTP", cx + 35, cy - 35);

    canvas.pushSprite(0, 0);
    delay(100);
}
