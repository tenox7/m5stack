#include "M5Cardputer.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

#define SDA_PIN 2
#define SCL_PIN 1

Adafruit_7segment seg = Adafruit_7segment();

void log(const char* msg) {
  Serial.println(msg);
  M5Cardputer.Display.println(msg);
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, false);

  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(WHITE, BLACK);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.fillScreen(BLACK);

  Serial.begin(115200);
  log("7seg test");

  log("Wire init...");
  Wire.begin(SDA_PIN, SCL_PIN);
  log("Wire OK");

  log("I2C scan...");
  int found = 0;
  char buf[32];
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      sprintf(buf, "Found: 0x%02X", addr);
      log(buf);
      found++;
    }
  }
  sprintf(buf, "Total: %d", found);
  log(buf);

  log("7seg init...");
  if (!seg.begin(0x70, &Wire)) {
    log("NOT FOUND!");
    while (1) delay(1000);
  }
  log("7seg OK");

  seg.setBrightness(8);
  seg.print(0, DEC);
  seg.drawColon(false);
  seg.writeDisplay();

  log("Display 0000");
  log("Done!");
}

void loop() {
  delay(1000);
}
