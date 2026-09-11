#include "LibraryBookDetails.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t CACHE_MAGIC = 0x3144424c;  // LBD1, little-endian targets.
constexpr size_t MAX_TEXT_BYTES = 512;
constexpr size_t MAX_PATH_BYTES = 1024;

struct CacheHeader {
  uint64_t sourceSize;
  uint32_t magic;
  uint16_t date;
  uint16_t time;
  uint16_t pathLength;
  uint16_t titleLength;
  uint16_t authorLength;
  uint8_t hasCover;
  uint8_t reserved;
};
static_assert(sizeof(CacheHeader) == 24);

void trimUtf8(std::string& text) {
  if (text.empty()) return;
  size_t start = text.size() - 1;
  while (start > 0 && (static_cast<uint8_t>(text[start]) & 0xc0) == 0x80) --start;
  const uint8_t lead = text[start];
  const size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
  if (text.size() - start < length) text.resize(start);
}

bool readText(HalFile& file, std::string& text, size_t length) {
  // Only three bounded strings are retained for the requested book, never the whole folder.
  text.resize(length);
  return length == 0 || file.read(&text[0], length) == static_cast<int>(length);
}

bool validThumbnail(const std::string& path, int height) {
  HalFile file;
  if (!Storage.exists(path.c_str()) || !Storage.openFileForRead("LIB", path, file)) return false;
  uint8_t header[54];
  if (file.read(header, sizeof(header)) != sizeof(header) || header[0] != 'B' || header[1] != 'M') return false;
  uint32_t offset, dibSize, width, compression, colors;
  int32_t signedRows;
  uint16_t planes, bits;
  memcpy(&offset, header + 10, sizeof(offset));
  memcpy(&dibSize, header + 14, sizeof(dibSize));
  memcpy(&width, header + 18, sizeof(width));
  memcpy(&signedRows, header + 22, sizeof(signedRows));
  memcpy(&planes, header + 26, sizeof(planes));
  memcpy(&bits, header + 28, sizeof(bits));
  memcpy(&compression, header + 30, sizeof(compression));
  memcpy(&colors, header + 46, sizeof(colors));
  if (dibSize != 40 || planes != 1 || bits != 1 || compression != 0 || (colors != 0 && colors != 2) || width == 0 ||
      width > static_cast<uint32_t>(height) || signedRows == 0 || signedRows < -height || signedRows > height ||
      offset != 62)
    return false;
  const uint32_t rows = signedRows < 0 ? -signedRows : signedRows;
  const uint64_t required = static_cast<uint64_t>(offset) + ((width + 31) / 32) * 4 * rows;
  return required <= file.fileSize64();
}

bool readCache(const std::string& cachePath, const std::string& sourcePath, const CacheHeader& source,
               const std::string& thumbPath, int height, LibraryBookDetails& out) {
  HalFile file;
  if (!Storage.exists(cachePath.c_str()) || !Storage.openFileForRead("LIB", cachePath, file)) return false;
  CacheHeader header{};
  if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != CACHE_MAGIC ||
      header.sourceSize != source.sourceSize || header.date != source.date || header.time != source.time ||
      header.pathLength != sourcePath.size() || header.titleLength > MAX_TEXT_BYTES ||
      header.authorLength > MAX_TEXT_BYTES || header.hasCover > 1 || header.reserved != 0 ||
      file.fileSize64() != sizeof(header) + header.pathLength + header.titleLength + header.authorLength)
    return false;
  // Compare the path without a second allocation, including in the unlikely event of a hash collision.
  for (char c : sourcePath) {
    if (file.read() != static_cast<uint8_t>(c)) return false;
  }
  if (!readText(file, out.title, header.titleLength) || !readText(file, out.author, header.authorLength)) return false;
  if (header.hasCover) {
    if (!validThumbnail(thumbPath, height)) return false;
    out.coverPath = thumbPath;
  }
  return true;
}

void writeCache(const std::string& cachePath, const std::string& sourcePath, CacheHeader header,
                const LibraryBookDetails& details) {
  header.magic = CACHE_MAGIC;
  header.pathLength = sourcePath.size();
  header.titleLength = details.title.size();
  header.authorLength = details.author.size();
  header.hasCover = !details.coverPath.empty();
  HalFile file;
  if (!Storage.openFileForWrite("LIB", cachePath, file)) return;
  if (file.write(&header, sizeof(header)) != sizeof(header) ||
      file.write(sourcePath.data(), sourcePath.size()) != sourcePath.size() ||
      file.write(details.title.data(), details.title.size()) != details.title.size() ||
      file.write(details.author.data(), details.author.size()) != details.author.size()) {
    LOG_ERR("LIB", "Could not persist library details");
    file.close();
    Storage.remove(cachePath.c_str());
  }
}
}  // namespace

bool loadLibraryBookDetails(const std::string& path, int coverHeight, LibraryBookDetails& out) {
  out = {};
  if (!FsHelpers::hasEpubExtension(path)) return true;
  if (path.size() > MAX_PATH_BYTES || coverHeight <= 0 || coverHeight > 512) {
    LOG_ERR("LIB", "Library preview path or dimensions exceed limits");
    return false;
  }
  CacheHeader source{};
  {
    HalFile file;
    if (!Storage.openFileForRead("LIB", path, file)) return false;
    source.sourceSize = file.fileSize64();
    if (!file.getModifyDateTime(&source.date, &source.time)) {
      LOG_ERR("LIB", "Could not read book modification time");
      return false;
    }
  }
  // Keep the Epub object off the worker stack; no reader cache or CSS parser is allocated.
  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint/library");
  if (!epub) {
    LOG_ERR("LIB", "OOM allocating library EPUB");
    return false;
  }
  const auto thumbPath = epub->getThumbBmpPath(coverHeight);
  const auto cachePath = epub->getCachePath() + "/details_" + std::to_string(coverHeight) + ".bin";
  if (readCache(cachePath, path, source, thumbPath, coverHeight, out)) return true;
  out = {};
  // Own this thumbnail separately from the reader's cover cache so replacement cannot reuse stale artwork.
  if (Storage.exists(thumbPath.c_str()) && !Storage.remove(thumbPath.c_str())) {
    LOG_ERR("LIB", "Could not invalidate library thumbnail");
    return false;
  }
  BookMetadataCache::BookMetadata metadata;
  if (!epub->readLibraryMetadata(metadata)) return false;
  out.title = std::move(metadata.title);
  out.author = std::move(metadata.author);
  out.title.resize(std::min(out.title.size(), MAX_TEXT_BYTES));
  out.author.resize(std::min(out.author.size(), MAX_TEXT_BYTES));
  trimUtf8(out.title);
  trimUtf8(out.author);
  if (!Storage.ensureDirectoryExists(epub->getCachePath().c_str())) return true;
  const bool supportedCover =
      FsHelpers::hasJpgExtension(metadata.coverItemHref) || FsHelpers::hasPngExtension(metadata.coverItemHref);
  if (supportedCover) {
    // Decoder/SD/OOM failures remain retryable; only a verified absent/unsupported cover is cached as absent.
    if (!epub->generateThumbBmp(coverHeight, metadata.coverItemHref) || !validThumbnail(thumbPath, coverHeight))
      return true;
    out.coverPath = thumbPath;
  }
  writeCache(cachePath, path, source, out);
  return true;
}
