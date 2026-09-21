#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#define class struct
#define private public
#include "Epub/ParsedText.h"
#undef private
#undef class

void setTestHyphenationBreak(size_t offset);

TEST(WordOffsets, ArenaRoundTripWithOddWordCountAndFocus) {
  const std::vector<std::string> words = {"first", "second", "marker"};
  const std::vector<int16_t> xpos = {0, 45, 90};
  const std::vector<EpdFontFamily::Style> styles(3, EpdFontFamily::REGULAR);
  const uint32_t offsets[] = {0, 70000, TextBlock::UNKNOWN_WORD_OFFSET};
  const uint32_t ends[] = {5, 70006, TextBlock::UNKNOWN_WORD_OFFSET};
  TextBlock block(words, xpos, styles, {2, 0, 0}, {16, 0, 0}, BlockStyle(), {}, {}, offsets, ends);
  ASSERT_TRUE(block.valid());
  ASSERT_TRUE(block.hasWordOffsets());
  EXPECT_EQ(block.wordVisibleOffset(0), 0u);
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-word-offsets.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    ASSERT_TRUE(block.serialize(output));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  auto loaded = TextBlock::deserialize(input);
  ASSERT_NE(loaded, nullptr);
  ASSERT_TRUE(loaded->hasWordOffsets());
  for (uint16_t i = 0; i < 3; ++i) {
    EXPECT_EQ(loaded->wordVisibleOffset(i), offsets[i]);
    EXPECT_EQ(loaded->wordVisibleEndOffset(i), ends[i]);
    EXPECT_STREQ(loaded->wordText(i), words[i].c_str());
    EXPECT_EQ(loaded->wordXpos(i), xpos[i]);
  }
  EXPECT_EQ(loaded->focusBoundary(0), 2);
  EXPECT_EQ(loaded->focusSuffixX(0), 16);
  EXPECT_EQ(loaded->wordVisibleOffset(3), TextBlock::UNKNOWN_WORD_OFFSET);
  EXPECT_EQ(loaded->wordVisibleEndOffset(3), TextBlock::UNKNOWN_WORD_OFFSET);
}

TEST(WordOffsets, SyntheticBlocksRemainUnknown) {
  TextBlock block({"table"}, {0}, {EpdFontFamily::REGULAR}, {}, {});
  ASSERT_TRUE(block.valid());
  EXPECT_FALSE(block.hasWordOffsets());
  EXPECT_EQ(block.wordVisibleOffset(0), TextBlock::UNKNOWN_WORD_OFFSET);
  EXPECT_EQ(block.wordVisibleEndOffset(0), TextBlock::UNKNOWN_WORD_OFFSET);
  EXPECT_EQ(block.wordVisibleOffset(1), TextBlock::UNKNOWN_WORD_OFFSET);
  ParsedText text(false);
  text.addWord("synthetic", EpdFontFamily::REGULAR);
  GfxRenderer renderer;
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, uint32_t) {
    ++lines;
    EXPECT_FALSE(line->hasWordOffsets());
  });
  EXPECT_EQ(lines, 1u);
}

TEST(WordOffsets, CjkAndMixedSyntheticOffsetsSurviveLayout) {
  ParsedText text(false);
  text.addWord("*", EpdFontFamily::REGULAR);
  text.addWord("中文", EpdFontFamily::REGULAR, false, false, 0);
  text.addWord("end", EpdFontFamily::REGULAR, false, false, 70000);
  GfxRenderer renderer;
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, uint32_t offset) {
    ++lines;
    ASSERT_EQ(line->wordCount(), 4);
    EXPECT_EQ(offset, 0u);
    EXPECT_EQ(line->wordVisibleOffset(0), TextBlock::UNKNOWN_WORD_OFFSET);
    EXPECT_EQ(line->wordVisibleEndOffset(0), TextBlock::UNKNOWN_WORD_OFFSET);
    EXPECT_EQ(line->wordVisibleOffset(1), 0u);
    EXPECT_EQ(line->wordVisibleOffset(2), 1u);
    EXPECT_EQ(line->wordVisibleOffset(3), 70000u);
    EXPECT_EQ(line->wordVisibleEndOffset(1), 1u);
    EXPECT_EQ(line->wordVisibleEndOffset(2), 2u);
    EXPECT_EQ(line->wordVisibleEndOffset(3), 70003u);
  });
  EXPECT_EQ(lines, 1u);
}

