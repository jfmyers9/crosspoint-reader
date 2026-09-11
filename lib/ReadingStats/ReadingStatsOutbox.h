#pragma once

#include "ReadingStatsRecorder.h"

class ReadingStatsOutbox {
 public:
  static constexpr uint8_t MAX_EVENTS = 8;
  struct Batch {
    char bookHash[33]{};
    char bootId[17]{};
    ReadingStatsEvent events[MAX_EVENTS]{};
    uint8_t count = 0;
  };

  // Caller serializes writes and creates scopeDir. Completed .bin files are immutable.
  static bool save(const char* scopeDir, const Batch& batch);
  // Output may be partially populated on failure; consume it only on success.
  static bool load(const char* path, Batch& batch);
};
