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

// Calibre output from Kindle/MOBI sources may carry body text in <div>/<span> with no <p>
// beyond an empty filepos anchor, when the source MOBI was authored that way (it is not
// universal: a Project Gutenberg MOBI converts back to <p> paragraphs).
// Visible offsets below follow the layout parser: every body codepoint counts, including
// formatting whitespace between blocks.
//   0 "\n"   1 "\n"   2 "3"   3 " "   4 "\n"   5-13 "She drove"   14-26 " back to D.C."
//   27 "\n"  28-33 "\u201cWho?\u201d"   34 "\n"
constexpr char kKindleDivSpanFixture[] =
    "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body class=\"calibre\">\n"
    "<p id=\"filepos1\" class=\"calibre1\"></p>\n"
    "<div class=\"calibre8\"><span class=\"calibre9\">3</span></div><div class=\"calibre10\"> </div>\n"
    "<div class=\"calibre12\"><span class=\"calibre6\"><span class=\"bold\">She drove</span> back to "
    "D.C.</span></div>\n"
    "<div class=\"calibre19\"><span class=\"calibre6\">\xE2\x80\x9CWho?\xE2\x80\x9D</span></div>\n"
    "</body></html>";
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
}

TEST(KOReaderXPathResolver, KeepsParagraphOnlyResolutionUnchanged) {
  const auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 2),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[2]");
}

TEST(KOReaderXPathResolver, ResolvesTextOutsideParagraphElements) {
  const auto epub = epubWith(kKindleDivSpanFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 2),
            "/body/DocFragment[1]/body/div[1]/span[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 5),
            "/body/DocFragment[1]/body/div[3]/span[1]/span[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 20),
            "/body/DocFragment[1]/body/div[3]/span[1]/text()[1].6");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 28),
            "/body/DocFragment[1]/body/div[4]/span[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(
                epubWith("<html><body><div>not a paragraph or list item</div></body></html>"), 0, 0),
            "/body/DocFragment[1]/body/div[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, ResolvesProgressOutsideParagraphElements) {
  const auto epub = epubWith(kKindleDivSpanFixture);

  // Progress spans the text up to the last non-whitespace codepoint (34), so 1.0 lands at the
  // end of the final text node rather than in the trailing formatting newline.
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f),
            "/body/DocFragment[1]/body/div[4]/span[1]/text()[1].6");
  // ceil(0.5 * 34) = 17 -> " back to D.C." starts at 14 -> offset 3 within the node.
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 0.5f),
            "/body/DocFragment[1]/body/div[3]/span[1]/text()[1].3");
}

TEST(KOReaderXPathResolver, SkipsWhitespaceOnlyTextWhenResolvingOffsets) {
  const auto epub = epubWith(kKindleDivSpanFixture);

  // Formatting whitespace between blocks is never a usable anchor: resolve to the next text.
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 1),
            "/body/DocFragment[1]/body/div[1]/span[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 3),
            "/body/DocFragment[1]/body/div[3]/span[1]/span[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 4),
            "/body/DocFragment[1]/body/div[3]/span[1]/span[1]/text()[1].0");
  // Trailing whitespace with no text after it cannot be resolved.
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 34).empty());
}

TEST(KOReaderXPathResolver, ResolvesTextDirectlyInsideBody) {
  const auto bare = epubWith("<html><body>Hello world</body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(bare, 0, 6), "/body/DocFragment[1]/body/text()[1].6");

  // "\nHello" is one text run after the <p>: text()[1] at offset 1.
  const auto afterBlock = epubWith("<html><body><p>a</p>\nHello</body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(afterBlock, 0, 2),
            "/body/DocFragment[1]/body/text()[1].1");
}

TEST(KOReaderXPathResolver, ProgressEndExcludesTrailingWhitespaceInsideRun) {
  const auto epub = epubWith("<html><body><p>Text   </p></body></html>");
  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].4");
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

TEST(KOReaderXPathResolver, StrictRangeAcceptsWhitespaceChangesAfterBothEndpoints) {
  std::string first, last;
  for (const auto* suffix : {" ", "  ", "\n", "\t", "  cd", "\ncd", " \n\t", " <em>tail</em>"}) {
    SCOPED_TRACE(suffix);
    ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
        epubWith(std::string("<html><body><p>abcd") + suffix + "</p></body></html>"), 0, 0, 2, first, last, "ab"));
    EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].0");
    EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].2");
  }
}

