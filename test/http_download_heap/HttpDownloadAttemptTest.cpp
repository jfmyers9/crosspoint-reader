#include <Arduino.h>
#include <HalStorage.h>
#include <ResumableFetch.h>
#include <gtest/gtest.h>

#include "HttpDownloader.h"

namespace {
class HttpDownloadAttemptTest : public testing::Test {
 protected:
  void SetUp() override {
    Storage = {};
    Storage.files["/book.epub"] = "previous";
    ESP = {};
    freeink::attempts = {{true, 16000, 8000}};
    freeink::connections = 0;
  }
  auto download(const bool* cancel = nullptr) {
    return HttpDownloader::downloadToFile("http://books.example/book.epub", "/book.epub", nullptr, cancel);
  }
};
TEST_F(HttpDownloadAttemptTest, LowHeapDirectHttpStillDownloads) {
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(Storage.files["/book.epub"], "book");
  EXPECT_EQ(freeink::connections, 1);
}
TEST_F(HttpDownloadAttemptTest, BlocksCrossOriginBeforeConnectAndPreservesExistingFile) {
  freeink::attempts.push_back({false, 16000, 8000});
  EXPECT_EQ(download(), HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(freeink::connections, 1);
  EXPECT_EQ(Storage.files["/book.epub"], "previous");
  EXPECT_EQ(Storage.files.count("/book.epub.part"), 0u);
}
TEST_F(HttpDownloadAttemptTest, AllowsCrossOriginWithBothHeapFloors) {
  freeink::attempts.push_back({false, HttpDownloader::MIN_TLS_FREE_HEAP, HttpDownloader::MIN_TLS_MAX_ALLOC});
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(freeink::connections, 2);
}
TEST_F(HttpDownloadAttemptTest, SameOriginHttpRetryNeedsNoTlsHeap) {
  freeink::attempts.push_back({true, 16000, 8000});
  EXPECT_EQ(download(), HttpDownloader::OK);
  EXPECT_EQ(freeink::connections, 2);
}
TEST_F(HttpDownloadAttemptTest, HttpsRetryRechecksHeadroom) {
  freeink::attempts = {{true, 50000, 25000}, {true, 16000, 8000}};
  EXPECT_EQ(HttpDownloader::downloadToFile("https://books.example/book.epub", "/book.epub"),
            HttpDownloader::HTTP_ERROR);
  EXPECT_EQ(freeink::connections, 1);
  EXPECT_EQ(Storage.files["/book.epub"], "previous");
}
TEST_F(HttpDownloadAttemptTest, CancellationRemainsDistinctFromHeapFailure) {
  const bool cancel = true;
  EXPECT_EQ(download(&cancel), HttpDownloader::ABORTED);
  EXPECT_EQ(freeink::connections, 0);
}
TEST_F(HttpDownloadAttemptTest, CatalogFetchRetainsExistingPolicy) {
  freeink::attempts.push_back({false, 16000, 8000});
  EXPECT_TRUE(HttpDownloader::fetchUrl("http://books.example/feed", [](const uint8_t*, size_t) { return true; }));
  EXPECT_EQ(freeink::connections, 2);
}
}  // namespace
