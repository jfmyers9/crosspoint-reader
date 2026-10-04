#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ChapterXPathResolver.h"

namespace {
std::shared_ptr<Epub> epubWith(std::string xhtml) {
  std::vector<std::string> spine;
  spine.push_back(std::move(xhtml));
  return std::make_shared<Epub>(std::move(spine));
}

constexpr char kNestedFixture[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body><div><section><p>Alpha bravo</p><p>Second <em>nested</em> tail</p></section></div></body></html>)";

constexpr char kNonVisibleInlineFixture[] =
    R"(<html><body><p><RP><span>hidden</span></RP>Visible text</p></body></html>)";

constexpr char kCommentBoundaryFixture[] = R"(<html><body><p>before<!--comment-->after</p></body></html>)";
constexpr char kProcessingInstructionBoundaryFixture[] = R"(<html><body><p>before<?marker?>after</p></body></html>)";
constexpr char kCdataBoundaryFixture[] = R"(<html><body><p>before<![CDATA[middle]]>after</p></body></html>)";
constexpr char kHiddenCdataFixture[] = R"(<html><body><p>before<rp><![CDATA[hidden]]></rp>after</p></body></html>)";
}  // namespace

TEST(KOReaderXPathResolver, ResolvesExactOffsetWithFullAncestry) {
  const auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 6),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[1]/text()[1].6");
}

TEST(KOReaderXPathResolver, PreservesNestedInlineTextNode) {
  const auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 26),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[2]/text()[2].2");
}

TEST(KOReaderXPathResolver, EmitsDetailedAnchorForOffsetZero) {
  const auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, IgnoresNestedNonVisibleInlineText) {
  const auto epub = epubWith(kNonVisibleInlineFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, ResolvesProgressAfterNestedNonVisibleInlineText) {
  const auto epub = epubWith(kNonVisibleInlineFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].12");
}

TEST(KOReaderXPathResolver, CountsUtf8CodepointsInsteadOfBytes) {
  const auto epub = epubWith(
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><html><body><p>A\xC3\xA9\xE4\xB8\xAD"
      "B</p></body></html>");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 3),
            "/body/DocFragment[1]/body/p[1]/text()[1].3");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundComments) {
  const auto epub = epubWith(kCommentBoundaryFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 6),
            "/body/DocFragment[1]/body/p[1]/text()[2].0");
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 11).empty());
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundProcessingInstructions) {
  const auto epub = epubWith(kProcessingInstructionBoundaryFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundCdata) {
  const auto epub = epubWith(kCdataBoundaryFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 13),
            "/body/DocFragment[1]/body/p[1]/text()[3].1");
}

TEST(KOReaderXPathResolver, DoesNotCreateNodesBeforeFirstComment) {
  const auto epub = epubWith(R"(<html><body><p><!--comment-->text</p></body></html>)");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, DoesNotCreateNodesBeforeFirstProcessingInstruction) {
  const auto epub = epubWith(R"(<html><body><p><?marker?>text</p></body></html>)");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, DoesNotCreateNodesBeforeFirstCdata) {
  const auto epub = epubWith(R"(<html><body><p><![CDATA[text]]></p></body></html>)");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, CountsVisibleCdataAndIgnoresHiddenCdata) {
  const auto visible = epubWith(kCdataBoundaryFixture);
  const auto hidden = epubWith(kHiddenCdataFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(visible, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(hidden, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, ReturnsEmptyForUnusableContent) {
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith(""), 0, 0).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epubWith("<html><body><p>broken"), 0, 100).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(
                  epubWith("<html><body><div>not a paragraph or list item</div></body></html>"), 0, 0)
                  .empty());
}

TEST(KOReaderXPathResolver, KeepsParagraphOnlyResolutionUnchanged) {
  const auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 2),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[2]");
}

TEST(KOReaderXPathResolver, StrictRangeUsesExclusiveEndAtEndOfSpine) {
  const auto epub = epubWith("<html><body><p>abc</p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 3, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].0");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].3");
  for (const auto [start, end] :
       std::vector<std::pair<uint32_t, uint32_t>>{{0, 4}, {3, 4}, {2, 2}, {3, 1}, {0, UINT32_MAX}}) {
    first = last = "stale";
    EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, start, end, first, last));
    EXPECT_TRUE(first.empty());
    EXPECT_TRUE(last.empty());
  }
}

TEST(KOReaderXPathResolver, StrictRangeUsesOppositeAffinitiesAtInlineBoundaries) {
  const auto epub = epubWith("<html><body><p>a<em>bc</em>d</p><p>ef</p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 1, 3, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/em[1]/text()[1].0");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/em[1]/text()[1].2");
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 3, 5, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[2].0");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[2]/text()[1].1");
}

TEST(KOReaderXPathResolver, StrictRangeCountsAllBodyTextLikeRenderer) {
  const auto epub = epubWith(
      "<html><head><title>ignored</title></head><body>\n<h1>Hi</h1>\n"
      "<div>abc</div><p><rp>hidden</rp>de</p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 4, 9, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/div[1]/text()[1].0");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].2");
}

