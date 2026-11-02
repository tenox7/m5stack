#include <M5NanoC6.h>
#include <Wire.h>
#include <M5UNIT_DIGI_CLOCK.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <esp_timer.h>

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

// WiFi name and password go in secrets.h next to this file (git ignores it):
//   #define WIFI_SSID     "MyNetwork"
//   #define WIFI_PASSWORD "MyPassword"
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#define SDA 2
#define SCL 1
#define ADD 0x30

#define LED_PIN    20
#define ENABLE_PIN 19
#define NUM_LEDS   1

#define BRIGHTNESS_DAY   8
#define BRIGHTNESS_NIGHT 1
#define NIGHT_START_HOUR 19
#define NIGHT_END_HOUR   7

// Start-up stages and error codes share numbers. While starting, a progress bar
// fills the edge of the display clockwise from the top left: 2 segments per stage,
// finished stages solid, the current one blinking. Full edge = done.
//   top left = 1, top right = 2, right side = 3, bottom right = 4,
//   bottom left = 5, left side = 6
// If a stage fails, the display shows "Err" + the stage number. While running,
// an error shows for 2 seconds out of every 10, and the time the rest.
#define ERR_NONE        0
#define ERR_NO_SSID     1  // stage 1: find the WiFi network     Err1: network not found
#define ERR_BAD_PASS    2  // stage 2: log in to WiFi            Err2: password wrong or missing
#define ERR_NO_IP       3  // stage 3: get an IP address (DHCP)  Err3: no IP address
#define ERR_NO_INTERNET 4  // stage 4: reach the internet        Err4: no internet
#define ERR_DNS         5  // stage 5: look up a name (DNS)      Err5: DNS error
#define ERR_NO_NTP      6  // stage 6: get the time (NTP)        Err6: no NTP time

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* ntpServer1 = "time.google.com";
const char* ntpServer2 = "time.cloudflare.com";
const char* ntpServer3 = "pool.ntp.org";
const char* ntpTimezone = "PST8PDT,M3.2.0,M11.1.0";

const unsigned long STATUS_CHECK_INTERVAL = 30000;
const uint32_t NTP_SYNC_INTERVAL_MS = 60UL * 60 * 1000;  // ask NTP every hour (core default is 3 hours)
const uint32_t NTP_STALE_SEC = 2UL * 60 * 60;            // no good sync for 2 hours = NTP is failing
const uint32_t MIN_STAGE_MS = 500;                       // each start-up stage stays visible this long
const uint32_t BLINK_MS = 250;                           // current stage: on 250 ms, off 250 ms
const uint32_t DONE_SHOW_MS = 600;                       // full progress bar shows this long at the end
const uint8_t STAGE_DONE = 7;                            // bootStage value for "all done"

volatile bool wifiOk = false;
volatile bool ntpOk = false;
volatile uint32_t ntpSyncCount = 0;    // good NTP replies since boot
volatile uint32_t lastNtpSyncSec = 0;  // uptime in seconds at the last good NTP reply
volatile uint8_t errorCode = ERR_NONE;  // shown on the display, 0 = no problem
volatile uint8_t lastWiFiReason = 0;    // last WiFi failure reason, 0 = none yet
volatile bool wifiLinked = false;       // joined the WiFi network (may have no IP yet)

// Start-up progress, drawn by progressTask until the first NTP sync
volatile bool booting = false;
volatile bool progressDone = false;
volatile uint8_t bootStage = 0;  // 1-6 = stage being worked on, STAGE_DONE = all done
volatile uint8_t bootError = 0;  // not 0: show "ErrN" instead
unsigned long stageSince = 0;

