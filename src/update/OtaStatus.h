#pragma once

#include "update/OtaBootHealthPolicy.h"

// The raw state is read from the running partition for each GET_STATUS.
enum class OtaPartitionState { NoRecord, PendingVerify, Valid, Other, ReadFailed };
enum class OtaStatus { NotPending, PendingVerify, Valid, ConfirmFailed, Unknown };

constexpr OtaStatus otaStatusFor(OtaBootHealthPolicy::Status health,
                                  OtaPartitionState partition) {
  return health == OtaBootHealthPolicy::Status::ConfirmFailed
             ? OtaStatus::ConfirmFailed
         : partition == OtaPartitionState::Valid ? OtaStatus::Valid
         : partition == OtaPartitionState::PendingVerify
             ? OtaStatus::PendingVerify
         : partition == OtaPartitionState::NoRecord &&
                   health == OtaBootHealthPolicy::Status::NotPending
             ? OtaStatus::NotPending
             : OtaStatus::Unknown;
}

constexpr const char* otaStatusToken(OtaStatus state) {
  return state == OtaStatus::NotPending ? "NOT_PENDING"
       : state == OtaStatus::PendingVerify ? "PENDING_VERIFY"
       : state == OtaStatus::Valid ? "VALID"
       : state == OtaStatus::ConfirmFailed ? "CONFIRM_FAILED"
       : "UNKNOWN";
}
