#pragma once

#include <cstddef>
#include <cstdint>

struct ReadingStatsEvent;

class BookOrbitStatsClient {
 public:
  static constexpr size_t MAX_BATCH_EVENTS = 8;
  static bool upload(const char* bookHash, const ReadingStatsEvent* events, size_t count);
  static bool lastUploadWasUnmatched();
  static bool completeSweep(uint32_t uploaded, uint32_t booksMatched = 0);
  // Empty if hardware identity could not be read.
  static const char* deviceId();
};
