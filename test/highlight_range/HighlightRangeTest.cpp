#include <gtest/gtest.h>

#include <limits>

#include "src/BookmarkEntry.h"
#include "src/util/HighlightRange.h"

TEST(HighlightRangeTest, LegacyBookmarksAreNotHighlights) {
  BookmarkEntry entry{};
  EXPECT_FALSE(entry.isHighlight());
  entry.highlightEndOffset = 20;
  EXPECT_FALSE(entry.isHighlight());
  entry.hasVisibleTextOffset = true;
  EXPECT_TRUE(entry.isHighlight());
}

TEST(HighlightRangeTest, RequiresExclusiveEndAfterStart) {
  BookmarkEntry entry{};
  entry.hasVisibleTextOffset = true;
  entry.visibleTextOffset = 10;
  entry.highlightEndOffset = 9;
  EXPECT_FALSE(entry.isHighlight());
  entry.highlightEndOffset = 10;
  EXPECT_FALSE(entry.isHighlight());
  entry.highlightEndOffset = 11;
  EXPECT_TRUE(entry.isHighlight());
}

TEST(HighlightRangeTest, HalfOpenOverlap) {
  EXPECT_TRUE(HighlightRange::overlaps(10, 20, 10, 20));
  EXPECT_TRUE(HighlightRange::overlaps(10, 20, 15, 25));
  EXPECT_TRUE(HighlightRange::overlaps(10, 20, 5, 15));
  EXPECT_TRUE(HighlightRange::overlaps(10, 20, 12, 18));
  EXPECT_TRUE(HighlightRange::overlaps(10, 20, 5, 25));
  EXPECT_FALSE(HighlightRange::overlaps(10, 20, 20, 25));
  EXPECT_FALSE(HighlightRange::overlaps(10, 20, 5, 10));
}

TEST(HighlightRangeTest, RejectsEmptyAndReversedRanges) {
  EXPECT_FALSE(HighlightRange::overlaps(10, 10, 5, 25));
  EXPECT_FALSE(HighlightRange::overlaps(10, 20, 15, 15));
  EXPECT_FALSE(HighlightRange::overlaps(20, 10, 5, 25));
  EXPECT_FALSE(HighlightRange::overlaps(10, 20, 25, 5));
}

TEST(HighlightRangeTest, MaximumOffsetsDoNotOverflow) {
  constexpr uint32_t max = std::numeric_limits<uint32_t>::max();
  static_assert(HighlightRange::overlaps(max - 2, max, max - 1, max));
  EXPECT_FALSE(HighlightRange::overlaps(max - 2, max - 1, max - 1, max));
}
