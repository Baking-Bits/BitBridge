#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>

#ifndef TFT_MISO
#define TFT_MISO 13
#endif
#ifndef TFT_MOSI
#define TFT_MOSI 11
#endif
#ifndef TFT_SCLK
#define TFT_SCLK 12
#endif
#ifndef TFT_CS
#define TFT_CS 10
#endif
#ifndef TFT_DC
#define TFT_DC 46
#endif
#ifndef TFT_BL
#define TFT_BL 45
#endif
#ifndef LCD_ENABLE_PIN
#define LCD_ENABLE_PIN 42
#endif

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif
#ifndef TFT_NAVY
#define TFT_NAVY 0x000F
#endif
#ifndef TFT_DARKGREEN
#define TFT_DARKGREEN 0x03E0
#endif
#ifndef TFT_DARKCYAN
#define TFT_DARKCYAN 0x03EF
#endif
#ifndef TFT_MAROON
#define TFT_MAROON 0x7800
#endif
#ifndef TFT_PURPLE
#define TFT_PURPLE 0x780F
#endif
#ifndef TFT_OLIVE
#define TFT_OLIVE 0x7BE0
#endif
#ifndef TFT_LIGHTGREY
#define TFT_LIGHTGREY 0xC618
#endif
#ifndef TFT_DARKGREY
#define TFT_DARKGREY 0x7BEF
#endif
#ifndef TFT_BLUE
#define TFT_BLUE 0x001F
#endif
#ifndef TFT_GREEN
#define TFT_GREEN 0x07E0
#endif
#ifndef TFT_CYAN
#define TFT_CYAN 0x07FF
#endif
#ifndef TFT_RED
#define TFT_RED 0xF800
#endif
#ifndef TFT_MAGENTA
#define TFT_MAGENTA 0xF81F
#endif
#ifndef TFT_YELLOW
#define TFT_YELLOW 0xFFE0
#endif
#ifndef TFT_WHITE
#define TFT_WHITE 0xFFFF
#endif
#ifndef TFT_ORANGE
#define TFT_ORANGE 0xFD20
#endif

class TFT_eSPI : public Adafruit_GFX {
 public:
  TFT_eSPI() : Adafruit_GFX(240, 320), spi(FSPI), inTransaction(false), rotationValue(0x48), swapBytes(false) {}

  void init() {
    pinMode(LCD_ENABLE_PIN, OUTPUT);
    pinMode(TFT_BL, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);

    digitalWrite(LCD_ENABLE_PIN, HIGH);
    digitalWrite(TFT_BL, HIGH);
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TFT_DC, HIGH);

    pinMode(42, OUTPUT);
    digitalWrite(42, HIGH);

