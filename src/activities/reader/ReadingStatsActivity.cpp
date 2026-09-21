#include "ReadingStatsActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "components/UITheme.h"

namespace fui = freeink::ui;

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string bookTitle, const float bookProgress,
                                           const float chapterRemaining)
    : UiListActivity("ReadingStats", renderer, mappedInput),
      bookTitle(std::move(bookTitle)),
      bookProgress(bookProgress),
      chapterRemaining(chapterRemaining) {}

void ReadingStatsActivity::onEnter() {
  RenderLock lock(*this);
  UiListActivity::onEnter();
  LocalReadingStats::checkpoint();
  LocalReadingStats::getSummary(summary);
  rebuildRows();
}

const char* ReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATISTICS); }

void ReadingStatsActivity::formatDuration(char* output, const size_t size, const uint32_t seconds) {
  if (seconds > 0 && seconds < 60) {
    snprintf(output, size, "%s", tr(STR_STATS_LESS_THAN_MINUTE));
    return;
  }
  snprintf(output, size, tr(STR_STATS_DURATION_FORMAT), static_cast<unsigned long long>(seconds / 3600),
           static_cast<unsigned>((seconds / 60) % 60));
}

void ReadingStatsActivity::rebuildRows() {
  if (!summary.storageHealthy) {
    // A failed book switch may leave the previous book's records in RAM.
    // Never attribute those totals to the currently displayed title.
    rowCount = 1;
    rows[0] = {};
    rows[0].label = tr(STR_READING_STATISTICS);
    rows[0].subtitle = tr(STR_STATS_STORAGE_UNAVAILABLE);
    rows[0].sectionHeading = bookTitle.c_str();
    return;
  }
  rowCount = SUMMARY_ROWS;
  rows[0].sectionHeading = nullptr;
  rows[0].label = tr(STR_STATS_TODAY);
  if (summary.clockValid) {
    formatDuration(subtitles[0], TEXT_SIZE, summary.todaySeconds);
  } else {
    snprintf(subtitles[0], TEXT_SIZE, "%s", tr(STR_STATS_CLOCK_UNKNOWN));
  }
  rows[1].label = tr(STR_STATS_ALL_TIME);
  formatDuration(subtitles[1], TEXT_SIZE, summary.totalSeconds);
  rows[2].label = tr(STR_STATS_THIS_BOOK);
  rows[2].sectionHeading = bookTitle.c_str();
  formatDuration(subtitles[2], TEXT_SIZE, summary.bookSeconds);
  rows[3].label = tr(STR_STATS_CHAPTER_LEFT);
  rows[4].label = tr(STR_STATS_BOOK_LEFT);

  uint32_t seconds = 0;
  if (std::isfinite(chapterRemaining) && chapterRemaining >= 0 &&
      LocalReadingStats::estimateSeconds(std::clamp(chapterRemaining, 0.0f, 1.0f), seconds)) {
    formatDuration(subtitles[3], TEXT_SIZE, seconds);
  } else {
    snprintf(subtitles[3], TEXT_SIZE, "%s", tr(STR_READING_PACE_LEARNING));
  }
  if (std::isfinite(bookProgress) && bookProgress >= 0 &&
      LocalReadingStats::estimateSeconds(1.0f - std::clamp(bookProgress, 0.0f, 1.0f), seconds)) {
    formatDuration(subtitles[4], TEXT_SIZE, seconds);
  } else {
    snprintf(subtitles[4], TEXT_SIZE, "%s", tr(STR_READING_PACE_LEARNING));
  }

  for (unsigned day = 0; day < LocalStats::DAYS; ++day) {
    const auto& total = summary.daily[day];
    if (total.dateKey == 0) continue;
    snprintf(dates[day], sizeof(dates[day]), tr(STR_STATS_DATE_FORMAT), static_cast<int>(total.dateKey / 10000),
             static_cast<int>((total.dateKey / 100) % 100), static_cast<int>(total.dateKey % 100));
    formatDuration(globalDuration, sizeof(globalDuration), total.seconds);
    // Match by date rather than assuming the per-book snapshot has identical populated slots.
    uint32_t bookSeconds = 0;
    for (const auto& bookDay : summary.bookDaily) {
      if (bookDay.dateKey == total.dateKey) {
        bookSeconds = bookDay.seconds;
        break;
      }
    }
    formatDuration(bookDuration, sizeof(bookDuration), bookSeconds);
    rows[rowCount].label = dates[day];
    snprintf(subtitles[rowCount], TEXT_SIZE, tr(STR_STATS_DAY_TOTALS_FORMAT), globalDuration, bookDuration);
    ++rowCount;
  }
  if (rowCount == SUMMARY_ROWS) {
    rows[rowCount].label = tr(STR_STATS_HISTORY_EMPTY);
    subtitles[rowCount][0] = '\0';
    ++rowCount;
  }
  rows[SUMMARY_ROWS].sectionHeading = tr(STR_STATS_LAST_30_DAYS);
  for (int row = 0; row < rowCount; ++row) {
    rows[row].subtitle = subtitles[row];
    rows[row].actionValue = static_cast<int16_t>(row);
  }
}

void ReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = summary.storageHealthy ? 2 : 4;
  syncListViewport(screen, props);
  screen.list(props);
}

void ReadingStatsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