M5UNIT_DIGI_CLOCK Digiclock;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// 64-bit timer, so it does not wrap after 49 days like millis()
uint32_t uptimeSec() {
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

#if SNTP_ENABLED
// Called by the network task after every successful NTP sync. Keep it short.
void onNtpSync(struct timeval* tv) {
    lastNtpSyncSec = uptimeSec();
    ntpSyncCount = ntpSyncCount + 1;
}
#endif

// RGB LED:  blinking blue   = starting (the display shows the stage)
//           green           = WiFi OK and NTP synced in the last 2 hours
//           blinking yellow = WiFi OK but no NTP sync for 2+ hours (time may drift)
//           red             = no WiFi
// Blue LED: on = WiFi connected
void setStatusLED(bool wifi, bool ntp) {
    analogWrite(M5NANO_C6_BLUE_LED_PIN, wifi ? 50 : 0);

    if (!wifi) {
        strip.setPixelColor(0, strip.Color(255, 0, 0));
    } else if (!ntp) {
        bool on = (millis() / 500) % 2;
        strip.setPixelColor(0, on ? strip.Color(255, 160, 0) : 0);
    } else {
        strip.setPixelColor(0, strip.Color(0, 255, 0));
    }
    strip.show();
}

// Shows a short text on the clock display.
// Use only M5Stack's Digi-Clock character table: A-Z and 0-9, letter case as
// in the table. W, I, M, S, X, Z look odd. ('-' is not in the table, but
// M5Stack's own string examples use it, e.g. "-3.14".)
void showText(const char* text) {
    char buf[10];
    snprintf(buf, sizeof(buf), "%s", text);
    Digiclock.setString(buf);
}

const char* stageName(uint8_t n) {
    switch (n) {
        case 1:  return "find WiFi network";
        case 2:  return "log in to WiFi";
        case 3:  return "get IP address";
        case 4:  return "reach internet";
        case 5:  return "DNS lookup";
        case 6:  return "get NTP time";
        default: return "?";
    }
}

const char* errorName(uint8_t code) {
    switch (code) {
        case ERR_NO_SSID:     return "WiFi network not found";
        case ERR_BAD_PASS:    return "WiFi password wrong or missing";
        case ERR_NO_IP:       return "no IP address";
        case ERR_NO_INTERNET: return "no internet";
        case ERR_DNS:         return "DNS error";
        case ERR_NO_NTP:      return "no NTP time";
        default:              return "OK";
    }
}

// Sets the code for the display and logs every change
void setError(uint8_t code) {
    if (code != errorCode) {
        if (code)
            Serial.printf("Status: Err%u (%s)\n", code, errorName(code));
        else
            Serial.println("Status: OK");
    }
    errorCode = code;
}

// Segment bits for raw writes. M5Stack's docs don't give the order; this is the
// usual TM1637 one (checked by eye on the clock): a top, b top right,
// c bottom right, d bottom, e bottom left, f top left (g middle = 0x40, dot = 0x80).
#define SEG_A 0x01
#define SEG_B 0x02
#define SEG_C 0x04
#define SEG_D 0x08
#define SEG_E 0x10
#define SEG_F 0x20

// The edge of the display, clockwise from the top left. The progress bar fills
// it in this order, 2 segments per stage (12 segments = 6 stages).
const struct {
    uint8_t digit, seg;
} TRACK[] = {
    {0, SEG_A}, {1, SEG_A}, {2, SEG_A}, {3, SEG_A},  // top, left to right
    {3, SEG_B}, {3, SEG_C},                          // right side, down
    {3, SEG_D}, {2, SEG_D}, {1, SEG_D}, {0, SEG_D},  // bottom, right to left
    {0, SEG_E}, {0, SEG_F},                          // left side, up
};
const uint8_t TRACK_LEN = sizeof(TRACK) / sizeof(TRACK[0]);

uint32_t progressPicture = 0xFFFFFFFF;  // what drawProgress last wrote, 0xFFFFFFFF = unknown

// Draws the progress bar for `stage`: earlier stages solid, this stage's two
// segments only when blinkOn. STAGE_DONE draws the full edge.
// Writes to the display only when the picture changes.
void drawProgress(uint8_t stage, bool blinkOn) {
    uint8_t lit = (stage - 1) * 2 + (blinkOn ? 2 : 0);
    if (lit > TRACK_LEN)
        lit = TRACK_LEN;
    uint8_t d[4] = {0, 0, 0, 0};
    for (uint8_t p = 0; p < lit; p++)
        d[TRACK[p].digit] |= TRACK[p].seg;
    uint32_t picture = d[0] | (d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if (picture != progressPicture) {
        Digiclock.setSegments(d[0], d[1], d[2], d[3], false);
        progressPicture = picture;
    }
}

// Draws the start-up progress bar, or "ErrN" while a failed stage is shown.
// Also blinks the RGB LED blue. Ends when booting goes false.
void progressTask(void* param) {
    uint8_t shownError = 0;
    while (booting) {
        uint8_t e = bootError;
        bool blinkOn = (millis() / BLINK_MS) % 2 == 0;
        if (e) {
            if (e != shownError) {
                char buf[6];
                snprintf(buf, sizeof(buf), "Err%u", e);
                showText(buf);
                shownError = e;
                progressPicture = 0xFFFFFFFF;  // text replaced the bar: redraw it next time
            }
        } else {
            shownError = 0;
            drawProgress(bootStage, blinkOn);
        }
        strip.setPixelColor(0, blinkOn ? strip.Color(0, 0, 255) : 0);
        strip.show();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    progressDone = true;
    vTaskDelete(NULL);
}

// Start-up only: moves the progress display to stage n, once the current stage
// has been visible for MIN_STAGE_MS. Does nothing after start-up.
void setStage(uint8_t n) {
    if (!booting || n == bootStage)
        return;
    while (millis() - stageSince < MIN_STAGE_MS)
        delay(10);
    bootStage = n;
    stageSince = millis();
    Serial.printf("Stage %u: %s\n", n, stageName(n));
}

// Start-up only: shows "ErrN" for 2 seconds
void showBootError(uint8_t code) {
    setError(code);
    bootError = code;
    delay(2000);
    bootError = 0;
}

// Turns a WiFi failure reason into a display code (0 = no reason yet)
uint8_t wifiError(uint8_t reason) {
    switch (reason) {
        case 0:
            return ERR_NONE;
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:             // 15:  login failed
        case WIFI_REASON_AUTH_FAIL:                          // 202: login failed
        case WIFI_REASON_HANDSHAKE_TIMEOUT:                  // 204: login failed
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:  // 210: found, security does not match
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:  // 211: found, security too weak/open
            return ERR_BAD_PASS;
        default:  // 201 NO_AP_FOUND, and anything else
            return ERR_NO_SSID;
    }
}

// Joins the WiFi network step by step (stages 1-3).
// Returns ERR_NONE, ERR_NO_SSID, ERR_BAD_PASS or ERR_NO_IP.
uint8_t joinWiFi() {
    if (WiFi.status() == WL_CONNECTED)
        return ERR_NONE;

    // 1: look for the network. Hidden networks don't show up here, so a miss
    // is only a hint; stage 2 tries anyway.
    setStage(1);
    WiFi.disconnect();
    bool seen = false;
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && !seen; i++) {
        if (WiFi.SSID(i) == ssid) {
            seen = true;
            Serial.printf("WiFi %s found: channel %ld, rssi %ld\n", ssid, (long)WiFi.channel(i),
                          (long)WiFi.RSSI(i));
        }
    }
    WiFi.scanDelete();
    if (!seen)
        Serial.printf("WiFi %s not in scan (%d networks seen)\n", ssid, n);

    // 2: log in
    setStage(2);
    lastWiFiReason = 0;
    wifiLinked = false;
    WiFi.begin(ssid, password);
    unsigned long t0 = millis();
    while (!wifiLinked && !lastWiFiReason && millis() - t0 < 10000)
        delay(50);
    if (!wifiLinked) {
        uint8_t e = wifiError(lastWiFiReason);
        return e ? e : (seen ? ERR_BAD_PASS : ERR_NO_SSID);
    }

    // 3: get an IP address
    setStage(3);
    t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000)
        delay(50);
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi joined, but no IP address from DHCP");
        return ERR_NO_IP;
    }
    return ERR_NONE;
}

