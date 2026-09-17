#include <gtest/gtest.h>

#include "src/activities/reader/FootnoteHistory.h"

namespace {

FootnotePosition page(int spine, int number, uint32_t start, std::optional<uint32_t> end) {
  FootnotePosition position;
  position.spineIndex = spine;
  position.pageNumber = number;
  position.renderSpec.fontId = 1;
  position.renderSpec.viewportWidth = 480;
  position.renderSpec.viewportHeight = 800;
  position.visibleTextOffset = start;
  position.nextVisibleTextOffset = end;
  return position;
}

FootnotePosition reflowedPage(int spine, int number, uint32_t start, std::optional<uint32_t> end) {
  auto position = page(spine, number, start, end);
  position.renderSpec.fontId = 2;
  return position;
}

}  // namespace

TEST(FootnoteHistoryTest, NestedBackReturnsInReverseOrderAndRetainsOuterOrigin) {
  FootnoteHistory history;
  EXPECT_TRUE(history.empty());
  EXPECT_EQ(history.origin(), nullptr);
  EXPECT_FALSE(history.pop().has_value());

  history.push(page(1, 10, 1000, 1100));
  history.push(page(2, 20, 2000, 2100));
  history.push(page(3, 30, 3000, 3100));

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
  EXPECT_EQ(outer->nextVisibleTextOffset, 1100u);
  EXPECT_EQ(outer->renderSpec.fontId, 1);
  EXPECT_TRUE(history.empty());
  EXPECT_EQ(history.origin(), nullptr);
}

TEST(FootnoteHistoryTest, BacklinkToMiddleOriginPreservesOuterReturn) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));
  history.push(page(2, 20, 2000, 2100));
  history.push(page(3, 30, 3000, 3100));

  history.unwind(page(2, 20, 2000, 2100));

  const auto remaining = history.pop();
  ASSERT_TRUE(remaining.has_value());
  EXPECT_EQ(remaining->spineIndex, 1);
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, BacklinkToEarliestMatchingOriginUnwindsAllNestedVisits) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));
  history.push(page(2, 20, 2000, 2100));
  history.push(page(1, 10, 1000, 1100));

  history.unwind(page(1, 10, 1000, 1100));

  EXPECT_TRUE(history.empty());
  EXPECT_EQ(history.origin(), nullptr);
}

TEST(FootnoteHistoryTest, ExplicitJumpClearsHistoryBeforeAnotherVisit) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));
  history.push(page(2, 20, 2000, 2100));
  history.clear();
  EXPECT_EQ(history.origin(), nullptr);
  EXPECT_FALSE(history.pop().has_value());

  history.push(page(4, 40, 4000, 4100));
  const auto destination = history.pop();
  ASSERT_TRUE(destination.has_value());
  EXPECT_EQ(destination->spineIndex, 4);
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, DepthOverflowRetainsExistingReturnsAndOuterOrigin) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));
  history.push(page(2, 20, 2000, 2100));
  history.push(page(3, 30, 3000, 3100));
  history.push(page(4, 40, 4000, 4100));
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
  history.push(page(1, 10, 1000, 1100));

  history.unwind(reflowedPage(1, 10, 2000, 2100));

  ASSERT_NE(history.origin(), nullptr);
  EXPECT_EQ(history.origin()->visibleTextOffset, 1000u);
  history.unwind(page(1, 10, 1000, 1100));
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, BacklinkIntoLaterPartOfSplitOriginPageUnwinds) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1200));

  history.unwind(reflowedPage(1, 22, 1100, 1150));

  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, LargerReflowedPageContainingOriginUnwinds) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));

  history.unwind(reflowedPage(1, 4, 900, 1200));

  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, AdjacentReflowedPagesDoNotCountAsReturningToOrigin) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));

  history.unwind(reflowedPage(1, 9, 900, 1000));
  EXPECT_FALSE(history.empty());
  history.unwind(reflowedPage(1, 11, 1100, 1200));
  EXPECT_FALSE(history.empty());
}