    delay(50);
    spi.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);

    applyVendorInit();
    setRotation(0);
  }

  void setRotation(uint8_t m) {
    Adafruit_GFX::setRotation(m);
    uint8_t madctl = 0x48;
    switch (m & 3) {
      case 0: madctl = 0x48; break;
      case 1: madctl = 0x28; break;
      case 2: madctl = 0x88; break;
      case 3: madctl = 0xE8; break;
    }
    rotationValue = madctl;
    startWrite();
    writecommand(0x36);
    writedata(rotationValue);
    endWrite();
  }

  void startWrite() {
    if (inTransaction) return;
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    inTransaction = true;
  }

  void endWrite() {
    if (!inTransaction) return;
    digitalWrite(TFT_CS, HIGH);
    spi.endTransaction();
    inTransaction = false;
  }

  void writecommand(uint8_t cmd) {
    if (inTransaction) {
      digitalWrite(TFT_DC, LOW);
      spi.transfer(cmd);
      return;
    }
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW);
    spi.transfer(cmd);
    digitalWrite(TFT_CS, HIGH);
    spi.endTransaction();
  }

  void writedata(uint8_t data) {
    if (inTransaction) {
      digitalWrite(TFT_DC, HIGH);
      spi.transfer(data);
      return;
    }
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, HIGH);
    spi.transfer(data);
    digitalWrite(TFT_CS, HIGH);
    spi.endTransaction();
  }

  void setSwapBytes(bool enable) {
    swapBytes = enable;
  }

  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) return;
    setAddrWindowRaw(x, y, x + w - 1, y + h - 1);
  }

  void pushColors(uint16_t *data, uint32_t len, bool swap) {
    bool startedHere = false;
    if (!inTransaction) {
      startWrite();
      startedHere = true;
    }

    digitalWrite(TFT_DC, HIGH);
    while (len--) {
      uint16_t color = *data++;
      bool useSwap = swap || swapBytes;
      if (useSwap) {
        spi.transfer(color & 0xFF);
        spi.transfer(color >> 8);
      } else {
        spi.transfer(color >> 8);
        spi.transfer(color & 0xFF);
      }
    }

    if (startedHere) {
      endWrite();
    }
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    startWrite();
    setAddrWindowRaw(x, y, x, y);
    writeColorRaw(color, 1);
    endWrite();
  }

  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width()) w = width() - x;
    if (y + h > height()) h = height() - y;
    if (w <= 0 || h <= 0) return;

    startWrite();
    setAddrWindowRaw(x, y, x + w - 1, y + h - 1);
    writeColorRaw(color, (uint32_t)w * (uint32_t)h);
    endWrite();
  }

  void fillScreen(uint16_t color) override {
    writeFillRect(0, 0, width(), height(), color);
  }

 private:
  SPIClass spi;
  bool inTransaction;
  uint8_t rotationValue;
  bool swapBytes;

  void setAddrWindowRaw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    writecommand(0x2A);
    writedata(x0 >> 8); writedata(x0 & 0xFF);
    writedata(x1 >> 8); writedata(x1 & 0xFF);

    writecommand(0x2B);
    writedata(y0 >> 8); writedata(y0 & 0xFF);
    writedata(y1 >> 8); writedata(y1 & 0xFF);

    writecommand(0x2C);
  }

  void writeColorRaw(uint16_t color, uint32_t count) {
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    digitalWrite(TFT_DC, HIGH);
    while (count--) {
      spi.transfer(hi);
      spi.transfer(lo);
    }
  }

  void applyVendorInit() {
    startWrite();

    writecommand(0xCF); writedata(0x00); writedata(0xC1); writedata(0x30);
    writecommand(0xED); writedata(0x64); writedata(0x03); writedata(0x12); writedata(0x81);
    writecommand(0xE8); writedata(0x85); writedata(0x00); writedata(0x78);
    writecommand(0xCB); writedata(0x39); writedata(0x2C); writedata(0x00); writedata(0x34); writedata(0x02);
    writecommand(0xF7); writedata(0x20);
    writecommand(0xEA); writedata(0x00); writedata(0x00);
    writecommand(0xC0); writedata(0x13);
    writecommand(0xC1); writedata(0x13);
    writecommand(0xC5); writedata(0x22); writedata(0x35);
    writecommand(0xC7); writedata(0xBD);
    writecommand(0x21);
    writecommand(0x36); writedata(rotationValue);
    writecommand(0xB6); writedata(0x08); writedata(0x82);
    writecommand(0x3A); writedata(0x55);
    writecommand(0xF6); writedata(0x01); writedata(0x30);
    writecommand(0xB1); writedata(0x00); writedata(0x1B);
    writecommand(0xF2); writedata(0x00);
    writecommand(0x26); writedata(0x01);

    const uint8_t e0[] = {0x0F,0x35,0x31,0x0B,0x0E,0x06,0x49,0xA7,0x33,0x07,0x0F,0x03,0x0C,0x0A,0x00};
    writecommand(0xE0);
    for (uint8_t value : e0) writedata(value);

    const uint8_t e1[] = {0x00,0x0A,0x0F,0x04,0x11,0x08,0x36,0x58,0x4D,0x07,0x10,0x0C,0x32,0x34,0x0F};
    writecommand(0xE1);
    for (uint8_t value : e1) writedata(value);

    writecommand(0x11);
    endWrite();
    delay(120);

    startWrite();
    writecommand(0x29);
    endWrite();
  }
};
