#include <BookOrbitAnnotations.h>
#include <BookOrbitAnnotationsClient.h>
#include <BookOrbitHighlightReader.h>
#include <BookOrbitStatsClient.h>
#include <BookmarkEntry.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <MD5Builder.h>
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

StorageStub Storage;
CredentialStub credentialStub;
HalClockStub halClock;
bool epubLoads = true, rangeResolves = true;
int64_t stubMonotonicMs = 0;
std::string lastExpectedQuote;
std::string tocTitle;
int tocIndex = 0, lastTocSpine = -1;

namespace {
std::vector<BookmarkEntry> entries;
std::vector<std::string> uploadDates, updateDates, uploadNotes;
std::vector<std::string> uploadChapters;
std::vector<uint32_t> uploadPages;
std::string savedChapter;
bool readable = true, accepted = true, unmatched = false, failAcknowledgement = false;
int64_t entryCostMs = 0;
constexpr char BOOK[] = "/Books/test.epub";
}  // namespace

bool BookOrbitHighlightReader::visit(const std::string&, Visitor visitor, void* context) {
  if (!readable) return false;
  for (const auto& entry : entries) {
    stubMonotonicMs += entryCostMs;
    if (entry.isHighlight() && visitor && !visitor(context, entry, savedChapter.c_str())) return false;
  }
  return true;
}

const char* BookOrbitStatsClient::deviceId() { return "crosspoint-test"; }
bool BookOrbitAnnotationsClient::lastUploadWasUnmatched() { return unmatched; }
bool BookOrbitAnnotationsClient::upload(const char*, const char* created, const char* updated, const char*, const char*,
                                        const char*, const char* note, const char* chapter, uint32_t pageNumber) {
  uploadDates.emplace_back(created);
  updateDates.emplace_back(updated);
  uploadNotes.emplace_back(note);
  uploadChapters.emplace_back(chapter);
  uploadPages.push_back(pageNumber);
  if (failAcknowledgement) Storage.failRename = true;
  return accepted;
}

class BookOrbitAnnotationsTest : public testing::Test {
 protected:
  void SetUp() override {
    Storage = {};
    credentialStub = {};
    halClock = {};
    stubMonotonicMs = 0;
    lastExpectedQuote.clear();
    entryCostMs = 0;
    epubLoads = rangeResolves = readable = accepted = true;
    unmatched = failAcknowledgement = false;
    uploadDates.clear();
    updateDates.clear();
    uploadNotes.clear();
    uploadChapters.clear();
    uploadPages.clear();
    savedChapter.clear();
    tocTitle = "Chapter from TOC";
    tocIndex = 0;
    lastTocSpine = -1;
    entries.clear();
    BookmarkEntry entry{};
    entry.hasVisibleTextOffset = true;
    entry.visibleTextOffset = 4;
    entry.highlightEndOffset = 12;
    entry.summary = "selected quote";
    entries.push_back(entry);
  }
  static auto sync() { return BookOrbitAnnotations::sync(BOOK); }
};

TEST_F(BookOrbitAnnotationsTest, ExportsOnceAndPreservesOriginalBookmarks) {
  Storage.files["/.crosspoint/bookmarks/Books_test.json"] = {'k', 'e', 'e', 'p'};
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 1u);
  EXPECT_EQ(lastExpectedQuote, entries[0].summary);
  EXPECT_EQ(updateDates[0], uploadDates[0]);
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 1u);
  EXPECT_EQ(Storage.files["/.crosspoint/bookmarks/Books_test.json"], (std::vector<uint8_t>{'k', 'e', 'e', 'p'}));
}

TEST_F(BookOrbitAnnotationsTest, UsesSavedChapterWithoutTurningItIntoANote) {
  savedChapter = "Saved clipping chapter";
  entries[0].name = "User note";
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadChapters.front(), savedChapter);
  EXPECT_EQ(uploadNotes.front(), "User note");
  EXPECT_EQ(lastTocSpine, -1);
}

TEST_F(BookOrbitAnnotationsTest, LegacyHighlightsUseTheirSpineTocTitle) {
  entries[0].computedSpineIndex = 7;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(lastTocSpine, 7);
  EXPECT_EQ(uploadChapters.front(), tocTitle);
  EXPECT_TRUE(uploadNotes.front().empty());
}

