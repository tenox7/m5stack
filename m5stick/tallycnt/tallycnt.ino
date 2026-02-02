#include <M5Unified.h>
#include <Preferences.h>

Preferences prefs;
int counter = 0;
int brightness = 32;
const int brightLevels[] = {32, 64, 128, 200};
int brightIdx = 0;

unsigned long lastActivity = 0;
const unsigned long sleepTimeout = 600000;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    prefs.begin("tallycnt", false);
    counter = prefs.getInt("counter", 0);
    brightIdx = prefs.getInt("brightIdx", 0);
    brightness = brightLevels[brightIdx];

    M5.Display.fillScreen(BLACK);
    M5.Display.setBrightness(brightness);
    M5.Speaker.setVolume(255);
    lastActivity = millis();

    drawCounter();
}

void drawBattery() {
    int level = M5.Power.getBatteryLevel();
    int rot = M5.Display.getRotation();
    M5.Display.setRotation(0);

    int bw = 40, bh = 16;
    int x = 135 - bw - 4;
    int y = 240 - bh - 3;

    uint16_t color = GREEN;
    if (level <= 25) color = RED;
    else if (level <= 50) color = YELLOW;

    M5.Display.drawRect(x, y, bw, bh, WHITE);
    M5.Display.fillRect(x + 1, y + 1, (bw - 2) * level / 100, bh - 2, color);

    M5.Display.setRotation(rot);
}

void drawCounter() {
    M5.Display.setTextColor(YELLOW, BLACK);

    if (counter < 10) {
        M5.Display.setRotation(0);
        M5.Display.fillScreen(BLACK);
        M5.Display.setTextSize(22);
        M5.Display.setCursor(3, 20);
        M5.Display.printf("%d", counter);
    } else if (counter < 100) {
        M5.Display.setRotation(1);
        M5.Display.fillScreen(BLACK);
        M5.Display.setTextSize(18);
        M5.Display.setCursor(5, 10);
        M5.Display.printf("%02d", counter);
    } else {
        M5.Display.setRotation(1);
        M5.Display.fillScreen(BLACK);
        M5.Display.setTextSize(12);
        M5.Display.setCursor(5, 25);
        M5.Display.printf("%03d", counter);
    }

    drawBattery();
}

void beep(int freq, int duration) {
    M5.Speaker.tone(freq, duration);
    delay(duration);
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        lastActivity = millis();
        counter++;
        if (counter > 999) counter = 999;
        prefs.putInt("counter", counter);
        drawCounter();
        beep(3000, 50);
    }

    if (M5.BtnB.wasPressed()) {
        lastActivity = millis();
        counter = 0;
        prefs.putInt("counter", counter);
        drawCounter();
        beep(4000, 60);
        delay(40);
        beep(4000, 60);
        delay(40);
        beep(4000, 60);
    }

    if (M5.BtnPWR.wasHold()) {
        M5.Power.powerOff();
    }

    if (M5.BtnPWR.wasReleased() && !M5.BtnPWR.wasHold()) {
        lastActivity = millis();
        brightIdx = (brightIdx + 1) % 4;
        prefs.putInt("brightIdx", brightIdx);
        M5.Display.setBrightness(brightLevels[brightIdx]);
    }

    if (millis() - lastActivity > sleepTimeout) {
        M5.Power.powerOff();
    }

    delay(10);
}
