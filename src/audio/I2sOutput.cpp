#include "audio/I2sOutput.h"

#include <esp_intr_alloc.h>

#include "Pins.h"
#include "diagnostics/Logger.h"

bool I2sOutput::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    Logger::error("I2S mutex allocation failed");
    return false;
  }
  return true;
}

bool I2sOutput::install(uint32_t sampleRate) {
  i2s_config_t config{};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = sampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = DMA_BUFFER_COUNT;
  config.dma_buf_len = DMA_BUFFER_FRAMES;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  esp_err_t result = i2s_driver_install(PORT, &config, 0, nullptr);
  if (result != ESP_OK) {
    Logger::error("I2S driver install failed");
    return false;
  }

  i2s_pin_config_t pins{};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = Pins::I2S_BCLK;
  pins.ws_io_num = Pins::I2S_WS;
  pins.data_out_num = Pins::I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  result = i2s_set_pin(PORT, &pins);
  if (result != ESP_OK) {
    Logger::error("I2S pin configuration failed");
    i2s_driver_uninstall(PORT);
    return false;
  }

  installed_ = true;
  sampleRate_ = sampleRate;
  i2s_stop(PORT);
  i2s_zero_dma_buffer(PORT);
  Logger::info("I2S initialized");
  Logger::info("I2S sample rate:", sampleRate, "Hz");
  return true;
}

bool I2sOutput::setSampleRate(uint32_t sampleRate) {
  if (sampleRate == 0) {
    return false;
  }
  if (mutex_ == nullptr) {
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!installed_) {
    const bool success = install(sampleRate);
    xSemaphoreGive(mutex_);
    return success;
  }
  if (sampleRate_ == sampleRate) {
    xSemaphoreGive(mutex_);
    return true;
  }

  const bool resume = active_;
  i2s_stop(PORT);
  active_ = false;
  const esp_err_t result =
      i2s_set_clk(PORT, sampleRate, I2S_BITS_PER_SAMPLE_16BIT,
                  I2S_CHANNEL_STEREO);
  if (result == ESP_OK) {
    sampleRate_ = sampleRate;
    i2s_zero_dma_buffer(PORT);
    if (resume && i2s_start(PORT) == ESP_OK) {
      active_ = true;
    }
    Logger::info("I2S sample rate:", sampleRate, "Hz");
  } else {
    Logger::error("I2S sample-rate configuration failed");
  }
  xSemaphoreGive(mutex_);
  return result == ESP_OK;
}

void I2sOutput::setActive(bool active) {
  if (mutex_ == nullptr || !installed_) {
    return;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (active && !active_) {
    i2s_zero_dma_buffer(PORT);
    if (i2s_start(PORT) == ESP_OK) {
      active_ = true;
    } else {
      Logger::error("I2S start failed");
    }
  } else if (!active && active_) {
    i2s_stop(PORT);
    i2s_zero_dma_buffer(PORT);
    active_ = false;
    Logger::info("I2S stopped");
  }
  xSemaphoreGive(mutex_);
}

void I2sOutput::write(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 || mutex_ == nullptr) {
    return;
  }
  const TickType_t started = xTaskGetTickCount();
  if (xSemaphoreTake(mutex_, WRITE_TIMEOUT) != pdTRUE) {
    flagWriteError();
    return;
  }
  if (!installed_ || !active_) {
    xSemaphoreGive(mutex_);
    return;
  }

  size_t offset = 0;
  while (offset < length) {
    const TickType_t elapsed = xTaskGetTickCount() - started;
    if (elapsed >= WRITE_TIMEOUT) {
      flagWriteError();
      break;
    }
    size_t written = 0;
    const esp_err_t result =
        i2s_write(PORT, data + offset, length - offset, &written,
                  WRITE_TIMEOUT - elapsed);
    if (result != ESP_OK || written == 0) {
      flagWriteError();
      break;
    }
    offset += written;
  }
  xSemaphoreGive(mutex_);
}

void I2sOutput::flagWriteError() {
  bool expected = false;
  if (__atomic_compare_exchange_n(&writeErrorLatched_, &expected, true, false,
                                  __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    __atomic_store_n(&writeErrorPending_, true, __ATOMIC_RELEASE);
  }
}

void I2sOutput::loop() {
  if (__atomic_exchange_n(&writeErrorPending_, false, __ATOMIC_ACQUIRE)) {
    Logger::error("I2S PCM write failed or timed out");
  }
}

void I2sOutput::end() {
  if (mutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (installed_) {
    i2s_stop(PORT);
    i2s_zero_dma_buffer(PORT);
    i2s_driver_uninstall(PORT);
    installed_ = false;
    active_ = false;
    sampleRate_ = 0;
    Logger::info("I2S stopped");
  }
  xSemaphoreGive(mutex_);
}