TEST(KOReaderXPathResolver, StrictRangeUsesCrengineCodepointsNotUtf16WithoutNormalizing) {
  const auto epub = epubWith("<html><body><p>A😀e\xcc\x81中Z</p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 1, 2, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].1");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].2");
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 2, 6, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].2");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].6");
}

TEST(KOReaderXPathResolver, StrictRangePreservesOffsetsAcrossStreamAndEntityChunks) {
  const auto epub = epubWith("<html><body><p>" + std::string(1008, 'x') + "😀&amp;éZ</p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 1009, 1012, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].1009");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].1012");
}

TEST(KOReaderXPathResolver, StrictRangeRejectsMalformedSuffixAndUnresolvedEntities) {
  std::string first, last;
  for (const auto& document :
       {"<html><body><p>abc</p><broken></body></html>", "<html><body><p>abc</p>",
        "<html><body><p>abc&nbsp;def</p></body></html>", "<html><body><p>abc</p></body></html>junk", ""}) {
    first = last = "stale";
    EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epubWith(document), 0, 0, 2, first, last));
    EXPECT_TRUE(first.empty());
    EXPECT_TRUE(last.empty());
  }
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(nullptr, 0, 0, 1, first, last));
  EXPECT_FALSE(
      ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epubWith(kNestedFixture), -1, 0, 1, first, last));
  EXPECT_FALSE(
      ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epubWith(kNestedFixture), 1, 0, 1, first, last));
}

TEST(KOReaderXPathResolver, StrictRangeKeepsMarkupTextNodeBoundaries) {
  std::string first, last;
  for (const auto* document : {kCommentBoundaryFixture, kProcessingInstructionBoundaryFixture, kCdataBoundaryFixture}) {
    ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epubWith(document), 0, 6, 8, first, last));
    EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[2].0");
    EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[2].2");
  }
}

TEST(KOReaderXPathResolver, StrictRangeRejectsCssDependentWhitespaceCoordinates) {
  std::string first, last;
  for (const auto* content : {"ab  cd", "ab\ncd", " abcd", "abcd ", " \n<em>x</em>abcd"}) {
    EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
        epubWith(std::string("<html><body><p>") + content + "</p></body></html>"), 0, 0, 2, first, last));
    EXPECT_TRUE(first.empty());
    EXPECT_TRUE(last.empty());
  }
  // The preceding whitespace-only node may be absent from CRengine's DOM.
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p> <em>x</em>abcd</p></body></html>"), 0, 2, 4, first, last));
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p>ab cd</p></body></html>"), 0, 3, 5, first, last));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].3");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].5");
}

TEST(KOReaderXPathResolver, StrictRangeChecksQuoteToRejectStaleOffsets) {
  const auto epub = epubWith("<html><body><p>alpha beta gamma</p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 6, 10, first, last, "beta"));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].6");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].10");
  for (const auto* quote : {"zeta", "bet", "betas", "Beta", "alpha", " \t\r\n"}) {
    first = last = "stale";
    EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 6, 10, first, last, quote));
    EXPECT_TRUE(first.empty());
    EXPECT_TRUE(last.empty());
  }
}

TEST(KOReaderXPathResolver, StrictRangeQuoteCollapsesAsciiWhitespaceOnBothSides) {
  const auto epub = epubWith("<html><body><p>alpha</p>\n \t<p>beta</p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 12, first, last,
                                                                        " \nalpha\t\v\f\r beta  "));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].0");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[2]/text()[1].4");
  // Quote normalization does not bypass the stricter XPointer whitespace-safety checks.
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p>alpha  beta</p></body></html>"), 0, 0, 11, first, last, "alpha beta"));
}

TEST(KOReaderXPathResolver, StrictRangeQuoteIsOtherwiseUtf8Exact) {
  const auto epub = epubWith("<html><body><p>😀e\xcc\x81&amp;中</p></body></html>");
  std::string first, last;
  EXPECT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 5, first, last, "😀e\xcc\x81&中"));
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 5, first, last, "😀é&中"));
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p>a&#160;b</p></body></html>"), 0, 0, 3, first, last, "a b"));
}

TEST(KOReaderXPathResolver, StrictRangeQuoteHandlesStreamChunksAndByteLimit) {
  const std::string prefix(1008, 'x');
  const std::string quote = prefix + "😀&éZ";
  const auto epub = epubWith("<html><body><p>" + prefix + "😀&amp;éZ</p></body></html>");
  std::string first, last;
  EXPECT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 1012, first, last, quote));
  const std::string maximum(2048, 'x');
  EXPECT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p>" + maximum + "</p></body></html>"), 0, 0, 2048, first, last, maximum));
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p>" + maximum + "x</p></body></html>"), 0, 0, 2049, first, last, maximum + "x"));
  // The source range also has a byte cap, even if whitespace collapse makes it tiny.
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p>a</p>" + std::string(2047, ' ') + "<p>b</p></body></html>"), 0, 0, 2049, first, last,
      "a b"));
  EXPECT_TRUE(first.empty());
  EXPECT_TRUE(last.empty());
}
