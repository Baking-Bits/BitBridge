#include <Arduino.h>
#include <SPI.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

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
#ifndef LCD_RST
#define LCD_RST -1
#endif
#ifndef LCD_BL
#define LCD_BL 45
#endif
#ifndef LCD_EN
#define LCD_EN 42
#endif

static SPIClass lcdSpi(FSPI);
static Adafruit_ILI9341 tft(&lcdSpi, LCD_CS, LCD_DC, LCD_RST);
static const char *CAPTIVE_SSID = "HomeLab-S3-ADA";

static void sendCmd(uint8_t cmd) {
  tft.sendCommand(cmd, (const uint8_t *)nullptr, 0);
}

static void sendCmdData(uint8_t cmd, const uint8_t *data, uint8_t len) {
  tft.sendCommand(cmd, data, len);
}

static void applyVendorInit() {
  const uint8_t cf[] = {0x00, 0xC1, 0x30};
  sendCmdData(0xCF, cf, sizeof(cf));
  const uint8_t ed[] = {0x64, 0x03, 0x12, 0x81};
  sendCmdData(0xED, ed, sizeof(ed));
  const uint8_t e8[] = {0x85, 0x00, 0x78};
  sendCmdData(0xE8, e8, sizeof(e8));
  const uint8_t cb[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
  sendCmdData(0xCB, cb, sizeof(cb));
  const uint8_t f7[] = {0x20};
  sendCmdData(0xF7, f7, sizeof(f7));
  const uint8_t ea[] = {0x00, 0x00};
  sendCmdData(0xEA, ea, sizeof(ea));
  const uint8_t c0[] = {0x13};
  sendCmdData(0xC0, c0, sizeof(c0));
  const uint8_t c1[] = {0x13};
  sendCmdData(0xC1, c1, sizeof(c1));
  const uint8_t c5[] = {0x22, 0x35};
  sendCmdData(0xC5, c5, sizeof(c5));
  const uint8_t c7[] = {0xBD};
  sendCmdData(0xC7, c7, sizeof(c7));
  sendCmd(0x21);
  const uint8_t m36[] = {0x08};
  sendCmdData(0x36, m36, sizeof(m36));
  const uint8_t b6[] = {0x08, 0x82};
  sendCmdData(0xB6, b6, sizeof(b6));
  const uint8_t pix[] = {0x55};
  sendCmdData(0x3A, pix, sizeof(pix));
  const uint8_t f6[] = {0x01, 0x30};
  sendCmdData(0xF6, f6, sizeof(f6));
  const uint8_t b1[] = {0x00, 0x1B};
  sendCmdData(0xB1, b1, sizeof(b1));
  const uint8_t f2[] = {0x00};
  sendCmdData(0xF2, f2, sizeof(f2));
  const uint8_t gsel[] = {0x01};
  sendCmdData(0x26, gsel, sizeof(gsel));
  const uint8_t e0[] = {0x0F,0x35,0x31,0x0B,0x0E,0x06,0x49,0xA7,0x33,0x07,0x0F,0x03,0x0C,0x0A,0x00};
  sendCmdData(0xE0, e0, sizeof(e0));
  const uint8_t e1[] = {0x00,0x0A,0x0F,0x04,0x11,0x08,0x36,0x58,0x4D,0x07,0x10,0x0C,0x32,0x34,0x0F};
  sendCmdData(0xE1, e1, sizeof(e1));
  sendCmd(0x11);
  delay(120);
  sendCmd(0x29);
}

static void prepPins() {
  pinMode(LCD_EN, OUTPUT);
  pinMode(LCD_BL, OUTPUT);
  pinMode(LCD_CS, OUTPUT);
  pinMode(LCD_DC, OUTPUT);
  digitalWrite(LCD_EN, HIGH);
  digitalWrite(LCD_BL, HIGH);
  digitalWrite(LCD_CS, HIGH);
  digitalWrite(LCD_DC, HIGH);
  pinMode(42, OUTPUT);
  digitalWrite(42, HIGH);
  delay(50);
}

static void showMessage(uint16_t bg, uint16_t fg, const char *line1, const char *line2 = nullptr) {
  tft.fillScreen(bg);
  tft.setRotation(0);
  tft.setTextWrap(false);
  tft.setTextColor(fg);
  tft.setTextSize(2);
  tft.setCursor(16, 40);
  tft.println(line1);
  if (line2) {
    tft.setCursor(16, 80);
    tft.println(line2);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
#if defined(ARDUINO_USB_CDC_ON_BOOT)
  delay(3000);
#endif

  prepPins();
  lcdSpi.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, -1);
  tft.begin(40000000);
  applyVendorInit();
  tft.setRotation(0);
  showMessage(ILI9341_RED, ILI9341_WHITE, "Display test", "Starting...");
  delay(400);
  showMessage(ILI9341_GREEN, ILI9341_BLACK, "Display test", "WiFi manager...");
  delay(400);
  showMessage(ILI9341_BLUE, ILI9341_WHITE, "Display test", "Init OK");

  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  bool connected = wm.autoConnect(CAPTIVE_SSID);
  if (connected) {
    showMessage(ILI9341_BLACK, ILI9341_GREEN, "WiFi connected", WiFi.localIP().toString().c_str());
  } else {
    showMessage(ILI9341_BLACK, ILI9341_YELLOW, "WiFi not set", "Portal timeout");
  }
}

void loop() {
  static uint32_t last = 0;
  static bool on = false;
  if (millis() - last > 1000) {
    last = millis();
    on = !on;
    tft.fillRect(200, 8, 24, 24, on ? ILI9341_CYAN : ILI9341_DARKGREY);
  }
}
