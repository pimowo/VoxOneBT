#include <cassert>
#include <cstdint>
#include <cstdio>

#include "update/OtaBootHealthPolicy.h"
#include "update/OtaStatus.h"

int main() {
  using Health = OtaBootHealthPolicy::Status;
  using Partition = OtaPartitionState;
  assert(otaStatusFor(Health::NotPending, Partition::NoRecord) ==
         OtaStatus::NotPending);
  assert(otaStatusFor(Health::Pending, Partition::PendingVerify) ==
         OtaStatus::PendingVerify);
  assert(otaStatusFor(Health::WaitingHealth, Partition::PendingVerify) ==
         OtaStatus::PendingVerify);
  assert(otaStatusFor(Health::Confirmed, Partition::Valid) == OtaStatus::Valid);
  assert(otaStatusFor(Health::Confirmed, Partition::PendingVerify) ==
         OtaStatus::PendingVerify);
  assert(otaStatusFor(Health::Confirmed, Partition::ReadFailed) ==
         OtaStatus::Unknown);
  assert(otaStatusFor(Health::ConfirmFailed, Partition::PendingVerify) ==
         OtaStatus::ConfirmFailed);
  assert(otaStatusFor(Health::NotPending, Partition::Other) ==
         OtaStatus::Unknown);
  assert(otaStatusFor(Health::NotPending, Partition::ReadFailed) ==
         OtaStatus::Unknown);

  OtaBootHealthPolicy normal;
  normal.begin(false);
  normal.markApplicationReady(100);
  assert(normal.status() == OtaBootHealthPolicy::Status::NotPending);
  assert(!normal.shouldConfirm(5100));

  OtaBootHealthPolicy pending;
  pending.begin(true);
  assert(pending.status() == OtaBootHealthPolicy::Status::Pending);
  assert(!pending.shouldConfirm(5100));  // Grace alone is insufficient.
  pending.markApplicationReady(300);
  assert(pending.status() == OtaBootHealthPolicy::Status::WaitingHealth);
  assert(!pending.shouldConfirm(5299));
  pending.noteMainCommunication();
  assert(pending.hasMainCommunication());
  assert(!pending.shouldConfirm(5299));  // MAIN cannot bypass the grace period.
  assert(pending.shouldConfirm(5300));
  assert(pending.status() == OtaBootHealthPolicy::Status::Confirming);
  assert(!pending.shouldConfirm(5301));
  pending.finishConfirmation(true);
  assert(pending.status() == OtaBootHealthPolicy::Status::Confirmed);
  assert(!pending.shouldConfirm(10000));

  OtaBootHealthPolicy noMain;
  noMain.begin(true);
  noMain.markApplicationReady(1000);
  assert(!noMain.shouldConfirm(5000));
  assert(noMain.shouldConfirm(6000));  // MAIN contact is optional.

  OtaBootHealthPolicy noReady;
  noReady.begin(true);
  assert(!noReady.shouldConfirm(5000));
  assert(!noReady.shouldConfirm(100000));

  OtaBootHealthPolicy failure;
  failure.begin(true);
  failure.markApplicationReady(0);
  assert(failure.shouldConfirm(5000));
  failure.finishConfirmation(false);
  assert(failure.status() == OtaBootHealthPolicy::Status::ConfirmFailed);
  assert(!failure.shouldConfirm(6000));

  OtaBootHealthPolicy wrap;
  wrap.begin(true);
  wrap.markApplicationReady(UINT32_MAX - 1000U);
  assert(!wrap.shouldConfirm(3998));
  assert(wrap.shouldConfirm(3999));

  std::puts("OtaBootHealthTest PASS");
}
