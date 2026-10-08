#include "update/FirmwareOtaWriter.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace FirmwareOta {

Partitions ArduinoOtaBackend::partitions() {
  Partitions result;
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (running != nullptr) {
    result.runningPresent = true;
    result.runningAddress = running->address;
  }
  if (next != nullptr) {
    result.nextPresent = true;
    result.nextAddress = next->address;
    result.nextSize = next->size;
    result.nextIsOtaApp =
        next->type == ESP_PARTITION_TYPE_APP &&
        next->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
        next->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
  }
  return result;
}

bool ArduinoOtaBackend::begin(uint32_t size) {
  return Update.begin(size, U_FLASH);
}

size_t ArduinoOtaBackend::write(const uint8_t* data, size_t length) {
  // Arduino Update 2.0.17 copies input into its own sector buffer.
  return Update.write(const_cast<uint8_t*>(data), length);
}

bool ArduinoOtaBackend::end() {
  return Update.end(false) && !Update.hasError();
}

void ArduinoOtaBackend::abort() {
  if (Update.isRunning()) Update.abort();
}

uint8_t ArduinoOtaBackend::rawError() const { return Update.getError(); }

}  // namespace FirmwareOta
