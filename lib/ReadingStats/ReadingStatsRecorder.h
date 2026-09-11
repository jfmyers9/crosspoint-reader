#pragma once

#include <cstdint>

struct ReadingStatsEvent {
  uint64_t startMonotonicMs = 0;
  int64_t startEpochSeconds = 0;  // Zero means no trusted UTC anchor in this boot.
  uint32_t page = 0;
  uint32_t totalPages = 0;
  uint32_t durationSeconds = 0;
};

class ReadingStatsRecorder {
 public:
  using Sink = bool (*)(void*, const ReadingStatsEvent&);

  explicit ReadingStatsRecorder(Sink sink, void* context, uint32_t idleSeconds = 120, uint32_t minDwellSeconds = 5);
  // Times must come from one non-wrapping monotonic clock, in milliseconds.
  bool showPage(uint32_t page, uint32_t totalPages, uint64_t nowMs);
  bool pause(uint64_t nowMs);
  // Rendering suspension preserves the current page's idle deadline.
  bool suspend(uint64_t nowMs);
  bool checkpoint(uint64_t nowMs);
  void setTimeAnchor(uint64_t nowMs, int64_t epochSeconds);

 private:
  Sink sink;
  void* context;
  uint32_t idleSeconds;
  uint32_t minDwellSeconds;
  uint64_t anchorMs = 0;
  int64_t anchorEpoch = 0;
  uint64_t openedMs = 0;
  uint64_t sliceMs = 0;
  int64_t sliceEpoch = 0;
  uint32_t page = 0;
  uint32_t totalPages = 0;
  bool active = false;
  bool closing = false;
  bool suspended = false;
  uint64_t closedMs = 0;
  bool hasPending = false;
  ReadingStatsEvent pending;

  int64_t epochAt(uint64_t timeMs) const;
  bool drain();
  bool finalize(uint64_t nowMs, bool close);
};
