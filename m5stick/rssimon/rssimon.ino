#include <M5Unified.h>
#include <WiFi.h>

const char* ssid = "tenox";
const char* password = "dupadupa";

const int SCREEN_W = 240;
const int SCREEN_H = 135;
const int BAR_X = 10;
const int BAR_Y = 100;
const int BAR_W = SCREEN_W - 20;
const int BAR_H = 20;
const int MAX_SAMPLES = BAR_W;
const int UPDATE_INTERVAL = 500;

const int RSSI_MIN = -90;
const int RSSI_MAX = -30;

int rssiHistory[MAX_SAMPLES];
int sampleIndex = 0;
int sampleCount = 0;
bool wifiOk = false;

M5Canvas canvas(&M5.Display);

uint16_t rssiColor(int rssi) {
    if (rssi > -50) return TFT_GREEN;
    if (rssi > -60) return TFT_YELLOW;
    if (rssi > -70) return TFT_ORANGE;
    return TFT_RED;
}

int rssiToBarLen(int rssi) {
    int clamped = constrain(rssi, RSSI_MIN, RSSI_MAX);
    return map(clamped, RSSI_MIN, RSSI_MAX, 0, BAR_W);
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

void showStatus(const char* msg) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, SCREEN_H / 2 - 10);
    M5.Display.print(msg);
}

void drawDisplay(int rssi) {
    canvas.fillSprite(TFT_BLACK);

    uint16_t color = rssiColor(rssi);

    canvas.setTextColor(color);
    canvas.setTextSize(6);
    canvas.setCursor(30, 20);
    canvas.printf("%d", rssi);

    canvas.setTextSize(2);
    canvas.setCursor(170, 35);
    canvas.print("dBm");

    canvas.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, TFT_DARKGREY);
    int barLen = rssiToBarLen(rssi);
    if (barLen > 0)
        canvas.fillRect(BAR_X + 1, BAR_Y + 1, barLen - 2, BAR_H - 2, color);

    int x25 = BAR_X + BAR_W / 4;
    int x50 = BAR_X + BAR_W / 2;
    int x75 = BAR_X + 3 * BAR_W / 4;
    canvas.drawFastVLine(x25, BAR_Y, BAR_H, TFT_DARKGREY);
    canvas.drawFastVLine(x50, BAR_Y, BAR_H, TFT_DARKGREY);
    canvas.drawFastVLine(x75, BAR_Y, BAR_H, TFT_DARKGREY);

    canvas.setTextColor(TFT_DARKGREY);
    canvas.setTextSize(1);
    canvas.setCursor(BAR_X, BAR_Y + BAR_H + 4);
    canvas.print("-90");
    canvas.setCursor(BAR_X + BAR_W - 18, BAR_Y + BAR_H + 4);
    canvas.print("-30");

    int bat = M5.Power.getBatteryLevel();
    uint16_t batColor = bat > 50 ? TFT_GREEN : bat > 20 ? TFT_YELLOW : TFT_RED;
    int batX = SCREEN_W - 45;
    int batY = 4;
    canvas.drawRect(batX, batY, 24, 10, TFT_WHITE);
    canvas.fillRect(batX + 24, batY + 3, 3, 4, TFT_WHITE);
    int fillW = (bat * 22) / 100;
    if (fillW > 0)
        canvas.fillRect(batX + 1, batY + 1, fillW, 8, batColor);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(batX + 28, batY + 1);
    canvas.printf("%d", bat);

    canvas.pushSprite(0, 0);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(64);
    M5.Display.fillScreen(TFT_BLACK);

    canvas.createSprite(SCREEN_W, SCREEN_H);

    Serial.begin(115200);
    Serial.println("RSSI Monitor starting...");

    showStatus("Connecting...");

    while (!wifiOk) {
        wifiOk = connectWiFi();
        if (!wifiOk) {
            showStatus("WiFi retry...");
            delay(2000);
        }
    }

    Serial.printf("Connected to %s\n", ssid);
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
        return;
    }

    int rssi = WiFi.RSSI();
    Serial.printf("RSSI: %d dBm\n", rssi);

    drawDisplay(rssi);

    delay(UPDATE_INTERVAL);
}
