#include <ContentOpfParser.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {
const std::string CACHE_PATH = "/preview";
const std::string CONTENT_PATH = "OEBPS/";

void feed(ContentOpfParser& parser, const std::string& xml, size_t chunkSize) {
  ASSERT_TRUE(parser.setup());
  for (size_t offset = 0; offset < xml.size(); offset += chunkSize) {
    const size_t length = std::min(chunkSize, xml.size() - offset);
    ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), length), length);
  }
}

TEST(LibraryOpfParser, PreservesChunkedTextAndSeparatesCreatorsOnce) {
  const std::string xml =
      "<opf:package xmlns:opf='urn:opf' xmlns:dc='urn:dc'><opf:metadata>"
      "<dc:title>A &amp; B</dc:title><dc:title>Subtitle</dc:title>"
      "<dc:creator>Ren\xc3\xa9 &amp; Jane</dc:creator><dc:creator>Lee</dc:creator>"
      "<dc:language>en</dc:language></opf:metadata></opf:package>";
  for (const size_t chunk : {1u, 7u, 1024u}) {
    ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
    feed(parser, xml, chunk);
    EXPECT_EQ(parser.title, "A & B");
    EXPECT_EQ(parser.author, "Ren\xc3\xa9 & Jane, Lee");
    EXPECT_EQ(parser.language, "en");
  }
}

TEST(LibraryOpfParser, FindsEpub2CoverWithoutReaderCacheAccess) {
  const std::string xml =
      "<package><metadata><meta name='cover' content='front'/></metadata><manifest>"
      "<item id='front' href='cover.jpg' media-type='image/jpeg'/>"
      "<item id='style' href='style.css' media-type='text/css'/>"
      "<item id='chapter' href='chapter.xhtml' media-type='application/xhtml+xml'/>"
      "</manifest><spine><itemref idref='chapter'/></spine>"
      "<guide><reference type='start' href='chapter.xhtml'/></guide></package>";
  cacheAccesses = 0;
  {
    ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
    feed(parser, xml, 17);
    EXPECT_EQ(parser.coverItemHref, "OEBPS/cover.jpg");
    EXPECT_TRUE(parser.cssFiles.empty());
  }
  EXPECT_EQ(cacheAccesses, 0);
}

TEST(LibraryOpfParser, FindsEpub3CoverProperty) {
  const std::string xml =
      "<package><metadata/><manifest>"
      "<item id='front' href='front.png' media-type='image/png' properties='cover-image scripted'/>"
      "</manifest><spine/></package>";
  ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
  feed(parser, xml, 11);
  EXPECT_EQ(parser.coverItemHref, "OEBPS/front.png");
}

TEST(LibraryOpfParser, PreservesWrapperForGuideFallback) {
  const std::string xml =
      "<package><metadata><meta name='cover' content='wrapper'/></metadata><manifest>"
      "<item id='wrapper' href='cover.xhtml' media-type='application/xhtml+xml'/>"
      "</manifest><spine/><guide><reference type='cover' href='cover.xhtml'/></guide></package>";
  ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
  feed(parser, xml, 19);
  EXPECT_TRUE(parser.coverItemHref.empty());
  EXPECT_EQ(parser.guideCoverPageHref, "OEBPS/cover.xhtml");
}

TEST(LibraryOpfParser, BoundsOversizedMetadataWhileStillFindingCover) {
  const std::string xml = "<package><metadata><title>" + std::string(4096, 'T') + "</title><creator>" +
                          std::string(4096, 'A') + "</creator><creator>Extra</creator><language>" +
                          std::string(4096, 'L') +
                          "</language></metadata><manifest><item id='c' href='cover.jpg' "
                          "properties='cover-image'/></manifest></package>";
  ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
  feed(parser, xml, 31);
  EXPECT_EQ(parser.title, std::string(512, 'T'));
  EXPECT_EQ(parser.author, std::string(512, 'A'));
  EXPECT_EQ(parser.language, std::string(512, 'L'));
  EXPECT_EQ(parser.coverItemHref, "OEBPS/cover.jpg");
}

TEST(LibraryOpfParser, AcceptsEmptyMetadataWithoutFabricatingDetails) {
  const std::string xml = "<package><metadata/><manifest/><spine/></package>";
  ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
  feed(parser, xml, 1024);
  EXPECT_TRUE(parser.title.empty());
  EXPECT_TRUE(parser.author.empty());
  EXPECT_TRUE(parser.coverItemHref.empty());
}

TEST(LibraryOpfParser, RejectsOversizedAttributesBeforeCopyingPaths) {
  const std::string xml = "<package><manifest><item id='c' href='" + std::string(1025, 'a') +
                          "' properties='cover-image'/></manifest></package>";
  ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
  ASSERT_TRUE(parser.setup());
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), 0u);
  EXPECT_TRUE(parser.coverItemHref.empty());
}

TEST(LibraryOpfParser, RejectsMalformedOrTruncatedXmlAtFinalChunk) {
  for (const std::string xml : {"<package><metadata></package>", "<package><metadata>"}) {
    ContentOpfParser parser(CACHE_PATH, CONTENT_PATH, xml.size(), nullptr, true, true);
    ASSERT_TRUE(parser.setup());
    EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), 0u);
    EXPECT_EQ(parser.write(static_cast<uint8_t>('x')), 0u);
  }
}
}  // namespace
