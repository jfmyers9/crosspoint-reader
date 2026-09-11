#pragma once
#include <HalStorage.h>

#include <string>

struct BookMetadataCache {
  struct BookMetadata {
    std::string title, author, language, coverItemHref, textReferenceHref;
  };
};
inline int metadataReads = 0;
inline int thumbnailAttempts = 0;
inline bool decodeSucceeds = true;
inline std::string testCover = "cover.jpg";
inline std::string testTitle = "Book title";

class Epub {
  std::string cachePath;

 public:
  Epub(const std::string& path, const std::string& cacheDir)
      : cachePath(cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(path))) {}
  const std::string& getCachePath() const { return cachePath; }
  std::string getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }
  bool readLibraryMetadata(BookMetadataCache::BookMetadata& out) {
    ++metadataReads;
    out.title = testTitle;
    out.author = "Author";
    out.coverItemHref = testCover;
    return true;
  }
  bool generateThumbBmp(int height, const std::string&) {
    ++thumbnailAttempts;
    if (!decodeSucceeds) return false;
    auto& bytes = testFiles[getThumbBmpPath(height)].bytes;
    const uint32_t offset = 62, width = height * 3 / 5, dibSize = 40;
    const int32_t rows = -height;
    const uint16_t bits = 1;
    bytes.resize(offset + ((width + 31) / 32) * 4 * height);
    bytes[0] = 'B';
    bytes[1] = 'M';
    memcpy(bytes.data() + 10, &offset, 4);
    memcpy(bytes.data() + 14, &dibSize, 4);
    memcpy(bytes.data() + 18, &width, 4);
    memcpy(bytes.data() + 22, &rows, 4);
    memcpy(bytes.data() + 26, &bits, 2);
    memcpy(bytes.data() + 28, &bits, 2);
    return true;
  }
};