TEST(FootnoteHistoryTest, ViewportChangeUsesContentInsteadOfPageNumber) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));
  auto landscape = page(1, 10, 2000, 2100);
  landscape.renderSpec.viewportWidth = 800;
  landscape.renderSpec.viewportHeight = 480;

  history.unwind(landscape);
  EXPECT_FALSE(history.empty());
  landscape.pageNumber = 5;
  landscape.visibleTextOffset = 1050;
  landscape.nextVisibleTextOffset = 1150;
  history.unwind(landscape);
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, MatchingOffsetsOrPageNumbersInAnotherSpineDoNotUnwind) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1100));

  history.unwind(page(2, 10, 1000, 1100));
  EXPECT_FALSE(history.empty());
  history.unwind(reflowedPage(2, 10, 1000, 1100));
  EXPECT_FALSE(history.empty());
}

TEST(FootnoteHistoryTest, SameLayoutImagePagesWithEqualOffsetsRetainExactIdentity) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, 1000));

  history.unwind(page(1, 11, 1000, 1000));
  EXPECT_FALSE(history.empty());
  history.unwind(page(1, 10, 1000, 1000));
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, UnknownFrontierDoesNotInferOverlapFromPageNumber) {
  FootnoteHistory history;
  history.push(page(1, 10, 1000, std::nullopt));

  history.unwind(reflowedPage(1, 10, 1050, std::nullopt));
  EXPECT_FALSE(history.empty());
  history.unwind(reflowedPage(1, 10, 950, std::nullopt));
  EXPECT_FALSE(history.empty());
  history.unwind(reflowedPage(1, 20, 1000, std::nullopt));
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, MissingContentOffsetCannotFallBackToPageNumberAfterReflow) {
  FootnoteHistory history;
  auto origin = page(1, 10, 1000, 1100);
  origin.visibleTextOffset.reset();
  history.push(origin);
  history.unwind(reflowedPage(1, 10, 1000, 1100));
  EXPECT_FALSE(history.empty());

  history.clear();
  history.push(page(1, 10, 1000, 1100));
  auto destination = reflowedPage(1, 10, 1000, 1100);
  destination.visibleTextOffset.reset();
  history.unwind(destination);
  EXPECT_FALSE(history.empty());
}

TEST(FootnoteHistoryTest, BackUsesExactPageInOriginalLayoutAndContentOffsetAfterReflow) {
  FootnoteHistory history;
  auto origin = page(1, 10, 1000, 1000);
  history.push(origin);
  const auto target = history.pop();
  ASSERT_TRUE(target.has_value());

  EXPECT_FALSE(target->returnOffset(origin.renderSpec).has_value());
  EXPECT_EQ(target->pageNumber, 10);
  EXPECT_EQ(target->returnOffset(reflowedPage(1, 20, 2000, 2100).renderSpec), 1000u);
  EXPECT_FALSE(target->returnOffset(origin.renderSpec).has_value());
}

TEST(FootnoteHistoryTest, ReflowedProgressRequiresOffsetEvenWhenANewLayoutCacheExists) {
  FootnoteHistory history;
  auto origin = page(1, 10, 1000, 1100);
  origin.pageCount = 50;
  history.push(origin);
  history.push(page(2, 20, 2000, 2100));
  const auto* saved = history.origin();
  ASSERT_NE(saved, nullptr);

  EXPECT_EQ(saved->progressPageCount(origin.renderSpec), 50);
  EXPECT_EQ(saved->progressPageCount(reflowedPage(2, 10, 2000, 2100).renderSpec), 0);
  EXPECT_EQ(saved->visibleTextOffset, 1000u);
  EXPECT_EQ(saved->spineIndex, 1);
}

TEST(FootnoteHistoryTest, ZeroContentOffsetIsAValidReflowReturn) {
  FootnoteHistory history;
  history.push(page(1, 0, 0, 100));
  const auto target = history.pop();
  ASSERT_TRUE(target.has_value());
  const auto offset = target->returnOffset(reflowedPage(1, 4, 1000, 1100).renderSpec);
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(*offset, 0u);
}
