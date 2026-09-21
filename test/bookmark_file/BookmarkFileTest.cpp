#include <gtest/gtest.h>

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <PersistableStore.h>

#include "src/util/BookmarkFile.h"
#include "src/util/BookmarkUtil.h"

class BookmarkFileTest : public testing::Test {
 protected:
  const std::string book = "/books/test.epub";
  const std::string path = BookmarkUtil::getBookmarkPath(book);
  const std::string original = R"({"bookmarks":[{"xpath":"old","summary":"keep me","vo":5}]})";

  void SetUp() override { Storage = HalStorage{}; }

  BookmarkEntry highlight() const {
    BookmarkEntry entry{};
    entry.xpath = "/body/DocFragment[2]/body/p[1]/text().10";
    entry.summary = "A \"quote\"\nwith café and 日本語 \\ escaping";
    entry.name = "Selected passage";
    entry.percentage = 0.125f;
    entry.computedSpineIndex = 1;
    entry.computedChapterPageCount = 30;
    entry.computedChapterProgress = 4;
    entry.hasVisibleTextOffset = true;
    entry.visibleTextOffset = 10;
    entry.highlightEndOffset = 45;
    return entry;
  }

  void expectOriginal() {
    ASSERT_TRUE(Storage.exists(path.c_str()));
    EXPECT_EQ(Storage.files.at(path), original);
  }
};

TEST_F(BookmarkFileTest, MixedBookmarksAndHighlightsRoundTrip) {
  BookmarkEntry legacy{};
  legacy.xpath = "legacy";
  legacy.summary = "page preview";
  const auto selected = highlight();
  ASSERT_TRUE(BookmarkFile::save(book, {legacy, selected}));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(book, loaded));
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_FALSE(loaded[0].isHighlight());
  EXPECT_FALSE(loaded[0].hasVisibleTextOffset);
  EXPECT_EQ(loaded[0].summary, legacy.summary);
  EXPECT_TRUE(loaded[1].isHighlight());
  EXPECT_EQ(loaded[1].xpath, selected.xpath);
  EXPECT_EQ(loaded[1].summary, selected.summary);
  EXPECT_EQ(loaded[1].name, selected.name);
  EXPECT_FLOAT_EQ(loaded[1].percentage, selected.percentage);
  EXPECT_EQ(loaded[1].computedSpineIndex, selected.computedSpineIndex);
  EXPECT_EQ(loaded[1].computedChapterPageCount, selected.computedChapterPageCount);
  EXPECT_EQ(loaded[1].computedChapterProgress, selected.computedChapterProgress);
  EXPECT_EQ(loaded[1].visibleTextOffset, 10u);
  EXPECT_EQ(loaded[1].highlightEndOffset, 45u);
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, Storage.files.at(path)));
  EXPECT_TRUE(doc["bookmarks"][0]["he"].isNull());
  EXPECT_EQ(doc["bookmarks"][1]["he"].as<uint32_t>(), 45u);
  EXPECT_FALSE(Storage.exists((path + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((path + ".bak").c_str()));
}

TEST_F(BookmarkFileTest, MeasureJsonMatchesActualPersistableWriter) {
  JsonDocument doc;
  doc["quote"] = highlight().summary;
  doc["fraction"] = 0.123456789;
  doc["max"] = UINT32_MAX;
  doc["long"] = std::string(2048, 'x');
  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(path.c_str(), doc));
  EXPECT_EQ(Storage.files.at(path).size(), measureJson(doc));
  std::string expected;
  serializeJson(doc, expected);
  EXPECT_EQ(Storage.files.at(path), expected);
}

TEST_F(BookmarkFileTest, MissingAndMalformedHighlightEndsRemainBookmarks) {
  Storage.files[path] = R"({"bookmarks":[
    {"vo":10}, {"vo":10,"he":10}, {"vo":10,"he":9}, {"he":30},
    {"vo":10,"he":-1}, {"vo":10,"he":"30"}, {"vo":"10","he":30},
    {"vo":10,"he":4294967296}, {"vo":0,"he":1}]})";
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(book, loaded));
  ASSERT_EQ(loaded.size(), 9u);
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_FALSE(loaded[i].isHighlight()) << i;
    EXPECT_EQ(loaded[i].highlightEndOffset, 0u) << i;
  }
  EXPECT_TRUE(loaded.back().isHighlight());
}

TEST_F(BookmarkFileTest, ExistingCollectionsAndQuotesAreNotTruncated) {
  auto entry = highlight();
  entry.summary.assign(BookmarkEntry::MAX_HIGHLIGHT_QUOTE_LENGTH + 100, 'q');
  const std::vector<BookmarkEntry> entries(BookmarkFile::MAX_ENTRIES_FOR_NEW_HIGHLIGHT + 1, entry);
  ASSERT_TRUE(BookmarkFile::save(book, entries));
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(book, loaded));
  ASSERT_EQ(loaded.size(), entries.size());
  EXPECT_EQ(loaded.back().summary, entry.summary);
}

