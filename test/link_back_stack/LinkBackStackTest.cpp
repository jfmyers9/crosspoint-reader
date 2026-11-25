#include <gtest/gtest.h>

#include <cstring>

#include "LinkBackStack.h"
#include "ProgressComparison.h"

namespace {
CrossPointPosition makePosition(int spine, int page, int totalPages, uint32_t offset = 0, bool hasOffset = false,
                                uint16_t paragraph = 0, bool hasParagraph = false) {
  CrossPointPosition pos = {spine, page, totalPages};
  pos.hasResolvedSpineIndex = true;
  pos.hasMappedPage = totalPages > 0;
  pos.visibleTextOffset = offset;
  pos.hasVisibleTextOffset = hasOffset;
  pos.paragraphIndex = paragraph;
  pos.hasParagraphIndex = hasParagraph;
  return pos;
}
}  // namespace

TEST(LinkBackStackTest, EmptyStackUsesLivePosition) {
  LinkBackStack stack;
  EXPECT_TRUE(stack.empty());
  EXPECT_FALSE(stack.hasSyncAnchor());
  EXPECT_FALSE(stack.syncAnchor().has_value());
  EXPECT_EQ(stack.depth(), 0);
  EXPECT_FALSE(stack.pop().has_value());

  const auto live = makePosition(5, 12, 40, 1234, true, 18, true);
  const auto synced = stack.syncPosition(live);
  EXPECT_EQ(synced.spineIndex, 5);
  EXPECT_EQ(synced.pageNumber, 12);
  EXPECT_EQ(synced.totalPages, 40);
  EXPECT_TRUE(synced.hasVisibleTextOffset);
  EXPECT_EQ(synced.visibleTextOffset, 1234u);

  uint8_t buf[LinkBackStack::MAX_SERIALIZED_BYTES] = {};
  EXPECT_EQ(stack.serialize(buf, sizeof(buf)), 0u);
}

TEST(LinkBackStackTest, SingleAndNestedFootnotesAnchorSyncToMainTextRoot) {
  LinkBackStack stack;
  const auto mainPos = makePosition(5, 12, 40, 4567, true, 22, true);
  const auto note1Pos = makePosition(30, 4, 95, 9876, true, 88, true);
  const auto note2Pos = makePosition(30, 52, 95, 42000, true, 310, true);

  EXPECT_TRUE(stack.followLink(mainPos, 30));
  EXPECT_EQ(stack.depth(), 1);
  EXPECT_TRUE(stack.hasSyncAnchor());

  auto synced = stack.syncPosition(note1Pos);
  EXPECT_EQ(synced.spineIndex, 5);
  EXPECT_EQ(synced.pageNumber, 12);
  EXPECT_EQ(synced.totalPages, 40);
  EXPECT_TRUE(synced.hasResolvedSpineIndex);
  EXPECT_TRUE(synced.hasMappedPage);
  EXPECT_TRUE(synced.hasVisibleTextOffset);
  EXPECT_EQ(synced.visibleTextOffset, 4567u);
  EXPECT_TRUE(synced.hasParagraphIndex);
  EXPECT_EQ(synced.paragraphIndex, 22u);

  // Nested footnote within the same endnotes spine pushes rather than unwinding.
  EXPECT_TRUE(stack.followLink(note1Pos, 30, 52));
  EXPECT_EQ(stack.depth(), 2);

  synced = stack.syncPosition(note2Pos);
  EXPECT_EQ(synced.spineIndex, 5);
  EXPECT_EQ(synced.pageNumber, 12);
  EXPECT_EQ(synced.visibleTextOffset, 4567u);

  const auto firstPop = stack.pop();
  ASSERT_TRUE(firstPop.has_value());
  EXPECT_EQ(firstPop->spineIndex, 30);
  EXPECT_EQ(firstPop->pageNumber, 4);
  EXPECT_EQ(firstPop->visibleTextOffset, 9876u);

  const auto secondPop = stack.pop();
  ASSERT_TRUE(secondPop.has_value());
  EXPECT_EQ(secondPop->spineIndex, 5);
  EXPECT_EQ(secondPop->pageNumber, 12);
  EXPECT_EQ(secondPop->visibleTextOffset, 4567u);
  EXPECT_TRUE(stack.empty());
  EXPECT_FALSE(stack.hasSyncAnchor());
}

