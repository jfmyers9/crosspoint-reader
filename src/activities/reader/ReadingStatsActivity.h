#pragma once

#include <LocalReadingStats.h>

#include <string>

#include "activities/UiListActivity.h"

// A read-only snapshot: collecting statistics and SD persistence stay outside rendering.
class ReadingStatsActivity final : public UiListActivity {
 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookTitle,
                       float bookProgress, float chapterRemaining);
  void onEnter() override;

 private:
  static constexpr unsigned SUMMARY_ROWS = 5;
  static constexpr unsigned MAX_ROWS = SUMMARY_ROWS + LocalStats::DAYS;
  static constexpr unsigned TEXT_SIZE = 128;

  int listCount() const override { return rowCount; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void drawFooter() override;
  void activateIndex(int) override {}
  void rebuildRows();
  static void formatDuration(char* output, size_t size, uint32_t seconds);

  std::string bookTitle;
  const float bookProgress;
  const float chapterRemaining;
  LocalReadingStats::Summary summary{};
  // Activity-owned fixed storage avoids large stack frames and repaint-time allocation.
  freeink::ui::ListItem rows[MAX_ROWS]{};
  char subtitles[MAX_ROWS][TEXT_SIZE]{};
  char dates[LocalStats::DAYS][24]{};
  char globalDuration[48]{};
  char bookDuration[48]{};
  int rowCount = 0;
};
