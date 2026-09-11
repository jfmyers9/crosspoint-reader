#include "ReadingStatsRecorder.h"

#include <algorithm>

ReadingStatsRecorder::ReadingStatsRecorder(Sink sink, void* context, uint32_t idleSeconds, uint32_t minDwellSeconds)
    : sink(sink), context(context), idleSeconds(idleSeconds), minDwellSeconds(minDwellSeconds) {}

int64_t ReadingStatsRecorder::epochAt(const uint64_t timeMs) const {
  if (anchorEpoch <= 0) {
    return 0;
  }
  if (timeMs >= anchorMs) {
    return anchorEpoch + static_cast<int64_t>((timeMs - anchorMs) / 1000);
  }
  const uint64_t delta = anchorMs - timeMs;
  const int64_t seconds = static_cast<int64_t>(delta / 1000 + (delta % 1000 != 0));
  return anchorEpoch > seconds ? anchorEpoch - seconds : 0;
}

void ReadingStatsRecorder::setTimeAnchor(const uint64_t nowMs, const int64_t epochSeconds) {
  if (epochSeconds <= 0) {
    return;
  }
  anchorMs = nowMs;
  anchorEpoch = epochSeconds;
  if (active && sliceEpoch == 0) {
    sliceEpoch = epochAt(sliceMs);
  }
}

bool ReadingStatsRecorder::drain() {
  if (!hasPending) {
    return true;
  }
  if (!sink || !sink(context, pending)) {
    return false;
  }
  hasPending = false;
  return true;
}

bool ReadingStatsRecorder::finalize(const uint64_t nowMs, const bool close) {
  if (close && active && !closing) {
    closedMs = nowMs;
    closing = true;
  }
  if (!drain()) {
    return false;
  }
  if (!active) {
    return true;
  }
  const uint64_t measuredMs = closing ? closedMs : nowMs;
  const uint64_t elapsed = measuredMs >= openedMs ? measuredMs - openedMs : 0;
  const uint64_t endMs = openedMs + std::min(elapsed, static_cast<uint64_t>(idleSeconds) * 1000);
  const uint32_t duration = endMs > sliceMs ? static_cast<uint32_t>((endMs - sliceMs) / 1000) : 0;
  if (closing) {
    active = false;
    closing = false;
  }
  if (duration == 0 || duration < minDwellSeconds) {
    return true;
  }
  pending = {sliceMs, sliceEpoch, page, totalPages, duration};
  hasPending = true;
  sliceMs += static_cast<uint64_t>(duration) * 1000;
  if (sliceEpoch != 0) {
    sliceEpoch += duration;
  }
  return drain();
}

bool ReadingStatsRecorder::showPage(const uint32_t newPage, const uint32_t newTotalPages, const uint64_t nowMs) {
  if (!drain()) {
    return false;
  }
  if (newTotalPages == 0 || newPage > newTotalPages) {
    return pause(nowMs);
  }
  if (active && !closing && page == newPage && totalPages == newTotalPages) {
    return true;
  }
  if (!finalize(nowMs, true)) {
    return false;
  }
  const bool retainDeadline = suspended && page == newPage && totalPages == newTotalPages;
  page = newPage;
  totalPages = newTotalPages;
  if (!retainDeadline) {
    openedMs = nowMs;
  }
  sliceMs = nowMs;
  sliceEpoch = epochAt(nowMs);
  active = true;
  suspended = false;
  return true;
}

bool ReadingStatsRecorder::pause(const uint64_t nowMs) {
  suspended = false;
  return finalize(nowMs, true);
}

bool ReadingStatsRecorder::suspend(const uint64_t nowMs) {
  // A redraw preserves an active page's deadline; it must not revive a page
  // explicitly paused for a menu or another activity.
  if (active) suspended = true;
  return finalize(nowMs, true);
}

bool ReadingStatsRecorder::checkpoint(const uint64_t nowMs) { return finalize(nowMs, false); }