TEST_F(BookOrbitAnnotationsTest, MissingTocDoesNotInventAChapterOrBlockExport) {
  tocIndex = -1;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_TRUE(uploadChapters.front().empty());
}

TEST_F(BookOrbitAnnotationsTest, BoundsTocTitleAtACompleteUtf8Character) {
  tocTitle = std::string(499, 'a') + "\xe7\xab\xa0";
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadChapters.front(), std::string(499, 'a'));
}

TEST_F(BookOrbitAnnotationsTest, ChapterChangeKeepsIdentityAndAdvancesRevision) {
  savedChapter = "Chapter one";
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  savedChapter = "Corrected chapter";
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 2u);
  EXPECT_EQ(uploadDates[0], uploadDates[1]);
  EXPECT_GT(updateDates[1], updateDates[0]);
  EXPECT_EQ(uploadChapters.back(), savedChapter);
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 2u);
}

TEST_F(BookOrbitAnnotationsTest, ExportsSavedPageAsOneBasedWithinChapter) {
  entries[0].computedSpineIndex = 7;
  entries[0].computedChapterPageCount = 20;
  entries[0].computedChapterProgress = 0;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadPages.back(), 1u);
  entries[0].computedChapterProgress = 19;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadPages.back(), 20u);
  ASSERT_EQ(uploadDates.size(), 2u);
  EXPECT_EQ(uploadDates[0], uploadDates[1]);
  EXPECT_GT(updateDates[1], updateDates[0]);
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 2u);
}

TEST_F(BookOrbitAnnotationsTest, UnknownOrOutOfRangePageDoesNotInventPageOne) {
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadPages.back(), 0u);
  entries[0].computedChapterPageCount = 9;
  entries[0].computedChapterProgress = 9;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 1u);
  entries[0].computedChapterProgress = UINT16_MAX;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 1u);
}

TEST_F(BookOrbitAnnotationsTest, MaximumSavedPageDoesNotOverflow) {
  entries[0].computedChapterPageCount = UINT16_MAX;
  entries[0].computedChapterProgress = UINT16_MAX - 1;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadPages.back(), UINT16_MAX);
}

TEST_F(BookOrbitAnnotationsTest, BackfillsOldAcknowledgementWithSameIdentityAndRetriesOnce) {
  entries[0].computedChapterPageCount = 10;
  entries[0].computedChapterProgress = 3;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  MD5Builder md5;
  md5.begin();
  md5.add("1:0:4:12:14:selected quote:");
  md5.calculate();
  const std::string oldRevision = md5.toString();
  bool found = false;
  for (auto& [path, bytes] : Storage.files) {
    if (!path.ends_with(".state")) continue;
    found = true;
    long long created = 0, updated = 0;
    ASSERT_EQ(std::sscanf(std::string(bytes.begin(), bytes.end()).c_str(), "BOH1 %lld %lld", &created, &updated), 2);
    const std::string oldState = "BOH1 " + std::to_string(created) + " " + std::to_string(updated) + " " + oldRevision +
                                 " " + oldRevision + "\n";
    bytes.assign(oldState.begin(), oldState.end());
  }
  ASSERT_TRUE(found);
  accepted = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  accepted = true;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 3u);
  EXPECT_EQ(uploadDates[0], uploadDates[1]);
  EXPECT_EQ(uploadDates[1], uploadDates[2]);
  EXPECT_GT(updateDates[1], updateDates[0]);
  EXPECT_EQ(updateDates[1], updateDates[2]);
  EXPECT_EQ(uploadChapters.back(), tocTitle);
  EXPECT_EQ(uploadPages.back(), 4u);
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 3u);
}

TEST_F(BookOrbitAnnotationsTest, LostResponseRetriesSameIdentityAndRevision) {
  accepted = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  accepted = true;
  halClock.epoch += 120;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 2u);
  EXPECT_EQ(uploadDates[0], uploadDates[1]);
  EXPECT_EQ(updateDates[0], updateDates[1]);
}

TEST_F(BookOrbitAnnotationsTest, LostAcknowledgementSaveRetriesSameIdentity) {
  failAcknowledgement = true;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  failAcknowledgement = false;
  Storage.failRename = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 2u);
  EXPECT_EQ(uploadDates[0], uploadDates[1]);
}

