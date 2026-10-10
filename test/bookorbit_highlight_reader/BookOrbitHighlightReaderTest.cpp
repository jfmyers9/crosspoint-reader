#include <ClippingStore.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstring>

#include "BookOrbitHighlightReader.h"
#include "util/BookmarkUtil.h"

namespace {
constexpr char BOOK[] = "/books/test.epub";
constexpr char HIGHLIGHT[] = R"({"si":2,"vo":10,"he":15,"summary":"hello","name":"label"})";
std::string wrap(const std::string& value) { return "{\"bookmarks\":[" + value + "]}"; }
class BookOrbitHighlightReaderTest : public testing::Test {
 protected:
  void SetUp() override {
    Storage = {};
    bytesRead = 0;
    ClippingStore::books.clear();
    ClippingStore::failLoad = false;
    ClippingStore::reads = 0;
    ClippingStore::globalAccesses = 0;
    ClippingStore::nullIndex = SIZE_MAX;
  }
  void set(const std::string& contents) { Storage.files[BookmarkUtil::getBookmarkPath(BOOK)] = {contents}; }
  bool validate() { return BookOrbitHighlightReader::visit(BOOK, nullptr, nullptr); }
  static StoredClipping clip(uint16_t spine = 2, uint32_t start = 10, uint32_t end = 15,
                             const std::string& text = "hello") {
    StoredClipping result{};
    result.clipping.spineIndex = spine;
    result.clipping.startOffset = start;
    result.clipping.endOffset = end;
    result.clipping.textLength = static_cast<uint16_t>(text.size());
    result.text = text;
    return result;
  }
  static bool collect(void* context, const BookmarkEntry& entry) {
    static_cast<std::vector<BookmarkEntry>*>(context)->push_back(entry);
    return true;
  }
  bool visit(std::vector<BookmarkEntry>& entries) {
    entries.clear();
    entries.reserve(CLIPPING_MAX_PER_BOOK + 1);
    return BookOrbitHighlightReader::visit(BOOK, collect, &entries);
  }
};
bool count(void* context, const BookmarkEntry& entry) {
  ++*static_cast<unsigned*>(context);
  EXPECT_TRUE(entry.isHighlight());
  EXPECT_EQ(entry.computedSpineIndex, 2);
  EXPECT_EQ(entry.visibleTextOffset, 10u);
  EXPECT_EQ(entry.highlightEndOffset, 15u);
  EXPECT_EQ(entry.summary, "hello");
  EXPECT_EQ(entry.name, "label");
  return true;
}
TEST_F(BookOrbitHighlightReaderTest, MissingAndEmptyCollectionAreSuccessful) {
  EXPECT_TRUE(validate());
  set(" { \"bookmarks\" : [ ] } \r\n");
  EXPECT_TRUE(validate());
}
TEST_F(BookOrbitHighlightReaderTest, VisitsOnlyHighlights) {
  set(wrap(std::string("{\"summary\":\"bookmark\"},") + HIGHLIGHT + ",{\"he\":0}"));
  unsigned seen = 0;
  EXPECT_TRUE(BookOrbitHighlightReader::visit(BOOK, count, &seen));
  EXPECT_EQ(seen, 1u);
  EXPECT_TRUE(validate());
}
TEST_F(BookOrbitHighlightReaderTest, StreamsLargeCollection) {
  std::string contents = "{\"bookmarks\":[";
  for (unsigned i = 0; i < 10000; ++i) {
    if (i) contents += ',';
    contents += HIGHLIGHT;
  }
  contents += "]}";
  set(contents);
  unsigned seen = 0;
  EXPECT_TRUE(BookOrbitHighlightReader::visit(BOOK, count, &seen));
  EXPECT_EQ(seen, 10000u);
}
TEST_F(BookOrbitHighlightReaderTest, BackupOnlyWhenPrimaryMissingAndNeverModified) {
  const auto path = BookmarkUtil::getBookmarkPath(BOOK);
  Storage.files[path + ".bak"] = {wrap(HIGHLIGHT)};
  EXPECT_TRUE(validate());
  EXPECT_FALSE(Storage.exists(path.c_str()));
  EXPECT_EQ(Storage.files.at(path + ".bak").content, wrap(HIGHLIGHT));
  set("bad");
  EXPECT_FALSE(validate());
}
TEST_F(BookOrbitHighlightReaderTest, CallbackCanAbortWithoutReadingRest) {
  set(wrap(std::string(HIGHLIGHT) + "," + HIGHLIGHT));
  unsigned seen = 0;
  EXPECT_FALSE(BookOrbitHighlightReader::visit(
      BOOK,
      [](void* p, const BookmarkEntry&) {
        ++*static_cast<unsigned*>(p);
        return false;
      },
      &seen));
  EXPECT_EQ(seen, 1u);
  EXPECT_LT(bytesRead, Storage.files.begin()->second.content.size());
}
TEST_F(BookOrbitHighlightReaderTest, RejectsInvalidEnvelopeAndTruncation) {
  for (const auto& bad :
       {std::string(), std::string("{}"), std::string("[]"), std::string("{\"bookmarks\":{}}"), wrap("null"),
        wrap("[]"), wrap("{},"), wrap("{} {}"), wrap(HIGHLIGHT) + "junk", wrap(HIGHLIGHT) + std::string(1, '\0'),
        std::string("{\"bookmarks\":[") + HIGHLIGHT, std::string("{\"bookmarks\":[],\"extra\":1}")}) {
    set(bad);
    EXPECT_FALSE(validate()) << bad;
  }
}
TEST_F(BookOrbitHighlightReaderTest, RejectsInvalidHighlightFields) {
  for (const auto* bad :
       {R"({"si":2,"vo":10,"he":10,"summary":"x"})", R"({"si":2,"vo":10,"he":9,"summary":"x"})",
        R"({"si":2,"he":15,"summary":"x"})", R"({"si":65536,"vo":10,"he":15,"summary":"x"})",
        R"({"si":-1,"vo":10,"he":15,"summary":"x"})", R"({"si":2,"vo":1.5,"he":15,"summary":"x"})",
        R"({"si":2,"vo":10,"he":4294967296,"summary":"x"})", R"({"si":2,"vo":10,"he":"15","summary":"x"})",
        R"({"si":2,"vo":10,"he":15,"summary":""})", R"({"si":2,"vo":10,"he":15,"summary":3})",
        R"({"si":2,"vo":10,"he":15,"summary":"x","name":3})", R"({"si":2,"vo":10,"he":15,"summary":"x\u0000y"})"}) {
    set(wrap(bad));
    EXPECT_FALSE(validate()) << bad;
  }
}
TEST_F(BookOrbitHighlightReaderTest, RejectsPermissiveJsonExtensionsAndDuplicateFields) {
  for (const auto* bad : {"{unquoted:1}", "{'single':'quoted'}", "{\"x\":01}", "{\"x\":1.}", "{\"x\":+1}",
                          "{\"x\":true,}", "{\"x\":false,\"x\":null}", "{\"x\":{}}", "{\"x\":[]}", "{\"he\":null}"}) {
    set(wrap(bad));
    EXPECT_FALSE(validate()) << bad;
  }
}
TEST_F(BookOrbitHighlightReaderTest, EnforcesStringAndEntryLimits) {
  set(wrap("{\"si\":2,\"vo\":10,\"he\":15,\"summary\":\"" + std::string(2049, 'x') + "\"}"));
  EXPECT_FALSE(validate());
  set(wrap("{\"si\":2,\"vo\":10,\"he\":15,\"summary\":\"x\",\"name\":\"" + std::string(129, 'x') + "\"}"));
  EXPECT_FALSE(validate());
  set(wrap("{\"unknown\":\"" + std::string(1000000, 'x') + "\"}"));
  bytesRead = 0;
  EXPECT_FALSE(validate());
  EXPECT_LT(bytesRead, 16500u);
  set(wrap("{" + std::string(20000, ' ') + "}"));
  EXPECT_FALSE(validate());
}
TEST_F(BookOrbitHighlightReaderTest, EnforcesDocumentAllocationLimitForManyFields) {
  std::string object = "{";
  for (unsigned i = 0; i < 1500; ++i) {
    if (i) object += ',';
    object += "\"" + std::to_string(i) + "\":0";
  }
  object += '}';
  ASSERT_LT(object.size(), 16384u);
  set(wrap(object));
  EXPECT_FALSE(validate());
}
TEST_F(BookOrbitHighlightReaderTest, RejectsIoFailureDirectoryAndOversizedFile) {
  set(wrap(HIGHLIGHT));
  Storage.failOpen = true;
  EXPECT_FALSE(validate());
  Storage.failOpen = false;
  auto& file = Storage.files.begin()->second;
  file.directory = true;
  EXPECT_FALSE(validate());
  file.directory = false;
  file.reportedSize = file.content.size() + 1;
  EXPECT_FALSE(validate());
  file.reportedSize = 16 * 1024 * 1024 + 1;
  bytesRead = 0;
  EXPECT_FALSE(validate());
  EXPECT_EQ(bytesRead, 0u);
}

TEST_F(BookOrbitHighlightReaderTest, MapsOnlyCurrentBooksPersistedClippingsWithoutNotes) {
  auto item = clip(7, 100, 105, "world");
  std::strcpy(item.clipping.chapterTitle, "Chapter title is not a note");
  item.clipping.pendingUpload = false;
  item.clipping.syncRevision = 42;
  std::strcpy(item.clipping.id, "upstream-id");
  ClippingStore::books[BOOK] = {item};
  ClippingStore::books["/other.epub"] = {clip()};
  EXPECT_TRUE(validate());
  EXPECT_EQ(ClippingStore::reads, 1u);
  std::vector<BookmarkEntry> entries;
  ASSERT_TRUE(visit(entries));
  ASSERT_EQ(entries.size(), 1u);
  const auto& entry = entries.front();
  EXPECT_TRUE(entry.isHighlight());
  EXPECT_EQ(entry.computedSpineIndex, 7);
  EXPECT_EQ(entry.visibleTextOffset, 100u);
  EXPECT_EQ(entry.highlightEndOffset, 105u);
  EXPECT_EQ(entry.summary, "world");
  EXPECT_TRUE(entry.name.empty());
  EXPECT_EQ(ClippingStore::reads, 2u);
  EXPECT_FALSE(ClippingStore::books[BOOK][0].clipping.pendingUpload);
  EXPECT_EQ(ClippingStore::books[BOOK][0].clipping.syncRevision, 42u);
  EXPECT_STREQ(ClippingStore::books[BOOK][0].clipping.id, "upstream-id");
}

TEST_F(BookOrbitHighlightReaderTest, LegacyAnchorWinsAndPreservesItsNote) {
  set(wrap(HIGHLIGHT));
  ClippingStore::books[BOOK] = {clip(2, 10, 15, "alternate quote"), clip()};
  EXPECT_TRUE(validate());
  EXPECT_EQ(ClippingStore::reads, 2u);
  unsigned seen = 0;
  EXPECT_TRUE(BookOrbitHighlightReader::visit(BOOK, count, &seen));
  EXPECT_EQ(seen, 1u);
  EXPECT_EQ(ClippingStore::reads, 4u);
}

TEST_F(BookOrbitHighlightReaderTest, DeduplicatesExactBinaryAnchorsNotQuoteText) {
  ClippingStore::books[BOOK] = {clip(), clip(2, 10, 15, "duplicate"), clip(2, 20, 25), clip(3, 10, 15),
                                clip(2, 10, 16)};
  EXPECT_TRUE(validate());
  EXPECT_EQ(ClippingStore::reads, 5u);
  std::vector<BookmarkEntry> entries;
  ASSERT_TRUE(visit(entries));
  ASSERT_EQ(entries.size(), 4u);
  EXPECT_EQ(entries[0].summary, "hello");
  EXPECT_EQ(entries[1].visibleTextOffset, 20u);
  EXPECT_EQ(entries[2].computedSpineIndex, 3);
  EXPECT_EQ(entries[3].highlightEndOffset, 16u);
  EXPECT_EQ(ClippingStore::reads, 10u);
}

TEST_F(BookOrbitHighlightReaderTest, RejectsUnknownInvalidAndOversizedClippingsOnBothPasses) {
  for (const auto& item : {clip(2, UINT32_MAX, UINT32_MAX), clip(2, 10, UINT32_MAX), clip(2, UINT32_MAX, 15),
                           clip(2, 10, 10), clip(2, 15, 10), clip(2, 10, 15, ""),
                           clip(2, 10, 15, std::string(2049, 'x')), clip(2, 10, 15, std::string("a\0b", 3))}) {
    ClippingStore::books[BOOK] = {item};
    EXPECT_FALSE(validate());
    std::vector<BookmarkEntry> entries;
    EXPECT_FALSE(visit(entries));
    EXPECT_TRUE(entries.empty());
  }
  ClippingStore::books[BOOK] = {clip(2, 0, 2048, std::string(2048, 'x'))};
  EXPECT_TRUE(validate());
  std::vector<BookmarkEntry> entries;
  ASSERT_TRUE(visit(entries));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].summary.size(), 2048u);
}

