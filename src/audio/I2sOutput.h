#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

class I2sOutput {
 public:
  bool setSampleRate(uint32_t sampleRate);
  void setActive(bool active);
  void write(const uint8_t* data, size_t length);
  void loop();
  void end();

 private:
  static constexpr i2s_port_t PORT = I2S_NUM_0;
  static constexpr int DMA_BUFFER_COUNT = 8;
  static constexpr int DMA_BUFFER_FRAMES = 256;
  static constexpr TickType_t WRITE_TIMEOUT = pdMS_TO_TICKS(20);

  bool install(uint32_t sampleRate);
  void flagWriteError();

  SemaphoreHandle_t mutex_ = nullptr;
  bool installed_ = false;
  bool active_ = false;
  uint32_t sampleRate_ = 0;
  volatile bool writeErrorPending_ = false;
  volatile bool writeErrorLatched_ = false;
};
