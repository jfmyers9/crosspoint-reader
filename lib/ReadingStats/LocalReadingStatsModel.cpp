#include "LocalReadingStatsModel.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace LocalStats {
uint32_t saturatedAdd(uint32_t a, uint32_t b) { return b > UINT32_MAX - a ? UINT32_MAX : a + b; }
int32_t dateKey(int64_t epoch) {
  if (epoch <= 0) return 0;
  time_t time = static_cast<time_t>(epoch);
  tm local{};
  if (!localtime_r(&time, &local)) return 0;
  return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}
static void addDay(Totals& totals, int32_t key, uint32_t seconds) {
  if (!key) return;
  unsigned slot = 0;
  for (unsigned i = 0; i < DAYS; ++i) {
    if (totals.daily[i].dateKey == key) {
      totals.daily[i].seconds = saturatedAdd(totals.daily[i].seconds, seconds);
      return;
    }
    if (totals.daily[i].dateKey < totals.daily[slot].dateKey) slot = i;
  }
  if (key > totals.daily[slot].dateKey) totals.daily[slot] = {key, seconds};
}
void add(Totals& totals, int64_t epoch, uint32_t seconds) {
  totals.seconds = saturatedAdd(totals.seconds, seconds);
  if (epoch <= 0) return;
  // Recorder events are capped at 120 seconds. Grouping consecutive civil dates
  // handles midnight, DST and historical timezone offsets without assuming 24h days.
  int32_t key = dateKey(epoch);
  uint32_t count = 0;
  for (uint32_t i = 0; i < seconds; ++i) {
    const int32_t next = dateKey(epoch + i);
    if (next != key) {
      addDay(totals, key, count);
      key = next;
      count = 0;
    }
    ++count;
  }
  addDay(totals, key, count);
}
void recentDays(const Totals& totals, int64_t now, DayTotal* output) {
  time_t time = static_cast<time_t>(now);
  tm local{};
  if (now <= 0 || !localtime_r(&time, &local)) {
    for (unsigned i = 0; i < DAYS; ++i) output[i] = {};
    return;
  }
  local.tm_hour = 12;
  local.tm_min = local.tm_sec = 0;
  for (unsigned i = 0; i < DAYS; ++i) {
    local.tm_isdst = -1;
    time = mktime(&local);
    const int32_t key = dateKey(time);
    output[i] = {key, 0};
    for (const auto& day : totals.daily)
      if (day.dateKey == key) output[i].seconds = day.seconds;
    --local.tm_mday;
  }
}
void Pace::reset() {
  previous = dwell = pacedSeconds = units = transitions = 0;
  active = false;
}
void Pace::breakSequence() {
  previous = dwell = 0;
  active = false;
}
void Pace::credit(uint32_t seconds) { dwell = saturatedAdd(dwell, seconds); }
void Pace::show(uint32_t progress) {
  if (active && previous == progress) return;
  // More than 2% of a book is navigation, not a normal page transition.
  if (active && progress > previous && progress - previous <= 200 && dwell >= 5) {
    units = saturatedAdd(units, progress - previous);
    pacedSeconds = saturatedAdd(pacedSeconds, dwell);
    transitions = saturatedAdd(transitions, 1);
  } else if (active) {
    pacedSeconds = units = transitions = 0;
  }
  previous = progress;
  dwell = 0;
  active = true;
}
bool Pace::estimate(float remainingProgress, uint32_t& seconds) const {
  if (transitions < 3 || pacedSeconds < 30 || !units || !std::isfinite(remainingProgress) || remainingProgress < 0 ||
      remainingProgress > 1)
    return false;
  const double value = static_cast<double>(remainingProgress) * 10000 * pacedSeconds / units;
  if (value > UINT32_MAX) return false;
  seconds = static_cast<uint32_t>(std::ceil(value));
  return true;
}
}  // namespace LocalStats
