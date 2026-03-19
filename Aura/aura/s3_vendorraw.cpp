#include <Arduino.h>
#include <SPI.h>

static constexpr int PIN_CS = 10;
static constexpr int PIN_DC = 46;
static constexpr int PIN_BL = 45;
static constexpr int PIN_MOSI = 11;
static constexpr int PIN_SCLK = 12;
static constexpr int PIN_MISO = 13;

static SPIClass spi(FSPI);

static inline void csLow() { digitalWrite(PIN_CS, LOW); }
static inline void csHigh() { digitalWrite(PIN_CS, HIGH); }
static inline void dcLow() { digitalWrite(PIN_DC, LOW); }
static inline void dcHigh() { digitalWrite(PIN_DC, HIGH); }

static void writeCmd(uint8_t cmd) {
  spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcLow();
  spi.transfer(cmd);
  csHigh();
  spi.endTransaction();
}

static void writeData(uint8_t data) {
  spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcHigh();
  spi.transfer(data);
  csHigh();
  spi.endTransaction();
}

static void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  writeCmd(0x2A);
  writeData(x0 >> 8); writeData(x0 & 0xFF);
  writeData(x1 >> 8); writeData(x1 & 0xFF);
  writeCmd(0x2B);
  writeData(y0 >> 8); writeData(y0 & 0xFF);
  writeData(y1 >> 8); writeData(y1 & 0xFF);
  writeCmd(0x2C);
}

static void pushColor(uint16_t color, uint32_t count) {
  uint8_t hi = color >> 8;
  uint8_t lo = color & 0xFF;
  spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcHigh();
  for (uint32_t i = 0; i < count; i++) {
    spi.transfer(hi);
    spi.transfer(lo);
  }
  csHigh();
  spi.endTransaction();
}

static void fillScreen(uint16_t color) {
  setAddrWindow(0, 0, 239, 319);
  pushColor(color, 240UL * 320UL);
}

static void ili9341InitVendor() {
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
  writeCmd(0x36); writeData(0x08);
  writeCmd(0xB6); writeData(0x08); writeData(0x82);
  writeCmd(0x3A); writeData(0x55);
  writeCmd(0xF6); writeData(0x01); writeData(0x30);
  writeCmd(0xB1); writeData(0x00); writeData(0x1B);
  writeCmd(0xF2); writeData(0x00);
  writeCmd(0x26); writeData(0x01);

  writeCmd(0xE0);
  uint8_t e0[] = {0x0F,0x35,0x31,0x0B,0x0E,0x06,0x49,0xA7,0x33,0x07,0x0F,0x03,0x0C,0x0A,0x00};
  for (uint8_t v : e0) writeData(v);

  writeCmd(0xE1);
  uint8_t e1[] = {0x00,0x0A,0x0F,0x04,0x11,0x08,0x36,0x58,0x4D,0x07,0x10,0x0C,0x32,0x34,0x0F};
  for (uint8_t v : e1) writeData(v);

  writeCmd(0x11);
  delay(120);
  writeCmd(0x29);
}

void setup() {
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);

  pinMode(42, OUTPUT);
  digitalWrite(42, HIGH);

  spi.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, -1);
  delay(50);
  ili9341InitVendor();

  fillScreen(0xF800);
  delay(400);
  fillScreen(0x07E0);
  delay(400);
  fillScreen(0x001F);
  delay(400);
  fillScreen(0x0000);
}

void loop() {
  static uint8_t idx = 0;
  static const uint16_t colors[] = {0xFFFF, 0xF800, 0x07E0, 0x001F, 0x0000};
  fillScreen(colors[idx % 5]);
  digitalWrite(42, (idx & 1) ? HIGH : LOW);
  idx++;
  delay(800);
}