TEST_F(BookmarkFileTest, FailedWritePreservesPrimary) {
  Storage.files[path] = original;
  Storage.failWrite = true;
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  expectOriginal();
}

TEST_F(BookmarkFileTest, ShortSuccessfulWriteIsRejected) {
  Storage.files[path] = original;
  Storage.shortWriteReportsSuccess = true;
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  expectOriginal();
}

TEST_F(BookmarkFileTest, FailedStagedReadPreservesPrimary) {
  Storage.files[path] = original;
  Storage.failOpen = true;
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  expectOriginal();
}

TEST_F(BookmarkFileTest, FailedBackupRenamePreservesPrimary) {
  Storage.files[path] = original;
  Storage.failedRenameSources.insert(path);
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  expectOriginal();
}

TEST_F(BookmarkFileTest, FailedPromotionRollsBackPrimary) {
  Storage.files[path] = original;
  Storage.failedRenameSources.insert(path + ".tmp");
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  expectOriginal();
  EXPECT_FALSE(Storage.exists((path + ".bak").c_str()));
}

TEST_F(BookmarkFileTest, FailedRollbackLeavesRecoverableBackup) {
  Storage.files[path] = original;
  Storage.failedRenameSources = {path + ".tmp", path + ".bak"};
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  EXPECT_FALSE(Storage.exists(path.c_str()));
  ASSERT_EQ(Storage.files.at(path + ".bak"), original);
  Storage.failedRenameSources.clear();
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(book, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded.front().summary, "keep me");
  expectOriginal();
}

TEST_F(BookmarkFileTest, FailedRecoveryDoesNotReadTemporaryFile) {
  Storage.files[path + ".bak"] = original;
  Storage.files[path + ".tmp"] = R"({"bookmarks":[]})";
  Storage.failedRenameSources.insert(path + ".bak");
  std::vector<BookmarkEntry> loaded{highlight()};
  EXPECT_FALSE(BookmarkFile::load(book, loaded));
  EXPECT_TRUE(loaded.empty());
  EXPECT_EQ(Storage.files.at(path + ".bak"), original);
}

TEST_F(BookmarkFileTest, FailedStaleBackupRemovalPreservesPrimary) {
  Storage.files[path] = original;
  Storage.files[path + ".bak"] = "stale";
  Storage.failedRemovals.insert(path + ".bak");
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  expectOriginal();
}

TEST_F(BookmarkFileTest, FailedBackupCleanupDoesNotFailSuccessfulSave) {
  Storage.files[path] = original;
  Storage.failedRemovals.insert(path + ".bak");
  ASSERT_TRUE(BookmarkFile::save(book, {highlight()}));
  EXPECT_EQ(Storage.files.at(path + ".bak"), original);
  std::vector<BookmarkEntry> loaded;
  ASSERT_TRUE(BookmarkFile::load(book, loaded));
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_TRUE(loaded.front().isHighlight());
}

TEST_F(BookmarkFileTest, CorruptOrInvalidDocumentCannotBeOverwritten) {
  for (const std::string corrupt : {"", "{broken", "{}", "[]", R"({"bookmarks":null})",
                                    R"({"bookmarks":[null]})", R"({"bookmarks":[42]})"}) {
    Storage.files[path] = corrupt;
    std::vector<BookmarkEntry> loaded;
    EXPECT_FALSE(BookmarkFile::load(book, loaded)) << corrupt;
    EXPECT_FALSE(BookmarkFile::save(book, {highlight()})) << corrupt;
    EXPECT_EQ(Storage.files.at(path), corrupt);
    EXPECT_EQ(Storage.writes, 0u);
  }
}

TEST_F(BookmarkFileTest, ReadFailureCannotReplaceExistingFile) {
  Storage.files[path] = original;
  Storage.failedReads.insert(path);
  std::vector<BookmarkEntry> loaded;
  EXPECT_FALSE(BookmarkFile::load(book, loaded));
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  EXPECT_EQ(Storage.writes, 0u);
  expectOriginal();
}

TEST_F(BookmarkFileTest, CorruptBackupCannotBeOverwrittenWhenPrimaryMissing) {
  Storage.files[path + ".bak"] = "broken";
  EXPECT_FALSE(BookmarkFile::save(book, {highlight()}));
  EXPECT_EQ(Storage.files.at(path + ".bak"), "broken");
  EXPECT_EQ(Storage.writes, 0u);
}

TEST_F(BookmarkFileTest, MissingFileCanBeCreatedAndEmptyListCanBeSaved) {
  std::vector<BookmarkEntry> loaded{highlight()};
  EXPECT_FALSE(BookmarkFile::load(book, loaded));
  EXPECT_TRUE(loaded.empty());
  ASSERT_TRUE(BookmarkFile::save(book, {highlight()}));
  ASSERT_TRUE(BookmarkFile::save(book, {}));
  ASSERT_TRUE(BookmarkFile::load(book, loaded));
  EXPECT_TRUE(loaded.empty());
}