TEST(WordOffsets, BidiOffsetsFollowTheirVisualWords) {
  BlockStyle style;
  style.isRtl = true;
  ParsedText text(false, false, false, style);
  text.addWord("שלום", EpdFontFamily::REGULAR, false, false, 12);
  text.addWord("עולם", EpdFontFamily::REGULAR, false, false, 17);
  GfxRenderer renderer;
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, uint32_t) {
    ++lines;
    ASSERT_EQ(line->wordCount(), 2);
    for (uint16_t i = 0; i < line->wordCount(); ++i) {
      EXPECT_EQ(line->wordVisibleOffset(i), std::string(line->wordText(i)) == "שלום" ? 12u : 17u);
      EXPECT_EQ(line->wordVisibleEndOffset(i), std::string(line->wordText(i)) == "שלום" ? 16u : 21u);
    }
  });
  EXPECT_EQ(lines, 1u);
}

TEST(WordOffsets, NormalizationDoesNotInventSourcePositions) {
  ParsedText text(false, false, true);
  text.addWord("e\xcc\x81lan", EpdFontFamily::REGULAR, false, false, 25);
  text.addWord("next", EpdFontFamily::REGULAR, false, false, 31);
  GfxRenderer renderer;
  text.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, uint32_t pageOffset) {
    ASSERT_EQ(line->wordCount(), 2);
    EXPECT_EQ(pageOffset, 25u);
    EXPECT_STREQ(line->wordText(0), "élan");
    EXPECT_EQ(line->wordVisibleOffset(0), TextBlock::UNKNOWN_WORD_OFFSET);
    EXPECT_EQ(line->wordVisibleEndOffset(0), TextBlock::UNKNOWN_WORD_OFFSET);
    EXPECT_EQ(line->wordVisibleOffset(1), 31u);
  });
}

TEST(WordOffsets, HyphenatedRemainderCountsSourceCodepointsNotBytesOrInsertedHyphen) {
  ParsedText text(false, true);
  text.addWord("ééabcd", EpdFontFamily::REGULAR, false, false, 65534);
  text.addWord("tail", EpdFontFamily::REGULAR, false, false, 65542);
  GfxRenderer renderer;
  auto widths = text.calculateWordWidths(renderer, 0);
  setTestHyphenationBreak(4);  // Two UTF-8 codepoints, before an inserted hyphen.
  const bool split = text.hyphenateWordAtIndex(0, 40, renderer, 0, widths, false);
  setTestHyphenationBreak(0);
  ASSERT_TRUE(split);
  EXPECT_EQ(text.visibleOffsetAt(0), 65534u);
  EXPECT_EQ(text.visibleOffsetAt(1), 65536u);
  EXPECT_EQ(text.visibleOffsetAt(2), 65542u);
  EXPECT_EQ(text.wordAt(0), "éé-");
}

TEST(WordOffsets, InsertingRebaseDoesNotChangeFollowingOffsets) {
  ParsedText text(false);
  text.pushVisibleOffset(0);
  text.pushVisibleOffset(65535);
  text.pushVisibleOffset(42);
  text.insertVisibleOffset(2, 65536);
  EXPECT_EQ(text.visibleOffsetAt(0), 0u);
  EXPECT_EQ(text.visibleOffsetAt(1), 65535u);
  EXPECT_EQ(text.visibleOffsetAt(2), 65536u);
  EXPECT_EQ(text.visibleOffsetAt(3), 42u);
  text.eraseVisibleOffsetPrefix(2);
  EXPECT_EQ(text.visibleOffsetAt(0), 65536u);
  EXPECT_EQ(text.visibleOffsetAt(1), 42u);
}

TEST(WordOffsets, TruncatedArenaAndInvalidFlagsAreRejected) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-bad-word-offsets.bin";
  for (const uint8_t flag : {1, 2}) {
    {
      HalFile output;
      ASSERT_TRUE(output.open(path.c_str(), "wb"));
      const uint8_t header[] = {1, 0, 0, flag, 2, 0};
      output.write(header, sizeof(header));
    }
    HalFile input;
    ASSERT_TRUE(input.open(path.c_str(), "rb"));
    EXPECT_EQ(TextBlock::deserialize(input), nullptr);
  }
}

