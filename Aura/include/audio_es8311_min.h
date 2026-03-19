#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "driver/i2s.h"

#ifndef AUDIO_AMP_ENABLE_PIN
#define AUDIO_AMP_ENABLE_PIN 1
#endif
#ifndef AUDIO_I2S_MCLK_PIN
#define AUDIO_I2S_MCLK_PIN 4
#endif
#ifndef AUDIO_I2S_BCLK_PIN
#define AUDIO_I2S_BCLK_PIN 5
#endif
#ifndef AUDIO_I2S_DIN_PIN
#define AUDIO_I2S_DIN_PIN 6
#endif
#ifndef AUDIO_I2S_WS_PIN
#define AUDIO_I2S_WS_PIN 7
#endif
#ifndef AUDIO_I2S_DOUT_PIN
#define AUDIO_I2S_DOUT_PIN 8
#endif
#ifndef AUDIO_CODEC_SDA_PIN
#define AUDIO_CODEC_SDA_PIN 16
#endif
#ifndef AUDIO_CODEC_SCL_PIN
#define AUDIO_CODEC_SCL_PIN 15
#endif
#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE 8000
#endif
#ifndef AUDIO_MCLK_MULTIPLE
#define AUDIO_MCLK_MULTIPLE 384
#endif

namespace aura_audio {

static constexpr uint8_t ES8311_ADDR0 = 0x18;
static constexpr uint8_t ES8311_ADDR1 = 0x19;

static constexpr uint8_t REG00_RESET = 0x00;
static constexpr uint8_t REG01_CLKMGR1 = 0x01;
static constexpr uint8_t REG02_CLKMGR2 = 0x02;
static constexpr uint8_t REG03_CLKMGR3 = 0x03;
static constexpr uint8_t REG04_CLKMGR4 = 0x04;
static constexpr uint8_t REG05_CLKMGR5 = 0x05;
static constexpr uint8_t REG06_CLKMGR6 = 0x06;
static constexpr uint8_t REG07_CLKMGR7 = 0x07;
static constexpr uint8_t REG08_CLKMGR8 = 0x08;
static constexpr uint8_t REG09_SDPIN = 0x09;
static constexpr uint8_t REG0A_SDPOUT = 0x0A;
static constexpr uint8_t REG0D_SYSTEM = 0x0D;
static constexpr uint8_t REG0E_SYSTEM = 0x0E;
static constexpr uint8_t REG12_SYSTEM = 0x12;
static constexpr uint8_t REG13_SYSTEM = 0x13;
static constexpr uint8_t REG14_SYSTEM = 0x14;
static constexpr uint8_t REG16_ADC = 0x16;
static constexpr uint8_t REG17_ADC = 0x17;
static constexpr uint8_t REG1C_ADC = 0x1C;
static constexpr uint8_t REG31_DAC = 0x31;
static constexpr uint8_t REG32_DAC = 0x32;
static constexpr uint8_t REG37_DAC = 0x37;

struct AudioState {
  int codecAddress = -1;
  bool codecReady = false;
  bool i2sReady = false;
  String status = "Audio: idle";
};

inline bool probeCodecAddress(TwoWire &wire, int &addrOut) {
  for (uint8_t addr : {ES8311_ADDR0, ES8311_ADDR1}) {
    wire.beginTransmission(addr);
    if (wire.endTransmission() == 0) {
      addrOut = addr;
      return true;
    }
  }
  addrOut = -1;
  return false;
}

inline bool writeReg(TwoWire &wire, uint8_t addr, uint8_t reg, uint8_t value) {
  wire.beginTransmission(addr);
  wire.write(reg);
  wire.write(value);
  return wire.endTransmission() == 0;
}

inline bool readReg(TwoWire &wire, uint8_t addr, uint8_t reg, uint8_t &value) {
  wire.beginTransmission(addr);
  wire.write(reg);
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom((int)addr, 1) != 1) return false;
  value = wire.read();
  return true;
}

inline bool initCodec(TwoWire &wire, AudioState &state) {
  if (state.codecReady) return true;
  if (state.codecAddress < 0 && !probeCodecAddress(wire, state.codecAddress)) {
    state.status = "Audio: ES8311 not found on I2C";
    return false;
  }

  const uint8_t addr = (uint8_t)state.codecAddress;
  uint8_t reg00 = 0;
  if (!writeReg(wire, addr, REG00_RESET, 0x1F)) return false;
  delay(20);
  if (!writeReg(wire, addr, REG00_RESET, 0x00)) return false;
  if (!writeReg(wire, addr, REG00_RESET, 0x80)) return false;

  if (!writeReg(wire, addr, REG01_CLKMGR1, 0x3F)) return false;
  if (!writeReg(wire, addr, REG02_CLKMGR2, 0x00)) return false;
  if (!writeReg(wire, addr, REG03_CLKMGR3, 0x10)) return false;
  if (!writeReg(wire, addr, REG04_CLKMGR4, 0x10)) return false;
  if (!writeReg(wire, addr, REG05_CLKMGR5, 0x00)) return false;
  if (!readReg(wire, addr, REG06_CLKMGR6, reg00)) reg00 = 0x00;
  reg00 &= 0xE0;
  reg00 |= 0x03;
  if (!writeReg(wire, addr, REG06_CLKMGR6, reg00)) return false;
  if (!writeReg(wire, addr, REG07_CLKMGR7, 0x00)) return false;
  if (!writeReg(wire, addr, REG08_CLKMGR8, 0xFF)) return false;

  if (!readReg(wire, addr, REG00_RESET, reg00)) reg00 = 0x80;
  reg00 &= 0xBF;
  if (!writeReg(wire, addr, REG00_RESET, reg00)) return false;
  if (!writeReg(wire, addr, REG09_SDPIN, 0x0C)) return false;
  if (!writeReg(wire, addr, REG0A_SDPOUT, 0x0C)) return false;

  if (!writeReg(wire, addr, REG0D_SYSTEM, 0x01)) return false;
  if (!writeReg(wire, addr, REG0E_SYSTEM, 0x02)) return false;
  if (!writeReg(wire, addr, REG12_SYSTEM, 0x00)) return false;
  if (!writeReg(wire, addr, REG13_SYSTEM, 0x10)) return false;
  if (!writeReg(wire, addr, REG1C_ADC, 0x6A)) return false;
  if (!writeReg(wire, addr, REG37_DAC, 0x08)) return false;
  if (!writeReg(wire, addr, REG17_ADC, 0xC8)) return false;
  if (!writeReg(wire, addr, REG14_SYSTEM, 0x1A)) return false;
  if (!writeReg(wire, addr, REG16_ADC, 0x00)) return false;
  if (!writeReg(wire, addr, REG31_DAC, 0x00)) return false;
  if (!writeReg(wire, addr, REG32_DAC, 0xD8)) return false;

  state.codecReady = true;
  state.status = String("Audio: ES8311 @0x") + String(addr, HEX) + " ready";
  return true;
}

