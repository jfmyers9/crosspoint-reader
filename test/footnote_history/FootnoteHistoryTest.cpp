#include <gtest/gtest.h>

#include "src/activities/reader/FootnoteHistory.h"

namespace {

FootnotePosition page(int spine, int number, uint32_t start) {
  FootnotePosition position;
  position.spineIndex = spine;
  position.pageNumber = number;
  position.renderSpec.fontId = 1;
  position.renderSpec.viewportWidth = 480;
  position.renderSpec.viewportHeight = 800;
  position.visibleTextOffset = start;
  return position;
}

FootnotePosition reflowedPage(int spine, int number, uint32_t start) {
  auto position = page(spine, number, start);
  position.renderSpec.fontId = 2;
  return position;
}

}  // namespace

TEST(FootnoteHistoryTest, NestedBackReturnsInReverseOrderAndRetainsOuterOrigin) {
  FootnoteHistory history;
  EXPECT_TRUE(history.empty());
  EXPECT_EQ(history.origin(), nullptr);
  EXPECT_FALSE(history.pop().has_value());

  history.push(page(1, 10, 1000));
  history.push(page(2, 20, 2000));
  history.push(page(3, 30, 3000));

  const auto inner = history.pop();
  ASSERT_TRUE(inner.has_value());
  EXPECT_EQ(inner->spineIndex, 3);
  EXPECT_EQ(inner->pageNumber, 30);
  ASSERT_NE(history.origin(), nullptr);
  EXPECT_EQ(history.origin()->visibleTextOffset, 1000u);

  const auto middle = history.pop();
  ASSERT_TRUE(middle.has_value());
  EXPECT_EQ(middle->spineIndex, 2);
  EXPECT_EQ(middle->pageNumber, 20);
  ASSERT_NE(history.origin(), nullptr);
  EXPECT_EQ(history.origin()->visibleTextOffset, 1000u);

  const auto outer = history.pop();
  ASSERT_TRUE(outer.has_value());
  EXPECT_EQ(outer->spineIndex, 1);
  EXPECT_EQ(outer->pageNumber, 10);
  EXPECT_EQ(outer->visibleTextOffset, 1000u);
  EXPECT_EQ(outer->renderSpec.fontId, 1);
  EXPECT_TRUE(history.empty());
  EXPECT_EQ(history.origin(), nullptr);
}

TEST(FootnoteHistoryTest, BacklinkToMiddleOriginPreservesOuterReturn) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000));
  history.push(page(2, 20, 2000));
  history.push(page(3, 30, 3000));

  history.unwind(page(2, 20, 2000));

  const auto remaining = history.pop();
  ASSERT_TRUE(remaining.has_value());
  EXPECT_EQ(remaining->spineIndex, 1);
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, BacklinkToEarliestMatchingOriginUnwindsAllNestedVisits) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000));
  history.push(page(2, 20, 2000));
  history.push(page(1, 10, 1000));

  history.unwind(page(1, 10, 1000));

  EXPECT_TRUE(history.empty());
  EXPECT_EQ(history.origin(), nullptr);
}

TEST(FootnoteHistoryTest, ExplicitJumpClearsHistoryBeforeAnotherVisit) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000));
  history.push(page(2, 20, 2000));
  history.clear();
  EXPECT_EQ(history.origin(), nullptr);
  EXPECT_FALSE(history.pop().has_value());

  history.push(page(4, 40, 4000));
  const auto destination = history.pop();
  ASSERT_TRUE(destination.has_value());
  EXPECT_EQ(destination->spineIndex, 4);
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, DepthOverflowRetainsExistingReturnsAndOuterOrigin) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000));
  history.push(page(2, 20, 2000));
  history.push(page(3, 30, 3000));
  history.push(page(4, 40, 4000));
  ASSERT_NE(history.origin(), nullptr);
  EXPECT_EQ(history.origin()->spineIndex, 1);

  for (int spine = 3; spine >= 1; --spine) {
    const auto position = history.pop();
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->spineIndex, spine);
  }
  EXPECT_FALSE(history.pop().has_value());
}

TEST(FootnoteHistoryTest, FontReflowPageNumberCollisionDoesNotDiscardOrigin) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000));

  history.unwind(reflowedPage(1, 10, 2000));

  ASSERT_NE(history.origin(), nullptr);
  EXPECT_EQ(history.origin()->visibleTextOffset, 1000u);
  history.unwind(page(1, 10, 1000));
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, ViewportChangeDoesNotUnwindAnUnrelatedPage) {
  FootnoteHistory history;
  const auto origin = page(1, 10, 1000);
  history.push(origin);
  auto landscape = origin;
  landscape.renderSpec.viewportWidth = 800;
  landscape.renderSpec.viewportHeight = 480;
  history.unwind(landscape);
  EXPECT_FALSE(history.empty());
  history.unwind(origin);
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, MatchingPageInAnotherSpineDoesNotUnwind) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000));
  history.unwind(page(2, 10, 1000));
  EXPECT_FALSE(history.empty());
}

TEST(FootnoteHistoryTest, ImagePagesWithEqualOffsetsRetainExactIdentity) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000));
  history.unwind(page(1, 11, 1000));
  EXPECT_FALSE(history.empty());
  history.unwind(page(1, 10, 1000));
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, OriginRetainsZeroOffsetAcrossNestedVisits) {
  FootnoteHistory history;
  history.push(page(1, 0, 0));
  history.push(page(2, 20, 2000));
  ASSERT_NE(history.origin(), nullptr);
  EXPECT_EQ(history.origin()->spineIndex, 1);
  EXPECT_EQ(history.origin()->visibleTextOffset, 0u);
}

TEST(FootnoteHistoryTest, SavedProgressKeepsPageHintOnlyInOriginalLayout) {
  FootnoteHistory history;
  auto origin = page(1, 10, 1000);
  origin.pageCount = 50;
  history.push(origin);
  history.push(page(2, 20, 2000));
  const auto* saved = history.origin();
  ASSERT_NE(saved, nullptr);
  EXPECT_EQ(saved->pageNumber, 10);
  EXPECT_EQ(saved->visibleTextOffset, 1000u);
  EXPECT_EQ(saved->progressPageCount(origin.renderSpec), 50);
  EXPECT_EQ(saved->progressPageCount(reflowedPage(2, 20, 2000).renderSpec), 0);
}
