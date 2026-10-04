#include <HalStorage.h>
#include <gtest/gtest.h>

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
  }
  void set(const std::string& contents) { Storage.files[BookmarkUtil::getBookmarkPath(BOOK)] = {contents}; }
  bool validate() { return BookOrbitHighlightReader::visit(BOOK, nullptr, nullptr); }
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
}  // namespace
