#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <ctype.h>

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

class DisplayManager {
 public:
  DisplayManager()
      : spi(FSPI),
        started(false),
        currentDcPin(LCD_DC),
        activeColor(0x0000),
        heartbeatOn(false),
        lastHeartbeatMs(0) {}

  bool begin() {
    prepPins();
    spi.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, -1);
    delay(50);

    applyVendorInit();
    fillScreen(0x0000);
    started = true;
    return true;
  }

  void showBoot(const char *line2 = nullptr) {
    showPattern(0x001F);
    drawStatusText("BOOTING", line2 ? String(line2) : String(""));
  }

  void showWiFiProvisioning() {
    showPattern(0xFD20);
    drawStatusText("WIFI SETUP", "OPEN 192.168.4.1");
  }

  void showWiFiConnected(const String &ip) {
    showPattern(0x07E0);
    drawStatusText("WIFI CONNECTED", ip);
  }

  void showRegistration(const char *statusLine) {
    showPattern(0x07FF);
    drawStatusText("REGISTERING", statusLine ? String(statusLine) : String(""));
  }

  void showReady(const String &deviceId) {
    showPattern(0xFFFF);
    String line2 = String("ID ") + deviceId;
    drawStatusText("READY", line2);
  }

  void showError(const char *line1, const char *line2 = nullptr) {
    showPattern(0xF800);
    drawStatusText(line1 ? String(line1) : String("ERROR"), line2 ? String(line2) : String(""));
  }

  void showAppScreen(const String &title,
                     const String &line1,
                     const String &line2,
                     const String &line3,
                     uint16_t accentColor) {
    if (!started) {
      return;
    }

    activeColor = 0x0000;
    heartbeatOn = false;
    lastHeartbeatMs = millis();

    fillScreen(0x0000);
    fillRect(0, 0, 240, 28, accentColor);
    uint16_t titleColor = textColorForBackground(accentColor);
    drawText(title, 8, 7, titleColor, accentColor, 2, 18);

    drawText(line1, 10, 48, 0xFFFF, 0x0000, 2, 19);
    drawText(line2, 10, 82, 0xFFFF, 0x0000, 2, 19);
    drawText(line3, 10, 116, 0x7BEF, 0x0000, 2, 19);
  }

  bool isStarted() const {
    return started;
  }

  void tick() {
    if (!started) {
      return;
    }

    const unsigned long now = millis();
    if ((now - lastHeartbeatMs) < 500) {
      return;
    }
    lastHeartbeatMs = now;
    heartbeatOn = !heartbeatOn;

    setAddrWindow(0, 0, 239, 23);
    pushColor(heartbeatOn ? 0xFFFF : 0x0000, 240UL * 24UL);
  }

 private:
  SPIClass spi;
  bool started;
  int currentDcPin;
  uint16_t activeColor;
  bool heartbeatOn;
  unsigned long lastHeartbeatMs;

  static const uint8_t* glyph(char c) {
    static const uint8_t gSpace[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t gDash[5]  = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t gDot[5]   = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t gColon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t gSlash[5] = {0x20,0x10,0x08,0x04,0x02};

    static const uint8_t g0[5] = {0x3E,0x51,0x49,0x45,0x3E};
    static const uint8_t g1[5] = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t g2[5] = {0x62,0x51,0x49,0x49,0x46};
    static const uint8_t g3[5] = {0x22,0x49,0x49,0x49,0x36};
    static const uint8_t g4[5] = {0x18,0x14,0x12,0x7F,0x10};
    static const uint8_t g5[5] = {0x2F,0x49,0x49,0x49,0x31};
    static const uint8_t g6[5] = {0x3E,0x49,0x49,0x49,0x32};
    static const uint8_t g7[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t g8[5] = {0x36,0x49,0x49,0x49,0x36};
    static const uint8_t g9[5] = {0x26,0x49,0x49,0x49,0x3E};

    static const uint8_t gA[5] = {0x7E,0x11,0x11,0x11,0x7E};
    static const uint8_t gB[5] = {0x7F,0x49,0x49,0x49,0x36};
    static const uint8_t gC[5] = {0x3E,0x41,0x41,0x41,0x22};
    static const uint8_t gD[5] = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t gE[5] = {0x7F,0x49,0x49,0x49,0x41};
    static const uint8_t gF[5] = {0x7F,0x09,0x09,0x09,0x01};
    static const uint8_t gG[5] = {0x3E,0x41,0x49,0x49,0x3A};
    static const uint8_t gH[5] = {0x7F,0x08,0x08,0x08,0x7F};
    static const uint8_t gI[5] = {0x00,0x41,0x7F,0x41,0x00};
    static const uint8_t gJ[5] = {0x20,0x40,0x41,0x3F,0x01};
    static const uint8_t gK[5] = {0x7F,0x08,0x14,0x22,0x41};
    static const uint8_t gL[5] = {0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t gM[5] = {0x7F,0x02,0x0C,0x02,0x7F};
    static const uint8_t gN[5] = {0x7F,0x04,0x08,0x10,0x7F};
    static const uint8_t gO[5] = {0x3E,0x41,0x41,0x41,0x3E};
    static const uint8_t gP[5] = {0x7F,0x09,0x09,0x09,0x06};
    static const uint8_t gQ[5] = {0x3E,0x41,0x51,0x21,0x5E};
    static const uint8_t gR[5] = {0x7F,0x09,0x19,0x29,0x46};
    static const uint8_t gS[5] = {0x46,0x49,0x49,0x49,0x31};
    static const uint8_t gT[5] = {0x01,0x01,0x7F,0x01,0x01};
    static const uint8_t gU[5] = {0x3F,0x40,0x40,0x40,0x3F};
    static const uint8_t gV[5] = {0x1F,0x20,0x40,0x20,0x1F};
    static const uint8_t gW[5] = {0x3F,0x40,0x38,0x40,0x3F};
    static const uint8_t gX[5] = {0x63,0x14,0x08,0x14,0x63};
    static const uint8_t gY[5] = {0x07,0x08,0x70,0x08,0x07};
    static const uint8_t gZ[5] = {0x61,0x51,0x49,0x45,0x43};

    switch (c) {
      case ' ': return gSpace;
      case '-': return gDash;
      case '.': return gDot;
      case ':': return gColon;
      case '/': return gSlash;
      case '0': return g0;
      case '1': return g1;
      case '2': return g2;
      case '3': return g3;
      case '4': return g4;
      case '5': return g5;
      case '6': return g6;
      case '7': return g7;
      case '8': return g8;
      case '9': return g9;
      case 'A': return gA;
      case 'B': return gB;
      case 'C': return gC;
      case 'D': return gD;
      case 'E': return gE;
      case 'F': return gF;
      case 'G': return gG;
      case 'H': return gH;
      case 'I': return gI;
      case 'J': return gJ;
      case 'K': return gK;
      case 'L': return gL;
      case 'M': return gM;
      case 'N': return gN;
      case 'O': return gO;
      case 'P': return gP;
      case 'Q': return gQ;
      case 'R': return gR;
      case 'S': return gS;
      case 'T': return gT;
      case 'U': return gU;
      case 'V': return gV;
      case 'W': return gW;
      case 'X': return gX;
      case 'Y': return gY;
      case 'Z': return gZ;
      default: return gSpace;
    }
  }

  uint16_t textColorForBackground(uint16_t bgColor) {
    if (bgColor == 0xFFFF || bgColor == 0x07E0 || bgColor == 0x07FF || bgColor == 0xFD20) {
      return 0x0000;
    }
    return 0xFFFF;
  }

  void drawChar(int x, int y, char input, uint16_t fg, uint16_t bg, uint8_t scale) {
    char c = static_cast<char>(toupper(static_cast<unsigned char>(input)));
    const uint8_t* glyphData = glyph(c);
    for (int col = 0; col < 5; col++) {
      uint8_t bits = glyphData[col];
      for (int row = 0; row < 7; row++) {
        uint16_t color = (bits & (1 << row)) ? fg : bg;
        fillRect(x + (col * scale), y + (row * scale), scale, scale, color);
      }
    }
    fillRect(x + (5 * scale), y, scale, 7 * scale, bg);
  }

  void drawText(const String &text, int x, int y, uint16_t fg, uint16_t bg, uint8_t scale, int maxChars) {
    int cursorX = x;
    int count = 0;
    for (size_t i = 0; i < text.length(); i++) {
      if (count >= maxChars) {
        break;
      }
      drawChar(cursorX, y, text[i], fg, bg, scale);
      cursorX += (6 * scale);
      count++;
    }
  }

  void drawStatusText(const String &line1, const String &line2) {
    uint16_t fg = textColorForBackground(activeColor);
    uint16_t bg = activeColor;

    drawText(line1, 10, 40, fg, bg, 3, 12);
    drawText(line2, 10, 84, fg, bg, 2, 18);
  }

  void prepPins() {
    pinMode(LCD_EN, OUTPUT);
    pinMode(LCD_BL, OUTPUT);
    pinMode(LCD_CS, OUTPUT);
    pinMode(currentDcPin, OUTPUT);

    digitalWrite(LCD_EN, HIGH);
    digitalWrite(LCD_BL, HIGH);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(currentDcPin, HIGH);
    delay(20);
  }

  inline void csLow() {
    digitalWrite(LCD_CS, LOW);
  }

  inline void csHigh() {
    digitalWrite(LCD_CS, HIGH);
  }

  inline void dcLow() {
    digitalWrite(currentDcPin, LOW);
  }

  inline void dcHigh() {
    digitalWrite(currentDcPin, HIGH);
  }

  void writeCmd(uint8_t cmd) {
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    csLow();
    dcLow();
    spi.transfer(cmd);
    csHigh();
    spi.endTransaction();
  }

  void writeData(uint8_t data) {
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    csLow();
    dcHigh();
    spi.transfer(data);
    csHigh();
    spi.endTransaction();
  }

  void applyVendorInit() {
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
    writeCmd(0xE0);
    const uint8_t e0[] = {0x0F,0x35,0x31,0x0B,0x0E,0x06,0x49,0xA7,0x33,0x07,0x0F,0x03,0x0C,0x0A,0x00};
    for (uint8_t value : e0) {
      writeData(value);
    }
    writeCmd(0xE1);
    const uint8_t e1[] = {0x00,0x0A,0x0F,0x04,0x11,0x08,0x36,0x58,0x4D,0x07,0x10,0x0C,0x32,0x34,0x0F};
    for (uint8_t value : e1) {
      writeData(value);
    }
    writeCmd(0x11);
    delay(120);
    writeCmd(0x29);
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
    spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    csLow();
    dcHigh();
    for (uint32_t index = 0; index < count; index++) {
      spi.transfer(hi);
      spi.transfer(lo);
    }
    csHigh();
    spi.endTransaction();
  }

  void fillScreen(uint16_t color) {
    setAddrWindow(0, 0, 239, 319);
    pushColor(color, 240UL * 320UL);
  }

  void fillRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) {
      return;
    }
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (x >= 240 || y >= 320) {
      return;
    }
    if ((x + w) > 240) {
      w = 240 - x;
    }
    if ((y + h) > 320) {
      h = 320 - y;
    }

    setAddrWindow(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                  static_cast<uint16_t>(x + w - 1), static_cast<uint16_t>(y + h - 1));
    pushColor(color, static_cast<uint32_t>(w) * static_cast<uint32_t>(h));
  }

  void showPattern(uint16_t color) {
    if (!started) {
      return;
    }

    activeColor = color;
    heartbeatOn = false;
    lastHeartbeatMs = millis();

    fillScreen(color);

    // Small top status bar for visual contrast (confirms drawing works)
    setAddrWindow(0, 0, 239, 23);
    pushColor(0x0000, 240UL * 24UL);
  }
};

#endif // DISPLAY_MANAGER_H
