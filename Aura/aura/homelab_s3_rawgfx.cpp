#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>

#ifndef LCD_MISO
#define LCD_MISO 13
#endif
#ifndef LCD_MOSI
#define LCD_MOSI 11
#endif
#ifndef LCD_SCLK
#define LCD_SCLK 12
#endif
#ifndef LCD_CS
#define LCD_CS 10
#endif
#ifndef LCD_DC
#define LCD_DC 46
#endif
#ifndef LCD_BL
#define LCD_BL 45
#endif
#ifndef LCD_EN
#define LCD_EN 42
#endif

static SPIClass lcdSpi(FSPI);

class RawILI9341GFX : public Adafruit_GFX {
public:
  RawILI9341GFX() : Adafruit_GFX(240, 320) {}

  void begin() {
    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_BL, OUTPUT);
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);
    digitalWrite(LCD_BL, HIGH);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_DC, HIGH);
    pinMode(42, OUTPUT);
    digitalWrite(42, HIGH);
    delay(50);

    lcdSpi.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, -1);
    initVendor();
    fillScreen(0x0000);
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if ((x < 0) || (y < 0) || (x >= width()) || (y >= height())) return;
    setAddrWindow(x, y, x, y);
    pushColor(color, 1);
  }

  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if ((w <= 0) || (h <= 0)) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if ((x + w) > width()) w = width() - x;
    if ((y + h) > height()) h = height() - y;
    if ((w <= 0) || (h <= 0)) return;
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    pushColor(color, (uint32_t)w * (uint32_t)h);
  }

  void fillScreen(uint16_t color) override {
    writeFillRect(0, 0, width(), height(), color);
  }

private:
  void csLow() { digitalWrite(LCD_CS, LOW); }
  void csHigh() { digitalWrite(LCD_CS, HIGH); }
  void dcLow() { digitalWrite(LCD_DC, LOW); }
  void dcHigh() { digitalWrite(LCD_DC, HIGH); }

  void writeCmd(uint8_t cmd) {
    lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    csLow();
    dcLow();
    lcdSpi.transfer(cmd);
    csHigh();
    lcdSpi.endTransaction();
  }

  void writeData(uint8_t data) {
    lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    csLow();
    dcHigh();
    lcdSpi.transfer(data);
    csHigh();
    lcdSpi.endTransaction();
  }

  void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    writeCmd(0x2A);
    writeData(x0 >> 8); writeData(x0 & 0xFF);
    writeData(x1 >> 8); writeData(x1 & 0xFF);
    writeCmd(0x2B);
    writeData(y0 >> 8); writeData(y0 & 0xFF);
    writeData(y1 >> 8); writeData(y1 & 0xFF);
    writeCmd(0x2C);
  }

  void pushColor(uint16_t color, uint32_t count) {
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    csLow();
    dcHigh();
    for (uint32_t i = 0; i < count; i++) {
      lcdSpi.transfer(hi);
      lcdSpi.transfer(lo);
    }
    csHigh();
    lcdSpi.endTransaction();
  }

  void initVendor() {
    writeCmd(0xCF); writeData(0x00); writeData(0xC1); writeData(0x30);
    writeCmd(0xED); writeData(0x64); writeData(0x03); writeData(0x12); writeData(0x81);
    writeCmd(0xE8); writeData(0x85); writeData(0x00); writeData(0x78);
    writeCmd(0xCB); writeData(0x39); writeData(0x2C); writeData(0x00); writeData(0x34); writeData(0x02);
    writeCmd(0xF7); writeData(0x20);
    writeCmd(0xEA); writeData(0x00); writeData(0x00);
    writeCmd(0xC0); writeData(0x13);
    writeCmd(0xC1); writeData(0x13);
    writeCmd(0xC5); writeData(0x22); writeData(0x35);
    writeCmd(0xC7); writeData(0xBD);
    writeCmd(0x21);
    writeCmd(0x36); writeData(0x48);
    writeCmd(0xB6); writeData(0x08); writeData(0x82);
    writeCmd(0x3A); writeData(0x55);
    writeCmd(0xF6); writeData(0x01); writeData(0x30);
    writeCmd(0xB1); writeData(0x00); writeData(0x1B);
    writeCmd(0xF2); writeData(0x00);
    writeCmd(0x26); writeData(0x01);
    const uint8_t e0[] = {0x0F,0x35,0x31,0x0B,0x0E,0x06,0x49,0xA7,0x33,0x07,0x0F,0x03,0x0C,0x0A,0x00};
    writeCmd(0xE0); for (uint8_t v : e0) writeData(v);
    const uint8_t e1[] = {0x00,0x0A,0x0F,0x04,0x11,0x08,0x36,0x58,0x4D,0x07,0x10,0x0C,0x32,0x34,0x0F};
    writeCmd(0xE1); for (uint8_t v : e1) writeData(v);
    writeCmd(0x11);
    delay(120);
    writeCmd(0x29);
  }
};

static RawILI9341GFX tft;

void setup() {
  Serial.begin(115200);
  delay(100);
#if defined(ARDUINO_USB_CDC_ON_BOOT)
  delay(3000);
#endif

  tft.begin();
  tft.fillScreen(0xF800);
  delay(250);
  tft.fillScreen(0x07E0);
  delay(250);
  tft.fillScreen(0x001F);
  delay(250);
  tft.fillScreen(0x0000);

  tft.setTextColor(0xFFFF);
  tft.setTextSize(2);
  tft.setCursor(12, 20);
  tft.print("RAW GFX OK");
  tft.setCursor(12, 50);
  tft.print("Transport works");
  tft.setCursor(12, 80);
  tft.print("Library bypass");
}

void loop() {
  static uint32_t last = 0;
  static bool on = false;
  if (millis() - last > 1000) {
    last = millis();
    on = !on;
    tft.writeFillRect(200, 8, 24, 24, on ? 0x07FF : 0x39E7);
  }
}
