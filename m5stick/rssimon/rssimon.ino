#include <M5Unified.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHAR_SSID_UUID      "12345678-1234-1234-1234-123456789ab1"
#define CHAR_PASS_UUID      "12345678-1234-1234-1234-123456789ab2"
#define CHAR_CMD_UUID       "12345678-1234-1234-1234-123456789ab3"
#define CHAR_STATUS_UUID    "12345678-1234-1234-1234-123456789ab4"

const int SCREEN_W = 240;
const int SCREEN_H = 135;
const int BAR_X = 10;
const int BAR_Y = 100;
const int BAR_W = SCREEN_W - 20;
const int BAR_H = 20;
const int UPDATE_INTERVAL = 500;

const int RSSI_MIN = -90;
const int RSSI_MAX = -30;

String wifiSSID = "";
String wifiPass = "";
bool wifiOk = false;
bool bleConnected = false;
bool shouldConnect = false;

Preferences prefs;
M5Canvas canvas(&M5.Display);
BLEServer* pServer = nullptr;
BLECharacteristic* pStatusChar = nullptr;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* s) override { bleConnected = true; }
    void onDisconnect(BLEServer* s) override {
        bleConnected = false;
        BLEDevice::startAdvertising();
    }
};

class SSIDCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        wifiSSID = c->getValue().c_str();
        Serial.printf("BLE: SSID set to '%s'\n", wifiSSID.c_str());
    }
};

class PassCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        wifiPass = c->getValue().c_str();
        Serial.println("BLE: Password set");
    }
};

class CmdCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        String cmd = c->getValue().c_str();
        if (cmd == "connect") {
            prefs.begin("wifi", false);
            prefs.putString("ssid", wifiSSID);
            prefs.putString("pass", wifiPass);
            prefs.end();
            shouldConnect = true;
            Serial.println("BLE: Connect command received");
        }
    }
};

void updateBLEStatus(const char* status) {
    if (pStatusChar)
        pStatusChar->setValue(status);
}

void setupBLE() {
    BLEDevice::init("RSSIMon");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    BLECharacteristic* pSSID = pService->createCharacteristic(
        CHAR_SSID_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pSSID->setCallbacks(new SSIDCallback());

    BLECharacteristic* pPass = pService->createCharacteristic(
        CHAR_PASS_UUID, BLECharacteristic::PROPERTY_WRITE);
    pPass->setCallbacks(new PassCallback());

    BLECharacteristic* pCmd = pService->createCharacteristic(
        CHAR_CMD_UUID, BLECharacteristic::PROPERTY_WRITE);
    pCmd->setCallbacks(new CmdCallback());

    pStatusChar = pService->createCharacteristic(
        CHAR_STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pStatusChar->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.println("BLE: Advertising as 'RSSIMon'");
}

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
    if (wifiSSID.isEmpty())
        return false;
    if (WiFi.status() == WL_CONNECTED)
        return true;
    updateBLEStatus("connecting");
    WiFi.disconnect();
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    for (int i = 40; i && WiFi.status() != WL_CONNECTED; --i)
        delay(250);
    bool ok = WiFi.status() == WL_CONNECTED;
    updateBLEStatus(ok ? "connected" : "failed");
    return ok;
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

    canvas.setTextColor(bleConnected ? TFT_BLUE : TFT_DARKGREY);
    canvas.setCursor(2, 2);
    canvas.print("BLE");

    canvas.pushSprite(0, 0);
}

void drawWaiting() {
    canvas.fillSprite(TFT_BLACK);

    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    canvas.setCursor(20, 30);
    canvas.print("Waiting for");
    canvas.setCursor(20, 55);
    canvas.print("BLE config...");

    canvas.setTextColor(bleConnected ? TFT_BLUE : TFT_CYAN);
    canvas.setTextSize(1);
    canvas.setCursor(20, 90);
    canvas.printf("BLE: %s", bleConnected ? "Connected" : "Advertising");

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

    prefs.begin("wifi", true);
    wifiSSID = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    prefs.end();

    setupBLE();

    if (!wifiSSID.isEmpty()) {
        showStatus("Connecting...");
        wifiOk = connectWiFi();
        if (wifiOk) {
            Serial.printf("Connected to %s\n", wifiSSID.c_str());
            showStatus("Connected!");
            delay(500);
        }
    }
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        int br = M5.Display.getBrightness();
        br = (br >= 200) ? 32 : br + 40;
        M5.Display.setBrightness(br);
    }

    if (shouldConnect) {
        shouldConnect = false;
        showStatus("Connecting...");
        wifiOk = connectWiFi();
        if (wifiOk)
            showStatus("Connected!");
        else
            showStatus("Failed!");
        delay(500);
    }

    if (!wifiOk && wifiSSID.isEmpty()) {
        drawWaiting();
        delay(UPDATE_INTERVAL);
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        wifiOk = false;
        showStatus("Reconnecting...");
        wifiOk = connectWiFi();
        if (!wifiOk) {
            delay(2000);
            return;
        }
    }

    int rssi = WiFi.RSSI();
    Serial.printf("RSSI: %d dBm\n", rssi);

    drawDisplay(rssi);

    delay(UPDATE_INTERVAL);
}
