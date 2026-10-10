#include <gtest/gtest.h>

#include "HttpDownloader.h"

namespace {
constexpr uint32_t FREE_FLOOR = HttpDownloader::MIN_TLS_FREE_HEAP;
constexpr uint32_t BLOCK_FLOOR = HttpDownloader::MIN_TLS_MAX_ALLOC;

TEST(HttpDownloadHeap, TailnetHttpDoesNotRequireTlsBuffers) {
  for (const auto* url : {"http://books.tail123456.ts.net/api/v1/opds/42/download?fileId=7",
                          "http://100.64.1.2:3000/api/v1/opds/42/download?fileId=7"}) {
    EXPECT_TRUE(HttpDownloader::hasDownloadHeapHeadroom(url, FREE_FLOOR - 1, BLOCK_FLOOR - 1));
    EXPECT_TRUE(HttpDownloader::hasDownloadHeapHeadroom(url, 16000, 8000));
  }
}

TEST(HttpDownloadHeap, LanHttpDoesNotRequireTlsBuffersEither) {
  EXPECT_TRUE(HttpDownloader::hasDownloadHeapHeadroom("http://192.168.1.2/book.epub", 16000, 8000));
}

TEST(HttpDownloadHeap, HttpsStillRequiresBothFreeHeapAndContiguousBlock) {
  constexpr auto* url = "https://books.example/book.epub";
  EXPECT_FALSE(HttpDownloader::hasDownloadHeapHeadroom(url, FREE_FLOOR - 1, BLOCK_FLOOR));
  EXPECT_FALSE(HttpDownloader::hasDownloadHeapHeadroom(url, FREE_FLOOR, BLOCK_FLOOR - 1));
  EXPECT_FALSE(HttpDownloader::hasDownloadHeapHeadroom(url, FREE_FLOOR - 1, BLOCK_FLOOR - 1));
  EXPECT_TRUE(HttpDownloader::hasDownloadHeapHeadroom(url, FREE_FLOOR, BLOCK_FLOOR));
  EXPECT_TRUE(HttpDownloader::hasDownloadHeapHeadroom(url, FREE_FLOOR * 2, BLOCK_FLOOR * 2));
}

TEST(HttpDownloadHeap, OnlyAnExplicitHttpSchemeBypassesTlsFloor) {
  for (const auto* url :
       {"", "http", "http:", "http:/book.epub", "books.example/book.epub", "https://books.example/http://book.epub"}) {
    EXPECT_FALSE(HttpDownloader::hasDownloadHeapHeadroom(url, 16000, 8000));
  }
}
}  // namespace