// Checks the way to NTP: the internet by IP address (stage 4), then DNS (stage 5).
// Returns ERR_NO_INTERNET, ERR_DNS, or ERR_NONE when both work.
// showStages: move the start-up progress bar along (false for repeat checks).
uint8_t netCheck(bool showStages) {
    if (showStages)
        setStage(4);
    WiFiClient c;
    bool online = c.connect(IPAddress(1, 1, 1, 1), 443, 3000);
    c.stop();
    if (!online) {
        online = c.connect(IPAddress(8, 8, 8, 8), 443, 3000);
        c.stop();
    }
    if (!online) {
        Serial.println("Internet check: 1.1.1.1 and 8.8.8.8 do not answer");
        return ERR_NO_INTERNET;
    }

    if (showStages)
        setStage(5);
    IPAddress ip;
    // hostByName() returns 1 on success, but an error code (not 0) on failure
    if (WiFi.hostByName(ntpServer1, ip) != 1) {
        Serial.printf("Internet check: DNS lookup of %s failed\n", ntpServer1);
        return ERR_DNS;
    }
    return ERR_NONE;
}

// Start-up only: waits for the first NTP sync (stage 6). Every 20 s without one,
// checks the way to NTP again and shows what is wrong for 2 seconds.
void waitForNTP() {
    setStage(6);
#if SNTP_ENABLED
    unsigned long lastCheck = millis();
    while (ntpSyncCount == 0) {
        delay(100);
        if (millis() - lastCheck > 20000) {
            uint8_t e = netCheck(false);
            showBootError(e ? e : ERR_NO_NTP);
            lastCheck = millis();
        }
    }
#else
    struct tm timeInfo;
    while (!getLocalTime(&timeInfo, 1000))
        delay(100);
#endif
}

