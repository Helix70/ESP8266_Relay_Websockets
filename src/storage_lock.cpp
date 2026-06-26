#include "storage_lock.h"

namespace
{
volatile bool gStorageWriteLock = false;
volatile uint32_t gStorageWriteLockAtMs = 0;
volatile uint32_t gStorageWriteLockLeaseMs = 3000;

bool tryAcquireStorageWriteLock(uint32_t nowMs, uint32_t leaseMs)
{
  bool acquired = false;

  noInterrupts();
  bool lockIsStale = gStorageWriteLock && ((uint32_t)(nowMs - gStorageWriteLockAtMs) > gStorageWriteLockLeaseMs);
  if (!gStorageWriteLock || lockIsStale)
  {
    gStorageWriteLock = true;
    gStorageWriteLockAtMs = nowMs;
    gStorageWriteLockLeaseMs = leaseMs;
    acquired = true;
  }
  interrupts();

  return acquired;
}
}

bool acquireStorageWriteLock(uint32_t leaseMs)
{
  return tryAcquireStorageWriteLock(millis(), leaseMs);
}

void releaseStorageWriteLock()
{
  noInterrupts();
  gStorageWriteLock = false;
  gStorageWriteLockAtMs = 0;
  interrupts();
}
