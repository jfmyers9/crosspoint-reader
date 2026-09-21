#pragma once

#include <cstdint>
#include <ctime>

namespace LocalStats {
constexpr unsigned DAYS = 30;
struct DayTotal {
  int32_t dateKey = 0;  // Local YYYYMMDD; zero is an unused slot.
  uint32_t seconds = 0;
};
struct Totals {
  uint32_t seconds = 0;
  DayTotal daily[DAYS]{};
};
uint32_t saturatedAdd(uint32_t a, uint32_t b);
int32_t dateKey(int64_t epoch);
void add(Totals& totals, int64_t epoch, uint32_t seconds);
void recentDays(const Totals& totals, int64_t now, DayTotal* output);

class Pace {
 public:
  void reset();
  void breakSequence();
  void credit(uint32_t seconds);
  void show(uint32_t progress);
  bool estimate(float remainingProgress, uint32_t& seconds) const;

 private:
  uint32_t previous = 0;
  uint32_t dwell = 0;
  uint32_t pacedSeconds = 0;
  uint32_t units = 0;
  uint32_t transitions = 0;
  bool active = false;
};
}  // namespace LocalStats