// Tracks the WiFi link, and remembers why WiFi failed (logs each new reason once;
// 201 NO_AP_FOUND = network not visible). Skips the disconnects we cause ourselves.
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    static uint8_t loggedReason = 0;
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        wifiLinked = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        lastWiFiReason = 0;
        loggedReason = 0;
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        wifiLinked = false;
        uint8_t reason = info.wifi_sta_disconnected.reason;
        if (reason == WIFI_REASON_ASSOC_LEAVE || reason == WIFI_REASON_STA_LEAVING)
            return;
        lastWiFiReason = reason;
        if (reason != loggedReason) {
            loggedReason = reason;
            Serial.printf("WiFi failed: reason %u %s\n", reason,
                          WiFi.STA.disconnectReasonName((wifi_err_reason_t)reason));
        }
    }
}

// Printed when NTP starts failing, to help find out why.
void printNetInfo() {
    Serial.printf("  WiFi: status=%d rssi=%d ip=%s gw=%s dns=%s\n", (int)WiFi.status(), WiFi.RSSI(),
                  WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                  WiFi.dnsIP().toString().c_str());
#if SNTP_ENABLED
    for (int i = 0; i < 3; i++) {
        const char* name = esp_sntp_getservername(i);
        // ip 0.0.0.0 = DNS lookup failed; reach 0x00 = last 8 requests got no reply
        Serial.printf("  NTP%d: %s ip=%s reach=0x%02x\n", i, name ? name : "-",
                      ipaddr_ntoa(esp_sntp_getserver(i)), esp_sntp_getreachability(i));
    }
#endif
}

