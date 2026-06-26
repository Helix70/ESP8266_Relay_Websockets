#pragma once

#include <Arduino.h>

bool acquireStorageWriteLock(uint32_t leaseMs = 3000);
void releaseStorageWriteLock();

class StorageWriteLockGuard {
public:
  explicit StorageWriteLockGuard(uint32_t leaseMs = 3000)
      : acquired_(acquireStorageWriteLock(leaseMs)) {}

  ~StorageWriteLockGuard() {
    if (acquired_) {
      releaseStorageWriteLock();
    }
  }

  bool acquired() const { return acquired_; }

private:
  bool acquired_;
};