TEST(LinkBackStackTest, OverflowPinsMainTextRootWhileKeepingNewestJumps) {
  LinkBackStack stack;
  const auto mainPos = makePosition(4, 9, 30, 1000, true);
  const auto noteA = makePosition(30, 1, 95, 2000, true);
  const auto noteB = makePosition(30, 15, 95, 3000, true);
  const auto noteC = makePosition(30, 40, 95, 4000, true);

  EXPECT_TRUE(stack.followLink(mainPos, 30));
  EXPECT_TRUE(stack.followLink(noteA, 30, 15));
  EXPECT_TRUE(stack.followLink(noteB, 30, 40));
  EXPECT_EQ(stack.depth(), LinkBackStack::MAX_DEPTH);

  // Fourth nested jump keeps mainPos pinned at entries[0] and drops noteA.
  EXPECT_TRUE(stack.followLink(noteC, 30, 80));
  EXPECT_EQ(stack.depth(), LinkBackStack::MAX_DEPTH);

  const auto synced = stack.syncPosition(makePosition(30, 80, 95, 5000, true));
  EXPECT_EQ(synced.spineIndex, 4);
  EXPECT_EQ(synced.pageNumber, 9);
  EXPECT_EQ(synced.visibleTextOffset, 1000u);

  const auto p1 = stack.pop();
  const auto p2 = stack.pop();
  const auto p3 = stack.pop();
  ASSERT_TRUE(p1.has_value());
  ASSERT_TRUE(p2.has_value());
  ASSERT_TRUE(p3.has_value());
  EXPECT_EQ(p1->pageNumber, 40);
  EXPECT_EQ(p2->pageNumber, 15);
  EXPECT_EQ(p3->spineIndex, 4);
  EXPECT_EQ(p3->pageNumber, 9);
  EXPECT_TRUE(stack.empty());
}

TEST(LinkBackStackTest, NonAnchoringTocLinkDoesNotPinSync) {
  LinkBackStack stack;
  const auto tocPos = makePosition(1, 0, 2, 0, true);
  const auto ch5Pos = makePosition(5, 12, 40, 4567, true);
  const auto endnotePos = makePosition(30, 6, 95, 9000, true);

  // Tapping an HTML TOC link saves a Back entry without anchoring sync.
  EXPECT_TRUE(stack.followLink(tocPos, 5, std::nullopt, /*anchorsSync=*/false));
  EXPECT_EQ(stack.depth(), 1);
  EXPECT_FALSE(stack.hasSyncAnchor());
  EXPECT_EQ(stack.syncPosition(ch5Pos).spineIndex, 5);
  EXPECT_EQ(stack.syncPosition(ch5Pos).pageNumber, 12);

  // Following a footnote from Chapter 5 anchors sync on Chapter 5, not the TOC.
  EXPECT_TRUE(stack.followLink(ch5Pos, 30, std::nullopt, /*anchorsSync=*/true));
  EXPECT_EQ(stack.depth(), 2);
  EXPECT_TRUE(stack.hasSyncAnchor());
  const auto synced = stack.syncPosition(endnotePos);
  EXPECT_EQ(synced.spineIndex, 5);
  EXPECT_EQ(synced.pageNumber, 12);
  EXPECT_EQ(synced.visibleTextOffset, 4567u);

  // Overflowing past MAX_DEPTH evicts the non-anchoring TOC entry first so the
  // Chapter 5 footnote anchor survives.
  EXPECT_TRUE(stack.followLink(endnotePos, 30, 25, /*anchorsSync=*/true));
  EXPECT_TRUE(stack.followLink(makePosition(30, 25, 95, 12000, true), 30, 60, /*anchorsSync=*/true));
  EXPECT_EQ(stack.depth(), LinkBackStack::MAX_DEPTH);
  EXPECT_EQ(stack.syncPosition(makePosition(30, 60, 95, 20000, true)).spineIndex, 5);
  EXPECT_EQ(stack.syncPosition(makePosition(30, 60, 95, 20000, true)).pageNumber, 12);
}

TEST(LinkBackStackTest, CrossSpineBackLinkUnwindsStack) {
  LinkBackStack stack;
  const auto mainPos = makePosition(5, 12, 40, 4567, true);
  const auto noteAPos = makePosition(30, 4, 95, 9876, true);
  const auto noteBPos = makePosition(31, 2, 50, 2048, true);

  EXPECT_TRUE(stack.followLink(mainPos, 30));
  EXPECT_TRUE(stack.followLink(noteAPos, 31));
  EXPECT_EQ(stack.depth(), 2);

  // Return link from spine 31 back to spine 30 unwinds to depth 1 (anchor stays spine 5).
  EXPECT_FALSE(stack.followLink(noteBPos, 30));
  EXPECT_EQ(stack.depth(), 1);
  ASSERT_TRUE(stack.syncAnchor().has_value());
  EXPECT_EQ(stack.syncAnchor()->spineIndex, 5);

  // Return link from spine 30 back to spine 5 unwinds completely.
  EXPECT_FALSE(stack.followLink(noteAPos, 5));
  EXPECT_TRUE(stack.empty());
}

