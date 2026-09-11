#include <gtest/gtest.h>
#include <HalStorage.h>
#include <ReadingStatus.h>
#include <limits>

using namespace ReadingStatus;
class ReadingStatusTest : public testing::Test {
 protected:
  void SetUp() override { testFiles.clear(); testWrites = 0; failRename = false; }
};

TEST_F(ReadingStatusTest, MissingIsUnknown) {
  EXPECT_EQ(load("/book.epub").state, State::Unknown);
}
TEST_F(ReadingStatusTest, TracksCurrentProgressAndSkipsIdenticalWrites) {
  ASSERT_TRUE(update("/book.epub", .375f));
  EXPECT_EQ(load("/book.epub").percent, 37);
  const int writes = testWrites;
  ASSERT_TRUE(update("/book.epub", .379f));
  EXPECT_EQ(testWrites, writes);
  ASSERT_TRUE(update("/book.epub", .2f));
  EXPECT_EQ(load("/book.epub").percent, 20);
}
TEST_F(ReadingStatusTest, FinishedIsStickyUntilManualUnread) {
  ASSERT_TRUE(update("/book.epub", 1, true));
  ASSERT_TRUE(update("/book.epub", .1f));
  EXPECT_EQ(load("/book.epub").state, State::Finished);
  ASSERT_TRUE(mark("/book.epub", State::Unread));
  EXPECT_EQ(load("/book.epub").state, State::Unread);
  ASSERT_TRUE(update("/book.epub", .1f));
  EXPECT_EQ(load("/book.epub").state, State::Reading);
}
TEST_F(ReadingStatusTest, NearEndDoesNotImplyFinished) {
  ASSERT_TRUE(update("/book.epub", 1));
  EXPECT_EQ(load("/book.epub").state, State::Reading);
  EXPECT_EQ(load("/book.epub").percent, 99);
}
TEST_F(ReadingStatusTest, RejectsInvalidFractions) {
  EXPECT_FALSE(update("/book.epub", -1));
  EXPECT_FALSE(update("/book.epub", 2));
  EXPECT_FALSE(update("/book.epub", std::numeric_limits<float>::quiet_NaN()));
  EXPECT_EQ(load("/book.epub").state, State::Unknown);
}
TEST_F(ReadingStatusTest, CorruptOrWrongSourceRecordIsUnknown) {
  ASSERT_TRUE(mark("/book.epub", State::Finished));
  auto& bytes = testFiles.begin()->second;
  bytes.back() ^= 1;
  EXPECT_EQ(load("/book.epub").state, State::Unknown);
  bytes.resize(4);
  EXPECT_EQ(load("/book.epub").state, State::Unknown);
}
TEST_F(ReadingStatusTest, RecoversBackupAcrossInterruptedReplace) {
  ASSERT_TRUE(update("/book.epub", .5f));
  const std::string filename = testFiles.begin()->first;
  const std::string backup = filename.substr(0, filename.size() - 4) + ".bak";
  ASSERT_TRUE(Storage.rename(filename.c_str(), backup.c_str()));
  EXPECT_EQ(load("/book.epub").percent, 50);
  ASSERT_TRUE(update("/book.epub", .6f));
  EXPECT_EQ(load("/book.epub").percent, 60);
}
TEST_F(ReadingStatusTest, MovePreservesFinishedAtNewPath) {
  ASSERT_TRUE(mark("/book.epub", State::Finished));
  ASSERT_TRUE(ReadingStatus::move("/book.epub", "/Read/book.epub"));
  EXPECT_EQ(load("/Read/book.epub").state, State::Finished);
  EXPECT_EQ(load("/book.epub").state, State::Unknown);
}
TEST_F(ReadingStatusTest, FailedReplacePreservesOldStatus) {
  ASSERT_TRUE(update("/book.epub", .5f));
  failRename = true;
  EXPECT_FALSE(update("/book.epub", .6f));
  EXPECT_EQ(load("/book.epub").percent, 50);
}
TEST_F(ReadingStatusTest, FailedReplacePreservesBackupWhenCanonicalIsCorrupt) {
  ASSERT_TRUE(update("/book.epub", .5f));
  const std::string filename = testFiles.begin()->first;
  const std::string backup = filename.substr(0, filename.size() - 4) + ".bak";
  testFiles[backup] = testFiles[filename];
  testFiles[filename].resize(4);
  ASSERT_EQ(load("/book.epub").percent, 50);
  failRename = true;
  EXPECT_FALSE(update("/book.epub", .6f));
  EXPECT_EQ(load("/book.epub").state, State::Reading);
  EXPECT_EQ(load("/book.epub").percent, 50);
  EXPECT_TRUE(Storage.exists(backup.c_str()));
  failRename = false;
  ASSERT_TRUE(update("/book.epub", .6f));
  EXPECT_EQ(load("/book.epub").percent, 60);
  EXPECT_FALSE(Storage.exists(backup.c_str()));
}
