#pragma once

#include <Arduino.h>

bool acquireStorageWriteLockWithBackoff(uint32_t waitMs = 800,
                                        uint32_t backoffMs = 25,
                                        uint32_t leaseMs = 3000);
void releaseStorageWriteLock();

class StorageWriteLockGuard {
public:
  StorageWriteLockGuard(uint32_t waitMs = 800, uint32_t backoffMs = 25,
                        uint32_t leaseMs = 3000)
      : acquired_(
            acquireStorageWriteLockWithBackoff(waitMs, backoffMs, leaseMs)) {}

  ~StorageWriteLockGuard() {
    if (acquired_) {
      releaseStorageWriteLock();
    }
  }

  bool acquired() const { return acquired_; }

private:
  bool acquired_;
};
