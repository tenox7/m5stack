#include <M5NanoC6.h>
#include <Wire.h>
#include <M5UNIT_DIGI_CLOCK.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

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

const char* ssid = "tenox";
const char* password = "";
const char* ntpServer1 = "time.google.com";
const char* ntpServer2 = "time.cloudflare.com";
const char* ntpServer3 = "pool.ntp.org";
const char* ntpTimezone = "PST8PDT,M3.2.0,M11.1.0";

const unsigned long STATUS_CHECK_INTERVAL = 30000;

volatile bool wifiOk = false;
volatile bool ntpOk = false;

M5UNIT_DIGI_CLOCK Digiclock;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setStatusLED(bool wifi, bool ntp) {
    analogWrite(M5NANO_C6_BLUE_LED_PIN, wifi ? 50 : 0);

    if (!wifi || !ntp) {
        strip.setPixelColor(0, strip.Color(255, 0, 0));
    } else {
        strip.setPixelColor(0, strip.Color(0, 255, 0));
    }
    strip.show();
}

void blinkBlueLED() {
    static unsigned long lastBlink = 0;
    static bool blueState = false;
    if (millis() - lastBlink > 250) {
        blueState = !blueState;
        strip.setPixelColor(0, blueState ? strip.Color(0, 0, 255) : strip.Color(0, 0, 0));
        strip.show();
        lastBlink = millis();
    }
}

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED)
        return true;
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i) {
        delay(500);
        blinkBlueLED();
    }
    return WiFi.status() == WL_CONNECTED;
}

void waitForNTP() {
#if SNTP_ENABLED
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        Serial.print(".");
        delay(150);
        blinkBlueLED();
    }
#else
    delay(1600);
    struct tm timeInfo;
    while (!getLocalTime(&timeInfo, 1000)) {
        Serial.print(".");
        blinkBlueLED();
    }
#endif
}

void statusTask(void* param) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_CHECK_INTERVAL));

        wifiOk = (WiFi.status() == WL_CONNECTED);
        if (!wifiOk) {
            Serial.println("WiFi lost, reconnecting...");
            setStatusLED(false, false);
            wifiOk = connectWiFi();
#if SNTP_ENABLED
            if (wifiOk) {
                sntp_restart();
                waitForNTP();
                ntpOk = true;
            }
#endif
        }
        setStatusLED(wifiOk, ntpOk);
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

    configTzTime(ntpTimezone, ntpServer1, ntpServer2, ntpServer3);
    Serial.println("configTzTime done.");

    while (!wifiOk) {
        Serial.print("WiFi connecting...");
        wifiOk = connectWiFi();
        Serial.println(wifiOk ? " OK" : " retry");
        if (!wifiOk)
            delay(2000);
    }
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());

    setStatusLED(wifiOk, false);

    Serial.print("NTP syncing");
    waitForNTP();
    Serial.println(" OK");
    ntpOk = true;
    setStatusLED(wifiOk, ntpOk);

    Serial.println("Initializing display...");
    if (Digiclock.begin(&Wire, SDA, SCL, ADD)) {
        Serial.println("Digital clock init successful");
    } else {
        Serial.println("Digital clock init error");
    }

    xTaskCreate(statusTask, "status", 8192, NULL, 1, NULL);
    Serial.println("Setup complete!");
}

void loop() {
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) {
        Digiclock.setString(" Err");
        setStatusLED(false, false);
        delay(1000);
        return;
    }

    static int lastSecond = -1;
    if (timeInfo.tm_sec != lastSecond) {
        lastSecond = timeInfo.tm_sec;

        uint8_t brightness = (timeInfo.tm_hour >= NIGHT_START_HOUR || timeInfo.tm_hour < NIGHT_END_HOUR) ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY;
        Digiclock.setBrightness(brightness);

        char buff[10];
        if (timeInfo.tm_sec % 2 == 0) {
            sprintf(buff, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
        } else {
            sprintf(buff, "%02d%02d", timeInfo.tm_hour, timeInfo.tm_min);
        }
        Digiclock.setString(buff);
    }

    delay(100);
}