TEST_F(BookOrbitAnnotationsTest, LabelEditKeepsIdentityAndAdvancesRevisionEvenWithinSameSecond) {
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  entries[0].name = "my label";
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 2u);
  EXPECT_EQ(uploadDates[0], uploadDates[1]);
  EXPECT_GT(updateDates[1], updateDates[0]);
  EXPECT_EQ(uploadNotes[1], "my label");
  entries[0].name.clear();
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_TRUE(uploadNotes.back().empty());
}

TEST_F(BookOrbitAnnotationsTest, SameSecondImportsHaveDifferentIdentities) {
  entries.push_back(entries[0]);
  entries[1].visibleTextOffset = 20;
  entries[1].highlightEndOffset = 30;
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 2u);
  EXPECT_NE(uploadDates[0], uploadDates[1]);
}

TEST_F(BookOrbitAnnotationsTest, RevertAfterUnacknowledgedEditSendsCorrectiveRevision) {
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  entries[0].name = "possibly accepted remotely";
  accepted = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  entries[0].name.clear();
  accepted = true;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  ASSERT_EQ(uploadDates.size(), 3u);
  EXPECT_EQ(uploadDates[0], uploadDates[2]);
  EXPECT_GT(updateDates[2], updateDates[1]);
  EXPECT_TRUE(uploadNotes[2].empty());
}

TEST_F(BookOrbitAnnotationsTest, BudgetResumesPastAcknowledgedPrefix) {
  entries.resize(40, entries[0]);
  for (size_t i = 0; i < entries.size(); ++i) {
    entries[i].visibleTextOffset = i * 20;
    entries[i].highlightEndOffset = i * 20 + 10;
  }
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Pending);
  EXPECT_EQ(uploadDates.size(), 32u);
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 40u);
  EXPECT_EQ(std::set<std::string>(uploadDates.begin(), uploadDates.end()).size(), 40u);
}

TEST_F(BookOrbitAnnotationsTest, CredentialsServerAndAccountAreIsolated) {
  credentialStub.credentials = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Skipped);
  credentialStub.credentials = true;
  credentialStub.url = "https://sync.koreader.rocks";
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Skipped);
  EXPECT_TRUE(uploadDates.empty());
  credentialStub.url = "https://books.test/api/v1/koreader";
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  credentialStub.user = "another-account";
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  credentialStub.url = "https://other.test/api/v1/koreader";
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 3u);
}

TEST_F(BookOrbitAnnotationsTest, SlowAcknowledgedPrefixStillAllowsNewUpload) {
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  entries.push_back(entries[0]);
  entries[1].visibleTextOffset = 20;
  entries[1].highlightEndOffset = 30;
  entryCostMs = 46000;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  EXPECT_EQ(uploadDates.size(), 2u);
}

TEST_F(BookOrbitAnnotationsTest, InvalidClockConversionAndSourceDoNotUpload) {
  halClock.valid = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  halClock.valid = true;
  rangeResolves = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  rangeResolves = true;
  readable = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  EXPECT_TRUE(uploadDates.empty());
}

TEST_F(BookOrbitAnnotationsTest, FailedMetadataSaveCannotSendUnstableIdentity) {
  Storage.failRename = true;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  EXPECT_TRUE(uploadDates.empty());
}

TEST_F(BookOrbitAnnotationsTest, CorruptAcknowledgementFailsClosedAndUnmatchedCanRetry) {
  accepted = false;
  unmatched = true;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Unmatched);
  unmatched = false;
  accepted = true;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  for (auto& [path, bytes] : Storage.files) {
    if (path.ends_with(".state")) bytes = {'b', 'a', 'd'};
  }
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Failed);
  EXPECT_EQ(uploadDates.size(), 2u);
}

TEST_F(BookOrbitAnnotationsTest, EmptyHighlightsDoNotNeedClockOrSendDeletion) {
  ASSERT_EQ(sync(), BookOrbitAnnotations::Result::Complete);
  entries.clear();
  halClock.valid = false;
  EXPECT_EQ(sync(), BookOrbitAnnotations::Result::Skipped);
  EXPECT_EQ(uploadDates.size(), 1u);
}