TEST_F(BookOrbitHighlightReaderTest, RejectsLoadReadAndLengthFailuresOnBothPasses) {
  ClippingStore::books[BOOK] = {clip()};
  std::vector<BookmarkEntry> entries;
  ClippingStore::failLoad = true;
  EXPECT_FALSE(validate());
  EXPECT_FALSE(visit(entries));
  ClippingStore::failLoad = false;
  auto& item = ClippingStore::books[BOOK][0];
  item.failRead = true;
  EXPECT_FALSE(validate());
  EXPECT_FALSE(visit(entries));
  item.failRead = false;
  ++item.clipping.textLength;
  EXPECT_FALSE(validate());
  EXPECT_FALSE(visit(entries));
  EXPECT_TRUE(entries.empty());
}

TEST_F(BookOrbitHighlightReaderTest, SuppressedDuplicatesStillRequireReadableBoundedQuotes) {
  for (const bool legacy : {false, true}) {
    if (legacy) set(wrap(HIGHLIGHT));
    ClippingStore::books[BOOK] = {clip(), clip()};
    auto& duplicate = ClippingStore::books[BOOK][1];
    duplicate.failRead = true;
    EXPECT_FALSE(validate());
    std::vector<BookmarkEntry> entries;
    EXPECT_FALSE(visit(entries));
    duplicate.failRead = false;
    duplicate = clip(2, 10, 15, std::string(2049, 'x'));
    EXPECT_FALSE(validate());
    EXPECT_FALSE(visit(entries));
  }
}

