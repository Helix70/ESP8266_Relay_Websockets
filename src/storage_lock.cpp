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

bool acquireStorageWriteLockWithBackoff(uint32_t waitMs, uint32_t backoffMs, uint32_t leaseMs)
{
  uint32_t started = millis();

  while ((uint32_t)(millis() - started) <= waitMs)
  {
    uint32_t nowMs = millis();
    if (tryAcquireStorageWriteLock(nowMs, leaseMs))
    {
      return true;
    }

    delay(backoffMs);
    yield();
  }

  return false;
}

void releaseStorageWriteLock()
{
  noInterrupts();
  gStorageWriteLock = false;
  gStorageWriteLockAtMs = 0;
  interrupts();
}
