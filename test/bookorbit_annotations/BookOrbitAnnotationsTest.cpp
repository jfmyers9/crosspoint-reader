#include <BookOrbitAnnotations.h>
#include <BookOrbitAnnotationsClient.h>
#include <BookOrbitHighlightReader.h>
#include <BookOrbitStatsClient.h>
#include <BookmarkEntry.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
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

namespace {
std::vector<BookmarkEntry> entries;
std::vector<std::string> uploadDates, updateDates, uploadNotes;
bool readable = true, accepted = true, unmatched = false, failAcknowledgement = false;
int64_t entryCostMs = 0;
constexpr char BOOK[] = "/Books/test.epub";
}  // namespace

bool BookOrbitHighlightReader::visit(const std::string&, bool (*visitor)(void*, const BookmarkEntry&), void* context) {
  if (!readable) return false;
  for (const auto& entry : entries) {
    stubMonotonicMs += entryCostMs;
    if (entry.isHighlight() && visitor && !visitor(context, entry)) return false;
  }
  return true;
}

const char* BookOrbitStatsClient::deviceId() { return "crosspoint-test"; }
bool BookOrbitAnnotationsClient::lastUploadWasUnmatched() { return unmatched; }
bool BookOrbitAnnotationsClient::upload(const char*, const char* created, const char* updated, const char*, const char*,
                                        const char*, const char* note) {
  uploadDates.emplace_back(created);
  updateDates.emplace_back(updated);
  uploadNotes.emplace_back(note);
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