TEST_F(BookOrbitHighlightReaderTest, LocalStoreIsDestroyedWithoutTouchingGlobalReaderState) {
  auto& reader = CLIPPINGS;
  ASSERT_TRUE(reader.loadForBook("/reader.epub", "", "", "epub"));
  ClippingStore::books["/reader.epub"] = {clip(9, 1, 2)};
  ClippingStore::books[BOOK] = {clip()};
  const auto accesses = ClippingStore::globalAccesses;
  const auto instances = ClippingStore::liveInstances;
  EXPECT_TRUE(validate());
  EXPECT_EQ(ClippingStore::liveInstances, instances);
  std::vector<BookmarkEntry> entries;
  EXPECT_TRUE(visit(entries));
  EXPECT_EQ(ClippingStore::liveInstances, instances);
  ClippingStore::failLoad = true;
  EXPECT_FALSE(validate());
  EXPECT_EQ(ClippingStore::liveInstances, instances);
  EXPECT_EQ(ClippingStore::globalAccesses, accesses);
  EXPECT_EQ(reader.loadedPath, "/reader.epub");
  ASSERT_EQ(reader.clippingCount(), 1u);
  EXPECT_EQ(reader.clippingAt(0)->spineIndex, 9);
}

TEST_F(BookOrbitHighlightReaderTest, BinaryCallbackCanAbortAndReleasesLocalStore) {
  ClippingStore::books[BOOK] = {clip(), clip(2, 20, 25)};
  const auto instances = ClippingStore::liveInstances;
  unsigned seen = 0;
  EXPECT_FALSE(BookOrbitHighlightReader::visit(
      BOOK,
      [](void* context, const BookmarkEntry&) {
        ++*static_cast<unsigned*>(context);
        return false;
      },
      &seen));
  EXPECT_EQ(seen, 1u);
  EXPECT_EQ(ClippingStore::reads, 1u);
  EXPECT_EQ(ClippingStore::liveInstances, instances);
}

TEST_F(BookOrbitHighlightReaderTest, FixedDedupMapCoversLastSlotAndRejectsExcessRecords) {
  ClippingStore::books[BOOK].assign(CLIPPING_MAX_PER_BOOK, clip());
  EXPECT_TRUE(validate());
  std::vector<BookmarkEntry> entries;
  EXPECT_TRUE(visit(entries));
  EXPECT_EQ(entries.size(), 1u);
  EXPECT_EQ(ClippingStore::reads, 2u * CLIPPING_MAX_PER_BOOK);
  ClippingStore::books[BOOK].push_back(clip());
  EXPECT_FALSE(validate());
  EXPECT_FALSE(visit(entries));
}

TEST_F(BookOrbitHighlightReaderTest, RejectsMissingIndexRecords) {
  ClippingStore::books[BOOK] = {clip(), clip(2, 20, 25)};
  ClippingStore::nullIndex = 1;
  EXPECT_FALSE(validate());
  std::vector<BookmarkEntry> entries;
  EXPECT_FALSE(visit(entries));
  set(wrap(HIGHLIGHT));
  EXPECT_FALSE(validate());
  EXPECT_FALSE(visit(entries));
}
}  // namespace
