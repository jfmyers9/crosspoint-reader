#include "BookmarkFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include "BookmarkUtil.h"

namespace {
bool isBookmarkDocument(const JsonDocument& doc) {
  if (!doc["bookmarks"].is<JsonArrayConst>()) {
    return false;
  }
  for (JsonVariantConst entry : doc["bookmarks"].as<JsonArrayConst>()) {
    if (!entry.is<JsonObjectConst>()) {
      return false;
    }
  }
  return true;
}
}  // namespace

bool BookmarkFile::load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks) {
  bookmarks.clear();

  // Read/write go through PersistableStoreBase so the JSON parser and
  // serializer stay instantiated once, in PersistableStore.cpp.
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  JsonDocument doc;
  // A failed promotion or interrupted save can leave the previous file in .bak.
  if (!Storage.exists(path.c_str())) {
    const std::string backupPath = path + ".bak";
    if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), path.c_str())) {
      LOG_ERR("BKM", "Failed to restore bookmark backup");
      return false;
    }
  }
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) {
    return false;
  }
  if (!isBookmarkDocument(doc)) {
    LOG_ERR("BKM", "Invalid bookmark document");
    return false;
  }

  JsonArray arr = doc["bookmarks"].as<JsonArray>();
  bookmarks.reserve(arr.size());
  for (JsonObject obj : arr) {
    bookmarks.emplace_back();
    auto& bookmark = bookmarks.back();
    bookmark.xpath = obj["xpath"] | "";
    bookmark.percentage = obj["percentage"] | static_cast<float>(0);
    bookmark.summary = obj["summary"] | "";
    bookmark.name = obj["name"] | "";
    if (bookmark.name.size() > BookmarkEntry::MAX_NAME_LENGTH) {
      bookmark.name.resize(BookmarkEntry::MAX_NAME_LENGTH);
    }
    bookmark.computedSpineIndex = obj["si"] | static_cast<uint16_t>(0);
    bookmark.computedChapterPageCount = obj["pc"] | static_cast<uint16_t>(0);
    bookmark.computedChapterProgress = obj["pp"] | static_cast<uint16_t>(0);
    if (!obj["vo"].isNull()) {
      bookmark.visibleTextOffset = obj["vo"] | static_cast<uint32_t>(0);
      bookmark.hasVisibleTextOffset = true;
    }
    if (obj["vo"].is<uint32_t>() && obj["he"].is<uint32_t>()) {
      bookmark.highlightEndOffset = obj["he"].as<uint32_t>();
      if (!bookmark.isHighlight()) {
        bookmark.highlightEndOffset = 0;
      }
    }
  }

  LOG_DBG("BKM", "Loaded %zu bookmarks from file", bookmarks.size());
  return true;
}

bool BookmarkFile::save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks) {
  for (const auto& bookmark : bookmarks) {
    if (bookmark.name.size() > BookmarkEntry::MAX_NAME_LENGTH) {
      LOG_ERR("BKM", "Bookmark name exceeds %zu bytes", BookmarkEntry::MAX_NAME_LENGTH);
      return false;
    }
  }

  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  const std::string backupPath = path + ".bak";
  JsonDocument doc;
  // Reuse the document allocation: an unreadable source must not be replaced by an empty cache.
  const char* existingPath = Storage.exists(path.c_str()) ? path.c_str() : backupPath.c_str();
  if (Storage.exists(existingPath) &&
      (!PersistableStoreBase::readDocFromFile(existingPath, doc) || !isBookmarkDocument(doc))) {
    LOG_ERR("BKM", "Refusing to overwrite unreadable bookmarks");
    return false;
  }
  doc.clear();
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  LOG_DBG("BKM", "Saving %zu bookmarks to file", bookmarks.size());
  for (const auto& bookmark : bookmarks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["xpath"] = bookmark.xpath;
    obj["percentage"] = bookmark.percentage;
    obj["summary"] = bookmark.summary;
    if (!bookmark.name.empty()) {
      obj["name"] = bookmark.name;
    }
    obj["si"] = bookmark.computedSpineIndex;
    obj["pc"] = bookmark.computedChapterPageCount;
    obj["pp"] = bookmark.computedChapterProgress;
    if (bookmark.hasVisibleTextOffset) {
      obj["vo"] = bookmark.visibleTextOffset;
    }
    if (bookmark.isHighlight()) {
      obj["he"] = bookmark.highlightEndOffset;
    }
  }

  if (doc.overflowed()) {
    LOG_ERR("BKM", "Insufficient memory to serialize bookmarks");
    return false;
  }

  // writeDocToFile ensures /.crosspoint; the bookmarks subdirectory is ours.
  Storage.mkdir(BookmarkUtil::getBookmarksDir().c_str());
  // These two short path allocations keep the previous JSON intact until staging succeeds.
  const std::string tempPath = path + ".tmp";
  if (!PersistableStoreBase::writeDocToFile(tempPath.c_str(), doc)) {
    return false;
  }
  {
    HalFile staged;
    if (!Storage.openFileForRead("BKM", tempPath.c_str(), staged) || staged.size() != measureJson(doc)) {
      LOG_ERR("BKM", "Incomplete staged bookmark file");
      return false;
    }
  }

  if (Storage.exists(path.c_str())) {
    if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) {
      LOG_ERR("BKM", "Failed to remove stale bookmark backup");
      return false;
    }
    if (!Storage.rename(path.c_str(), backupPath.c_str())) {
      LOG_ERR("BKM", "Failed to back up bookmarks");
      return false;
    }
  }
  if (!Storage.rename(tempPath.c_str(), path.c_str())) {
    LOG_ERR("BKM", "Failed to promote staged bookmarks");
    if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), path.c_str())) {
      LOG_ERR("BKM", "Bookmark backup retained for recovery");
    }
    return false;
  }
  // Failure to remove the backup does not invalidate the successfully saved primary.
  if (Storage.exists(backupPath.c_str())) {
    Storage.remove(backupPath.c_str());
  }
  return true;
}
