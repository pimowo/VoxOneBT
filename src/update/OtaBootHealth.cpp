#include "update/OtaBootHealth.h"

#include <esp_ota_ops.h>
#include <sdkconfig.h>
#include <stdio.h>

#include "diagnostics/Logger.h"

// Arduino-ESP32 2.0.17 defines this weak hook in esp32-hal-misc.c. Its C
// linkage is required to replace the framework symbol from C++.
#ifdef CONFIG_APP_ROLLBACK_ENABLE
extern "C" bool verifyRollbackLater(void) { return true; }
#endif

void OtaBootHealth::begin() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState{};
  if (running == nullptr) {
    imageState_ = ImageState::InspectionFailed;
    policy_.begin(false);
    return;
  }
  const esp_err_t stateResult = esp_ota_get_state_partition(running, &otaState);
  if (stateResult == ESP_ERR_NOT_SUPPORTED || stateResult == ESP_ERR_NOT_FOUND) {
    imageState_ = ImageState::Normal;  // Factory app or no OTA state record.
    policy_.begin(false);
    return;
  }
  if (stateResult != ESP_OK) {
    imageState_ = ImageState::InspectionFailed;
    policy_.begin(false);
    return;
  }

  if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
    imageState_ = ImageState::PendingVerify;
    policy_.begin(true);
    Logger::info("OTA pending verification");
  } else {
    imageState_ = otaState == ESP_OTA_IMG_VALID || otaState == ESP_OTA_IMG_UNDEFINED
                      ? ImageState::Normal
                      : ImageState::Other;
    policy_.begin(false);
  }
}

void OtaBootHealth::loop(uint32_t nowMs) {
  if (!policy_.shouldConfirm(nowMs)) {
    return;
  }

  Logger::info("OTA health check passed");
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  policy_.finishConfirmation(result == ESP_OK);
  if (result == ESP_OK) {
    if (snapshotStatus() == OtaStatus::Valid)
      Logger::info("OTA image confirmed VALID");
    else
      Logger::error("OTA confirm returned OK but state is not VALID");
  } else {
    char message[48];
    snprintf(message, sizeof(message), "OTA confirm failed: 0x%X",
             static_cast<unsigned int>(result));
    Logger::error(message);
  }
}

OtaStatus OtaBootHealth::snapshotStatus() const {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr)
    return otaStatusFor(policy_.status(), OtaPartitionState::ReadFailed);

  esp_ota_img_states_t state{};
  const esp_err_t result = esp_ota_get_state_partition(running, &state);
  OtaPartitionState partition = OtaPartitionState::ReadFailed;
  if (result == ESP_ERR_NOT_SUPPORTED || result == ESP_ERR_NOT_FOUND)
    partition = OtaPartitionState::NoRecord;
  else if (result == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
    partition = OtaPartitionState::PendingVerify;
  else if (result == ESP_OK && state == ESP_OTA_IMG_VALID)
    partition = OtaPartitionState::Valid;
  else if (result == ESP_OK && state == ESP_OTA_IMG_UNDEFINED)
    partition = OtaPartitionState::NoRecord;
  else if (result == ESP_OK)
    partition = OtaPartitionState::Other;
  return otaStatusFor(policy_.status(), partition);
}