inline bool initI2S(AudioState &state) {
  if (state.i2sReady) return true;

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  cfg.sample_rate = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = AUDIO_SAMPLE_RATE * AUDIO_MCLK_MULTIPLE;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = AUDIO_I2S_MCLK_PIN;
  pins.bck_io_num = AUDIO_I2S_BCLK_PIN;
  pins.ws_io_num = AUDIO_I2S_WS_PIN;
  pins.data_out_num = AUDIO_I2S_DOUT_PIN;
  pins.data_in_num = AUDIO_I2S_DIN_PIN;

  i2s_driver_uninstall(I2S_NUM_1);
  if (i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr) != ESP_OK) {
    state.status = "Audio: I2S install failed";
    return false;
  }
  if (i2s_set_pin(I2S_NUM_1, &pins) != ESP_OK) {
    state.status = "Audio: I2S pin setup failed";
    return false;
  }
  state.i2sReady = true;
  state.status = "Audio: I2S ready";
  return true;
}

inline int readI2SMicPercent(AudioState &state, uint16_t captureMs = 120) {
  if (!initI2S(state)) return -1;

  const uint32_t captureStart = millis();
  int16_t buffer[256 * 2];
  int64_t sum = 0;
  int64_t sumSq = 0;
  uint32_t count = 0;

  while ((millis() - captureStart) < captureMs) {
    size_t bytesRead = 0;
    if (i2s_read(I2S_NUM_1, buffer, sizeof(buffer), &bytesRead, pdMS_TO_TICKS(25)) != ESP_OK) {
      continue;
    }
    if (bytesRead < sizeof(int16_t) * 2) {
      continue;
    }

    size_t sampleCount = bytesRead / sizeof(int16_t);
    for (size_t i = 0; i + 1 < sampleCount; i += 2) {
      int32_t sample = buffer[i];
      sum += sample;
      sumSq += (int64_t)sample * (int64_t)sample;
      count++;
    }
  }

  if (count < 32) {
    return -1;
  }

  float mean = (float)sum / (float)count;
  float meanSq = (float)sumSq / (float)count;
  float var = meanSq - (mean * mean);
  if (var < 0.0f) var = 0.0f;
  float rms = sqrtf(var);

  if (rms < 18.0f) {
    return 0;
  }

  int pct = (int)((rms * 100.0f) / 900.0f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

inline bool playI2STone(AudioState &state, uint16_t frequency, uint16_t durationMs, int16_t amplitude = 14000) {
  if (!state.i2sReady) return false;

  const size_t frames = 256;
  int16_t buffer[frames * 2];
  uint32_t halfPeriod = max<uint32_t>(1, AUDIO_SAMPLE_RATE / max<uint32_t>(2, frequency * 2));
  uint32_t totalFrames = ((uint32_t)AUDIO_SAMPLE_RATE * durationMs) / 1000;
  uint32_t phase = 0;
  if (amplitude < 500) amplitude = 500;
  if (amplitude > 30000) amplitude = 30000;

  while (totalFrames > 0) {
    size_t chunk = totalFrames > frames ? frames : totalFrames;
    for (size_t i = 0; i < chunk; ++i) {
      int16_t sample = (phase / halfPeriod) % 2 == 0 ? amplitude : -amplitude;
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
      phase++;
    }
    size_t bytesWritten = 0;
    if (i2s_write(I2S_NUM_1, buffer, chunk * sizeof(int16_t) * 2, &bytesWritten, pdMS_TO_TICKS(1000)) != ESP_OK) {
      state.status = "Audio: I2S write failed";
      return false;
    }
    totalFrames -= chunk;
  }
  state.status = "Audio: codec tone sent";
  return true;
}

inline bool playPinScanTone(AudioState &state, int configuredPin) {
  const int candidates[] = {configuredPin, AUDIO_I2S_MCLK_PIN, AUDIO_I2S_BCLK_PIN, AUDIO_I2S_DIN_PIN, AUDIO_I2S_WS_PIN, AUDIO_I2S_DOUT_PIN};
  const int channel = 3;
  bool played = false;
  for (int pin : candidates) {
    if (pin < 0 || pin > 48) continue;
    pinMode(pin, OUTPUT);
    ledcSetup(channel, 1800, 8);
    ledcAttachPin(pin, channel);
    ledcWriteTone(channel, 1200);
    delay(140);
    ledcWrite(channel, 0);
    ledcDetachPin(pin);
    delay(40);
    played = true;
  }
  state.status = "Audio: fallback pin scan 4/5/6/7/8";
  return played;
}

} // namespace aura_audio