TEST(WordOffsets, PartialParagraphExtractionKeepsRemainingAnchors) {
  ParsedText text(false);
  text.addWord("a", EpdFontFamily::REGULAR, false, false, 0);
  text.addWord("b", EpdFontFamily::REGULAR, false, false, 2);
  text.addWord("c", EpdFontFamily::REGULAR, false, false, 4);
  GfxRenderer renderer;
  unsigned words = 0;
  const auto inspect = [&](std::unique_ptr<TextBlock> line, uint32_t) {
    for (uint16_t i = 0; i < line->wordCount(); ++i) {
      EXPECT_EQ(line->wordVisibleOffset(i), words * 2);
      EXPECT_EQ(line->wordVisibleEndOffset(i), words * 2 + 1);
      ++words;
    }
  };
  text.layoutAndExtractLines(renderer, 0, 20, inspect, false);
  ASSERT_GT(words, 0u);
  ASSERT_LT(words, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, inspect);
  EXPECT_EQ(words, 3u);
}

TEST(WordOffsets, SourceEndsExcludeInsertedHyphensAfterRepeatedSplits) {
  ParsedText text(false, true);
  text.addWord("ééabcdef", EpdFontFamily::REGULAR, false, false, 0);
  GfxRenderer renderer;
  auto widths = text.calculateWordWidths(renderer, 0);
  setTestHyphenationBreak(4);
  const bool firstSplit = text.hyphenateWordAtIndex(0, 40, renderer, 0, widths, false);
  setTestHyphenationBreak(2);
  const bool secondSplit = firstSplit && text.hyphenateWordAtIndex(1, 24, renderer, 0, widths, false);
  setTestHyphenationBreak(0);
  ASSERT_TRUE(firstSplit);
  ASSERT_TRUE(secondSplit);
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, uint32_t) {
    ++lines;
    ASSERT_EQ(line->wordCount(), 3);
    EXPECT_STREQ(line->wordText(0), "éé-");
    EXPECT_STREQ(line->wordText(1), "ab-");
    EXPECT_STREQ(line->wordText(2), "cdef");
    EXPECT_EQ(line->wordVisibleOffset(0), 0u);
    EXPECT_EQ(line->wordVisibleEndOffset(0), 2u);
    EXPECT_EQ(line->wordVisibleOffset(1), 2u);
    EXPECT_EQ(line->wordVisibleEndOffset(1), 4u);
    EXPECT_EQ(line->wordVisibleOffset(2), 4u);
    EXPECT_EQ(line->wordVisibleEndOffset(2), 8u);
  });
  EXPECT_EQ(lines, 1u);
}

TEST(WordOffsets, SourceEndsIncludeStrippedSoftHyphensWithAndWithoutSplit) {
  for (const bool splitWord : {false, true}) {
    ParsedText text(false, true);
    text.addWord(
        "ab\xc2\xad"
        "cdef",
        EpdFontFamily::REGULAR, false, false, 10);
    GfxRenderer renderer;
    if (splitWord) {
      auto widths = text.calculateWordWidths(renderer, 0);
      setTestHyphenationBreak(4);  // after the source soft hyphen
      const bool split = text.hyphenateWordAtIndex(0, 24, renderer, 0, widths, false);
      setTestHyphenationBreak(0);
      ASSERT_TRUE(split);
    }
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, uint32_t) {
      ++lines;
      ASSERT_EQ(line->wordCount(), splitWord ? 2 : 1);
      EXPECT_EQ(line->wordVisibleOffset(0), 10u);
      if (splitWord) {
        EXPECT_STREQ(line->wordText(0), "ab-");
        EXPECT_EQ(line->wordVisibleEndOffset(0), 13u);
        EXPECT_EQ(line->wordVisibleOffset(1), 13u);
        EXPECT_EQ(line->wordVisibleEndOffset(1), 17u);
      } else {
        EXPECT_STREQ(line->wordText(0), "abcdef");
        EXPECT_EQ(line->wordVisibleEndOffset(0), 17u);
      }
    });
    EXPECT_EQ(lines, 1u);
  }
}
