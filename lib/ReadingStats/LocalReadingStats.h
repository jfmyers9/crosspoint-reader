#pragma once

#include <string>

#include "LocalReadingStatsModel.h"

class LocalReadingStats {
 public:
  using DayTotal = LocalStats::DayTotal;
  struct Summary {
    bool clockValid = false;
    bool storageHealthy = false;
    uint32_t totalSeconds = 0;
    uint32_t todaySeconds = 0;
    uint32_t bookSeconds = 0;
    DayTotal daily[LocalStats::DAYS]{};  // Today first, including zero-reading days.
    DayTotal bookDaily[LocalStats::DAYS]{};
  };
  static void beginBook(const std::string& path);
  static void showPage(float progress);
  static void suspend();
  static void pause();
  static void checkpoint();
  static void resetPace();
  static void breakSequence();
  // RAM-only; call checkpoint before opening the dashboard to refresh its date.
  static void getSummary(Summary& out);
  static bool estimateSeconds(float remainingProgress, uint32_t& seconds);
};
