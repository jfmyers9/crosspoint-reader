#include <Epub.h>
#include <gtest/gtest.h>

#include "LibraryBookDetails.h"

namespace {
constexpr const char* BOOK = "/books/example.epub";
constexpr int HEIGHT = 96;
class LibraryDetails : public testing::Test {
 protected:
  LibraryBookDetails details;
  void SetUp() override {
    testFiles.clear();
    testFiles[BOOK].bytes.resize(100);
    metadataReads = thumbnailAttempts = 0;
    decodeSucceeds = true;
    testCover = "cover.jpg";
    testTitle = "Book title";
  }
  std::string cache() { return Epub(BOOK, "/.crosspoint/library").getCachePath() + "/details_96.bin"; }
  std::string thumb() { return Epub(BOOK, "/.crosspoint/library").getThumbBmpPath(HEIGHT); }
  void load() { ASSERT_TRUE(loadLibraryBookDetails(BOOK, HEIGHT, details)); }
};

TEST_F(LibraryDetails, CachedTopDownCoverAvoidsReparsing) {
  load();
  EXPECT_EQ(details.title, "Book title");
  EXPECT_EQ(details.coverPath, thumb());
  load();
  EXPECT_EQ(metadataReads, 1);
  EXPECT_EQ(thumbnailAttempts, 1);
}

TEST_F(LibraryDetails, SourceSizeAndTimestampInvalidate) {
  load();
  ++testFiles[BOOK].time;
  testTitle = "Replacement";
  load();
  EXPECT_EQ(details.title, "Replacement");
  testFiles[BOOK].bytes.push_back(0);
  load();
  EXPECT_EQ(metadataReads, 3);
  EXPECT_EQ(thumbnailAttempts, 3);
}

TEST_F(LibraryDetails, MissingAndUnsupportedCoversAreRemembered) {
  testCover.clear();
  load();
  load();
  EXPECT_TRUE(details.coverPath.empty());
  EXPECT_EQ(metadataReads, 1);
  EXPECT_EQ(thumbnailAttempts, 0);
  ++testFiles[BOOK].time;
  testCover = "cover.gif";
  load();
  load();
  EXPECT_EQ(metadataReads, 2);
  EXPECT_EQ(thumbnailAttempts, 0);
}

TEST_F(LibraryDetails, DecoderFailureIsRetried) {
  decodeSucceeds = false;
  load();
  EXPECT_EQ(details.title, "Book title");
  EXPECT_TRUE(details.coverPath.empty());
  EXPECT_FALSE(testFiles.count(cache()));
  decodeSucceeds = true;
  load();
  EXPECT_EQ(thumbnailAttempts, 2);
  EXPECT_EQ(details.coverPath, thumb());
}

TEST_F(LibraryDetails, TruncatedCacheAndThumbnailAreRebuilt) {
  load();
  testFiles[cache()].bytes.resize(4);
  load();
  EXPECT_EQ(metadataReads, 2);
  testFiles[thumb()].bytes.resize(54);
  load();
  EXPECT_EQ(metadataReads, 3);
}

TEST_F(LibraryDetails, UnboundedCacheTextLengthIsRejected) {
  load();
  auto& bytes = testFiles[cache()].bytes;
  bytes[18] = bytes[19] = 0xff;
  load();
  EXPECT_EQ(metadataReads, 2);
  EXPECT_EQ(details.title, "Book title");
}

TEST_F(LibraryDetails, InvalidBitmapHeaderIsRebuilt) {
  load();
  for (const int offset : {14, 26, 46}) {
    testFiles[thumb()].bytes[offset] = 3;
    load();
    EXPECT_EQ(details.coverPath, thumb());
  }
  EXPECT_EQ(metadataReads, 4);
  EXPECT_EQ(thumbnailAttempts, 4);
}

TEST_F(LibraryDetails, TruncatedUtf8TitleHasNoIncompleteCodepoint) {
  testTitle = std::string(511, 'a') + "\xc3\xa9";
  load();
  EXPECT_EQ(details.title, std::string(511, 'a'));
}
}  // namespace
