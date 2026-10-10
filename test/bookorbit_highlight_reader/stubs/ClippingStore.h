#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

inline constexpr uint16_t CLIPPING_MAX_PER_BOOK = 1024;

struct Clipping {
  uint16_t spineIndex = 0;
  uint32_t startOffset = UINT32_MAX;
  uint32_t endOffset = UINT32_MAX;
  uint16_t textLength = 0;
  bool pendingUpload = true;
  uint64_t syncRevision = 0;
  char id[65] = {};
  char chapterTitle[48] = {};
};

struct StoredClipping {
  Clipping clipping;
  std::string text;
  bool failRead = false;
};

class ClippingStore {
 public:
  inline static std::map<std::string, std::vector<StoredClipping>> books;
  inline static bool failLoad = false;
  inline static size_t reads = 0;
  inline static size_t globalAccesses = 0;
  inline static size_t liveInstances = 0;
  inline static size_t nullIndex = SIZE_MAX;
  std::string loadedPath;

  ClippingStore() { ++liveInstances; }
  ~ClippingStore() { --liveInstances; }
  static ClippingStore& getInstance() {
    ++globalAccesses;
    static ClippingStore instance;
    return instance;
  }
  bool loadForBook(const std::string& path, const std::string&, const std::string&, const std::string& type) {
    loadedPath = path;
    return !failLoad && type == "epub";
  }
  size_t clippingCount() const {
    const auto found = books.find(loadedPath);
    return found == books.end() ? 0 : found->second.size();
  }
  const Clipping* clippingAt(size_t index) const {
    if (index == nullIndex || index >= clippingCount()) return nullptr;
    return &books.at(loadedPath)[index].clipping;
  }
  bool readClippingText(size_t index, std::string& out) const {
    ++reads;
    if (index >= clippingCount()) return false;
    const auto& entry = books.at(loadedPath)[index];
    if (entry.failRead) return false;
    out = entry.text;
    return true;
  }
};

#define CLIPPINGS ClippingStore::getInstance()
