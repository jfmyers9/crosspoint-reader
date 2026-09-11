#include <HalStorage.h>
#include <gtest/gtest.h>

#include "ReadingStatsOutbox.h"

StorageStub Storage;

class ReadingStatsOutboxTest : public ::testing::Test {
 protected:
  ReadingStatsOutbox::Batch batch;
  void SetUp() override {
    Storage = {};
    std::strcpy(batch.bookHash, "0123456789abcdef0123456789abcdef");
    std::strcpy(batch.bootId, "0123456789abcdef");
    batch.count = 2;
    batch.events[0] = {1234, 0, 1, 20, 30};
    batch.events[1] = {123456789012ULL, 2200000000LL, 20, 20, 5};
  }
};

TEST_F(ReadingStatsOutboxTest, RoundTripsUndatedAndPost2038Events) {
  ASSERT_TRUE(ReadingStatsOutbox::save("/scope", batch));
  ASSERT_EQ(Storage.files.size(), 1);
  const auto& path = Storage.files.begin()->first;
  EXPECT_TRUE(path.ends_with(".bin"));
  ReadingStatsOutbox::Batch loaded;
  ASSERT_TRUE(ReadingStatsOutbox::load(path.c_str(), loaded));
  EXPECT_STREQ(loaded.bookHash, batch.bookHash);
  EXPECT_STREQ(loaded.bootId, batch.bootId);
  EXPECT_EQ(loaded.count, 2);
  EXPECT_EQ(loaded.events[0].startEpochSeconds, 0);
  EXPECT_EQ(loaded.events[1].startMonotonicMs, 123456789012ULL);
  EXPECT_EQ(loaded.events[1].startEpochSeconds, 2200000000LL);
  EXPECT_EQ(loaded.events[1].page, 20);
  EXPECT_EQ(loaded.events[1].totalPages, 20);
  EXPECT_EQ(loaded.events[1].durationSeconds, 5);
}

TEST_F(ReadingStatsOutboxTest, NeverOverwritesCompletedBatches) {
  ASSERT_TRUE(ReadingStatsOutbox::save("/scope", batch));
  const auto original = *Storage.files.begin();
  ASSERT_TRUE(ReadingStatsOutbox::save("/scope", batch));
  EXPECT_EQ(Storage.files.size(), 2);
  EXPECT_EQ(Storage.files.at(original.first), original.second);
}

TEST_F(ReadingStatsOutboxTest, AcceptsZeroProgressPage) {
  batch.events[0].page = 0;
  ASSERT_TRUE(ReadingStatsOutbox::save("/scope", batch));
  ReadingStatsOutbox::Batch loaded;
  ASSERT_TRUE(ReadingStatsOutbox::load(Storage.files.begin()->first.c_str(), loaded));
  EXPECT_EQ(loaded.events[0].page, 0);
}

TEST_F(ReadingStatsOutboxTest, RejectsEveryTruncationAndSingleByteCorruption) {
  ASSERT_TRUE(ReadingStatsOutbox::save("/scope", batch));
  const auto path = Storage.files.begin()->first;
  const auto original = Storage.files.at(path);
  ReadingStatsOutbox::Batch loaded;
  for (size_t length = 0; length < original.size(); ++length) {
    Storage.files[path] = std::vector<uint8_t>(original.begin(), original.begin() + length);
    EXPECT_FALSE(ReadingStatsOutbox::load(path.c_str(), loaded)) << length;
  }
  for (size_t byte = 0; byte < original.size(); ++byte) {
    Storage.files[path] = original;
    Storage.files[path][byte] ^= 1;
    EXPECT_FALSE(ReadingStatsOutbox::load(path.c_str(), loaded)) << byte;
  }
  Storage.files[path] = original;
  Storage.files[path].push_back(0);
  EXPECT_FALSE(ReadingStatsOutbox::load(path.c_str(), loaded));
}

TEST_F(ReadingStatsOutboxTest, FailedWritesReadbackAndRenameNeverPublish) {
  for (int fault = 0; fault < 3; ++fault) {
    Storage = {};
    Storage.shortWrite = fault == 0;
    Storage.corruptReadback = fault == 1;
    Storage.failRename = fault == 2;
    EXPECT_FALSE(ReadingStatsOutbox::save("/scope", batch));
    ASSERT_EQ(Storage.files.size(), 1);
    EXPECT_TRUE(Storage.files.begin()->first.ends_with(".tmp"));
  }
}

TEST_F(ReadingStatsOutboxTest, RejectsInvalidIdentityCountAndEventsBeforeWriting) {
  batch.bookHash[0] = '/';
  EXPECT_FALSE(ReadingStatsOutbox::save("/scope", batch));
  batch.bookHash[0] = '0';
  batch.count = 9;
  EXPECT_FALSE(ReadingStatsOutbox::save("/scope", batch));
  batch.count = 2;
  batch.events[0].totalPages = 0;
  EXPECT_FALSE(ReadingStatsOutbox::save("/scope", batch));
  EXPECT_TRUE(Storage.files.empty());
}