// Owns the status LEDs after start-up. Checks WiFi and NTP every STATUS_CHECK_INTERVAL.
void statusTask(void* param) {
    unsigned long lastCheck = millis();
    uint32_t seenSyncs = 0;
    for (;;) {
#if SNTP_ENABLED
        if (ntpSyncCount != seenSyncs) {
            seenSyncs = ntpSyncCount;
            time_t now = time(nullptr);
            struct tm t;
            localtime_r(&now, &t);
            char buf[24];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
            Serial.printf("NTP sync #%lu OK, time is %s\n", (unsigned long)seenSyncs, buf);
        }
#endif

        if (millis() - lastCheck >= STATUS_CHECK_INTERVAL) {
            lastCheck = millis();

            wifiOk = (WiFi.status() == WL_CONNECTED);
            if (!wifiOk) {
                Serial.println("WiFi lost, reconnecting...");
                setStatusLED(false, ntpOk);
                uint8_t e = joinWiFi();
                wifiOk = (e == ERR_NONE);
                if (!wifiOk)
                    setError(e);
#if SNTP_ENABLED
                else
                    sntp_restart();  // fresh time now, don't wait for the next interval
#endif
            }

#if SNTP_ENABLED
            uint32_t age = uptimeSec() - lastNtpSyncSec;
            bool fresh = age < NTP_STALE_SEC;
            if (fresh != ntpOk) {
                Serial.printf("NTP %s: last good sync was %lu min ago\n", fresh ? "OK again" : "FAILING",
                              (unsigned long)(age / 60));
                if (!fresh)
                    printNetInfo();
            }
            ntpOk = fresh;
#endif

            if (wifiOk) {
                if (!ntpOk) {
                    uint8_t e = netCheck(false);
                    setError(e ? e : ERR_NO_NTP);
                } else {
                    setError(ERR_NONE);
                }
            }
        }

        setStatusLED(wifiOk, ntpOk);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    NanoC6.begin();

    pinMode(M5NANO_C6_BLUE_LED_PIN, OUTPUT);
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, HIGH);
    strip.begin();
    strip.setBrightness(128);
    strip.show();

    setStatusLED(false, false);

    Serial.begin(115200);
    Serial.println("\n\n=== NTP Clock ===");

    Wire.begin(SDA, SCL);
    Serial.println("Wire initialized");

    Serial.println("Initializing display...");
    if (Digiclock.begin(&Wire, SDA, SCL, ADD)) {
        Serial.println("Digital clock init successful");
    } else {
        Serial.println("Digital clock init error");
    }
    Digiclock.setBrightness(BRIGHTNESS_DAY);

#if SNTP_ENABLED
    sntp_set_time_sync_notification_cb(onNtpSync);
    sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
#endif
    configTzTime(ntpTimezone, ntpServer1, ntpServer2, ntpServer3);
    Serial.println("configTzTime done.");

    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_STA);
    Serial.printf("WiFi network: %s\n", ssid);
    if (strcmp(ssid, "YOUR_SSID") == 0)
        Serial.println("WiFi name not set: put it in secrets.h");

    // Progress bar until the first NTP sync
    booting = true;
    bootStage = 1;
    stageSince = millis();
    Serial.printf("Stage 1: %s\n", stageName(1));
    xTaskCreate(progressTask, "progress", 4096, NULL, 2, NULL);

    for (;;) {
        uint8_t e = joinWiFi();
        if (e == ERR_NONE)
            break;
        showBootError(e);  // then try again
    }
    wifiOk = true;
    analogWrite(M5NANO_C6_BLUE_LED_PIN, 50);
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());
#if SNTP_ENABLED
    sntp_restart();  // ask for the time now that the network is up
#endif

    uint8_t e = netCheck(true);
    if (e)
        showBootError(e);  // NTP may still work, so go on
    waitForNTP();
    setError(ERR_NONE);
    ntpOk = true;

    while (millis() - stageSince < MIN_STAGE_MS)  // let stage 6 be seen too
        delay(10);
    bootStage = STAGE_DONE;  // full bar for a moment
    delay(DONE_SHOW_MS);
    booting = false;  // progressTask ends, loop() shows the time
    while (!progressDone)
        delay(10);
    setStatusLED(wifiOk, ntpOk);

    xTaskCreate(statusTask, "status", 8192, NULL, 1, NULL);
    Serial.println("Setup complete!");
}

void loop() {
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) {
        showText("Err6");  // no valid time
        delay(1000);
        return;
    }

    static int lastSecond = -1;
    if (timeInfo.tm_sec != lastSecond) {
        lastSecond = timeInfo.tm_sec;

        uint8_t brightness = (timeInfo.tm_hour >= NIGHT_START_HOUR || timeInfo.tm_hour < NIGHT_END_HOUR) ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY;
        Digiclock.setBrightness(brightness);

        // On a problem, show "Err1".."Err6" for 2 seconds out of every 10, time the rest
        uint8_t err = errorCode;

        char buff[10];
        if (err && timeInfo.tm_sec % 10 >= 8) {
            snprintf(buff, sizeof(buff), "Err%u", err);
        } else if (timeInfo.tm_sec % 2 == 0) {
            sprintf(buff, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
        } else {
            sprintf(buff, "%02d%02d", timeInfo.tm_hour, timeInfo.tm_min);
        }
        Digiclock.setString(buff);
    }

    delay(100);
}