TEST(LinkBackStackTest, SameSpineReturnLinkUnwindsWithinTolerance) {
  LinkBackStack stack;
  const auto mainPos = makePosition(5, 12, 45, 3200, true);
  const auto endOfChapterNote = makePosition(5, 40, 45, 18000, true);

  // Jump to chapter-end footnote in the same spine.
  EXPECT_TRUE(stack.followLink(mainPos, 5, 40));
  EXPECT_EQ(stack.depth(), 1);

  // A nested same-spine note far from page 12 pushes rather than unwinding.
  EXPECT_TRUE(stack.followLink(endOfChapterNote, 5, 43));
  EXPECT_EQ(stack.depth(), 2);

  // Tapping a return link back to page 40 (exact) unwinds to depth 1.
  EXPECT_FALSE(stack.followLink(makePosition(5, 43, 45, 19500, true), 5, 40));
  EXPECT_EQ(stack.depth(), 1);

  // Tapping a return link back to page 11 (±1 tolerance around page 12) unwinds to empty.
  EXPECT_FALSE(stack.followLink(endOfChapterNote, 5, 11));
  EXPECT_TRUE(stack.empty());
}

TEST(LinkBackStackTest, V2SerializeDeserializeRoundTrip) {
  LinkBackStack original;
  original.followLink(makePosition(1, 0, 2, 0, false), 5, std::nullopt, /*anchorsSync=*/false);
  original.followLink(makePosition(5, 12, 40, 0x00123456u, true, 77, true), 30, std::nullopt, /*anchorsSync=*/true);

  uint8_t buf[LinkBackStack::MAX_SERIALIZED_BYTES] = {};
  const size_t written = original.serialize(buf, sizeof(buf));
  ASSERT_EQ(written, 2u + 2u * LinkBackStack::V2_ENTRY_BYTES);
  EXPECT_EQ(buf[0], LinkBackStack::V2_MAGIC);
  EXPECT_EQ(buf[1], 2u);

  LinkBackStack restored;
  bool wasLegacyV1 = true;
  ASSERT_TRUE(restored.deserialize(buf, written, 40, &wasLegacyV1));
  EXPECT_FALSE(wasLegacyV1);
  EXPECT_EQ(restored.depth(), 2);

  // Sync anchor skips the non-anchoring TOC entry and picks spine 5.
  ASSERT_TRUE(restored.syncAnchor().has_value());
  EXPECT_EQ(restored.syncAnchor()->spineIndex, 5);
  EXPECT_EQ(restored.syncAnchor()->pageNumber, 12);

  const auto top = restored.pop();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(top->spineIndex, 5);
  EXPECT_EQ(top->pageNumber, 12);
  EXPECT_EQ(top->totalPages, 40);
  EXPECT_TRUE(top->hasVisibleTextOffset);
  EXPECT_EQ(top->visibleTextOffset, 0x00123456u);
  EXPECT_TRUE(top->hasParagraphIndex);
  EXPECT_EQ(top->paragraphIndex, 77u);
  EXPECT_TRUE(top->anchorsSync);

  const auto bottom = restored.pop();
  ASSERT_TRUE(bottom.has_value());
  EXPECT_EQ(bottom->spineIndex, 1);
  EXPECT_EQ(bottom->pageNumber, 0);
  EXPECT_EQ(bottom->totalPages, 2);
  EXPECT_FALSE(bottom->hasVisibleTextOffset);
  EXPECT_FALSE(bottom->hasParagraphIndex);
  EXPECT_FALSE(bottom->anchorsSync);
}

