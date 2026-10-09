#pragma once

#include <stdint.h>

#include "update/OtaBootHealthPolicy.h"
#include "update/OtaStatus.h"

class OtaBootHealth {
 public:
  enum class ImageState { Normal, PendingVerify, Other, InspectionFailed };

  void begin();
  void markApplicationReady(uint32_t nowMs) { policy_.markApplicationReady(nowMs); }
  void noteMainCommunication() { policy_.noteMainCommunication(); }
  void loop(uint32_t nowMs);

  ImageState imageState() const { return imageState_; }
  OtaBootHealthPolicy::Status status() const { return policy_.status(); }
  OtaStatus snapshotStatus() const;

 private:
  OtaBootHealthPolicy policy_;
  ImageState imageState_ = ImageState::InspectionFailed;
};
