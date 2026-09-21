#pragma once
#include <cstdint>
struct HalClockStub {
  bool valid = true;
  int64_t epoch = 1800000000;
  int ntpCalls = 0;
  bool getUtcTime(int64_t& result) const {
    if (valid) result = epoch;
    return valid;
  }
  void syncFromNTP() { ++ntpCalls; }
};
extern HalClockStub halClock;
