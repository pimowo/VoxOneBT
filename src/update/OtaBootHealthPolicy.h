#pragma once

#include <stdint.h>

class OtaBootHealthPolicy {
 public:
  enum class Status {
    NotPending,
    Pending,
    WaitingHealth,
    Confirming,
    Confirmed,
    ConfirmFailed,
  };

  static constexpr uint32_t GracePeriodMs = 5000U;

  void begin(bool pending) {
    status_ = pending ? Status::Pending : Status::NotPending;
    readyMs_ = 0;
    applicationReady_ = false;
    mainCommunication_ = false;
  }

  void markApplicationReady(uint32_t nowMs) {
    if (status_ == Status::Pending) {
      applicationReady_ = true;
      readyMs_ = nowMs;
      status_ = Status::WaitingHealth;
    }
  }

  // Optional evidence for a future policy; MAIN availability is not required.
  void noteMainCommunication() { mainCommunication_ = true; }
  bool hasMainCommunication() const { return mainCommunication_; }

  bool shouldConfirm(uint32_t nowMs) {
    if (status_ != Status::WaitingHealth || !applicationReady_ ||
        static_cast<uint32_t>(nowMs - readyMs_) < GracePeriodMs) {
      return false;
    }
    status_ = Status::Confirming;
    return true;
  }

  void finishConfirmation(bool success) {
    if (status_ == Status::Confirming) {
      status_ = success ? Status::Confirmed : Status::ConfirmFailed;
    }
  }

  Status status() const { return status_; }

 private:
  Status status_ = Status::NotPending;
  uint32_t readyMs_ = 0;
  bool applicationReady_ = false;
  bool mainCommunication_ = false;
};