TEST(LinkBackStackTest, LegacyV1DeserializeMarksUnmappedUntilHydrated) {
  const uint8_t v1[] = {
      2,             // depth
      5,  0, 12, 0,  // spine 5, page 12
      30, 0, 4,  0,  // spine 30, page 4
  };
  LinkBackStack stack;
  bool wasLegacyV1 = false;
  ASSERT_TRUE(stack.deserialize(v1, sizeof(v1), 40, &wasLegacyV1));
  EXPECT_TRUE(wasLegacyV1);
  EXPECT_EQ(stack.depth(), 2);
  EXPECT_TRUE(stack.hasSyncAnchor());

  // Unhydrated v1 entry has totalPages == 0 -> hasMappedPage must be false so
  // compareProgress does not treat page 12 > remote page 10 as LocalAhead and
  // upload chapter-start 0.0f backwards.
  const auto unhydrated = stack.syncPosition(makePosition(30, 20, 95, 9999, true));
  EXPECT_EQ(unhydrated.spineIndex, 5);
  EXPECT_EQ(unhydrated.pageNumber, 12);
  EXPECT_EQ(unhydrated.totalPages, 0);
  EXPECT_FALSE(unhydrated.hasVisibleTextOffset);
  EXPECT_FALSE(unhydrated.hasParagraphIndex);
  EXPECT_TRUE(unhydrated.hasResolvedSpineIndex);
  EXPECT_FALSE(unhydrated.hasMappedPage);

  const auto remoteSameSpineLaterPct = makePosition(5, 10, 40, 3800, true);
  EXPECT_EQ(compareProgress(unhydrated, 0.20f, remoteSameSpineLaterPct, 0.24f), ProgressComparison::RemoteAhead);

  // After hydrating totalPages + offset from section cache, mapping is restored.
  stack.at(0).totalPages = 40;
  stack.at(0).visibleTextOffset = 4567;
  stack.at(0).hasVisibleTextOffset = true;
  const auto hydrated = stack.syncPosition(makePosition(30, 20, 95, 9999, true));
  EXPECT_TRUE(hydrated.hasMappedPage);
  EXPECT_EQ(compareProgress(hydrated, 0.25f, remoteSameSpineLaterPct, 0.24f), ProgressComparison::LocalAhead);
}

TEST(LinkBackStackTest, RejectsInvalidOrCorruptSerializedData) {
  LinkBackStack stack;
  EXPECT_FALSE(stack.deserialize(nullptr, 10, 40));

  const uint8_t emptyDepth[] = {LinkBackStack::V2_MAGIC, 0};
  EXPECT_FALSE(stack.deserialize(emptyDepth, sizeof(emptyDepth), 40));

  const uint8_t tooDeep[] = {LinkBackStack::V2_MAGIC, LinkBackStack::MAX_DEPTH + 1};
  EXPECT_FALSE(stack.deserialize(tooDeep, sizeof(tooDeep), 40));

  // Valid v2 entry, then corrupt spine count / reserved flags / truncation.
  LinkBackStack valid;
  valid.followLink(makePosition(10, 3, 20, 500, true, 4, true), 30);
  uint8_t buf[LinkBackStack::MAX_SERIALIZED_BYTES] = {};
  const size_t written = valid.serialize(buf, sizeof(buf));
  ASSERT_GT(written, 0u);

  EXPECT_EQ(valid.serialize(buf, written - 1), 0u);
  EXPECT_FALSE(stack.deserialize(buf, written - 1, 40));
  EXPECT_FALSE(stack.deserialize(buf, written, 10));  // spine 10 out of range for spineCount=10

  buf[2 + 6] = 0x80;  // reserved flag bit
  EXPECT_FALSE(stack.deserialize(buf, written, 40));
  EXPECT_TRUE(stack.empty());
}

TEST(LinkBackStackTest, ProgressComparisonUsesMainTextRootWhileInFootnote) {
  LinkBackStack stack;
  const auto mainPos = makePosition(5, 12, 40, 4567, true);
  const auto endnotePos = makePosition(30, 18, 95, 15000, true);
  stack.followLink(mainPos, 30);

  const auto localForSync = stack.syncPosition(endnotePos);
  const auto remoteSame = makePosition(5, 12, 40, 4567, true);
  const auto remoteBehind = makePosition(5, 8, 40, 2000, true);
  const auto remoteAhead = makePosition(7, 2, 35, 900, true);

  EXPECT_EQ(compareProgress(localForSync, 0.25f, remoteSame, 0.25f), ProgressComparison::Synchronized);
  EXPECT_EQ(compareProgress(localForSync, 0.25f, remoteBehind, 0.20f), ProgressComparison::LocalAhead);
  EXPECT_EQ(compareProgress(localForSync, 0.25f, remoteAhead, 0.35f), ProgressComparison::RemoteAhead);
}