TEST(KOReaderXPathResolver, StrictRangeAcceptsCompleteTextBeforeTrailingWhitespace) {
  std::string first, last;
  for (const auto* suffix : {" ", "  ", "\n", "\t", " \n\t"}) {
    ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
        epubWith(std::string("<html><body><p>ab cd") + suffix + "</p></body></html>"), 0, 0, 5, first, last, "ab cd"));
    EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].5");
  }
}

TEST(KOReaderXPathResolver, StrictRangeRejectsWhitespaceChangesBeforeEitherEndpoint) {
  struct Range {
    const char* content;
    uint32_t start;
    uint32_t end;
  };
  for (const auto& range :
       {Range{"ab  cd", 4, 6}, Range{"ab\ncd", 3, 5}, Range{" abcd", 1, 3}, Range{"ab  cd", 0, 5}, Range{"abcd ", 0, 5},
        Range{"abcd  ", 4, 5}, Range{"abcd\n", 0, 5}, Range{"abcd\t", 4, 5}}) {
    SCOPED_TRACE(range.content);
    std::string first = "stale", last = "stale";
    EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
        epubWith(std::string("<html><body><p>") + range.content + "</p></body></html>"), 0, range.start, range.end,
        first, last));
    EXPECT_TRUE(first.empty());
    EXPECT_TRUE(last.empty());
  }
}

TEST(KOReaderXPathResolver, StrictRangeKeepsUncertainTextNodeIndicesRejected) {
  std::string first, last;
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p> <em>x</em>abcd</p></body></html>"), 0, 2, 4, first, last));
  EXPECT_TRUE(first.empty());
  EXPECT_TRUE(last.empty());
}

TEST(KOReaderXPathResolver, StrictRangeValidatesInlineEndpointsIndependently) {
  const auto epub = epubWith("<html><body><p>ab<em>cd  </em>ef  </p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 4, first, last, "abcd"));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].0");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/em[1]/text()[1].2");
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 6, 8, first, last, "ef"));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[2].0");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[2].2");
}

TEST(KOReaderXPathResolver, StrictRangeAcceptsLaterTextAfterAnInternalSingleSpace) {
  const auto epub = epubWith("<html><body><p>ab cd  </p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 3, first, last, "ab "));
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].3");
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 0, 6, first, last, "ab cd "));
}

TEST(KOReaderXPathResolver, StrictRangeAcceptsLongParagraphWithTrailingSpace) {
  const std::string prefix(2845, 'x');
  const std::string quote = "selected quote";
  const auto epub = epubWith("<html><body><p>" + prefix + quote + std::string(512, 'y') + " </p></body></html>");
  std::string first, last;
  ASSERT_TRUE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, 2845, 2859, first, last, quote));
  EXPECT_EQ(first, "/body/DocFragment[1]/body/p[1]/text()[1].2845");
  EXPECT_EQ(last, "/body/DocFragment[1]/body/p[1]/text()[1].2859");
}

TEST(KOReaderXPathResolver, StrictRangePrefixSafetySurvivesStreamAndEntityBoundaries) {
  for (const size_t padding : {990u, 1000u, 1010u, 1020u, 1030u}) {
    SCOPED_TRACE(padding);
    const std::string prefix(padding, 'x');
    const auto epub = epubWith("<html><body><p>" + prefix + "😀&amp;éZ  \n\t</p></body></html>");
    std::string first, last;
    ASSERT_TRUE(
        ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epub, 0, padding, padding + 4, first, last, "😀&éZ"));
    const std::string path = "/body/DocFragment[1]/body/p[1]/text()[1].";
    EXPECT_EQ(first, path + std::to_string(padding));
    EXPECT_EQ(last, path + std::to_string(padding + 4));
  }
}

TEST(KOReaderXPathResolver, StrictRangeSafePrefixStillRequiresValidDocumentAndQuote) {
  for (const auto* document :
       {"<html><body><p>abcd </p><broken></body></html>", "<html><body><p>abcd </p>",
        "<html><body><p>abcd &nbsp;</p></body></html>", "<html><body><p>abcd </p></body></html>junk"}) {
    SCOPED_TRACE(document);
    std::string first = "stale", last = "stale";
    EXPECT_FALSE(
        ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(epubWith(document), 0, 0, 2, first, last, "ab"));
    EXPECT_TRUE(first.empty());
    EXPECT_TRUE(last.empty());
  }
  std::string first = "stale", last = "stale";
  EXPECT_FALSE(ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
      epubWith("<html><body><p>abcd </p></body></html>"), 0, 0, 2, first, last, "ac"));
  EXPECT_TRUE(first.empty());
  EXPECT_TRUE(last.empty());
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
