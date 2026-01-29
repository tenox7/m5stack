#include <M5Unified.h>

M5Canvas canvas(&M5.Display);

float ballX, ballY;
float velX = 0, velY = 0;
float accX, accY, accZ;

const int ballRadius = 6;
const float friction = 0.995;
const float accelScale = 2.0;
const float bounce = 0.85;

int screenW, screenH;

const uint16_t colors[] = {TFT_WHITE, TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_CYAN, TFT_MAGENTA, TFT_ORANGE};
const int numColors = sizeof(colors) / sizeof(colors[0]);
int colorIndex = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Imu.begin();

    M5.Display.setRotation(1);
    screenW = M5.Display.width();
    screenH = M5.Display.height();

    ballX = screenW / 2;
    ballY = screenH / 2;

    canvas.createSprite(screenW, screenH);
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        colorIndex = (colorIndex + 1) % numColors;
    }

    M5.Imu.getAccel(&accX, &accY, &accZ);

    velX += accY * accelScale;
    velY += accX * accelScale;

    velX *= friction;
    velY *= friction;

    ballX += velX;
    ballY += velY;

    bool hit = false;

    if (ballX - ballRadius < 0) {
        ballX = ballRadius;
        velX = -velX * bounce;
        hit = true;
    }
    if (ballX + ballRadius > screenW) {
        ballX = screenW - ballRadius;
        velX = -velX * bounce;
        hit = true;
    }
    if (ballY - ballRadius < 0) {
        ballY = ballRadius;
        velY = -velY * bounce;
        hit = true;
    }
    if (ballY + ballRadius > screenH) {
        ballY = screenH - ballRadius;
        velY = -velY * bounce;
        hit = true;
    }

    if (hit) {
        float speed = sqrt(velX * velX + velY * velY);
        int freq = 800 + (int)(speed * 50);
        M5.Speaker.tone(freq, 20);
    }

    canvas.fillScreen(TFT_BLACK);
    canvas.fillCircle((int)ballX, (int)ballY, ballRadius, colors[colorIndex]);
    canvas.pushSprite(0, 0);

    delay(8);
}
