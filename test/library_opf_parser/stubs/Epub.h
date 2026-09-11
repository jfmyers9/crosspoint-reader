#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// These collaborators must remain unused by metadata-only parsing.
inline int cacheAccesses = 0;

class HalFile {
 public:
  explicit operator bool() const { return false; }
  void close() { ++cacheAccesses; }
  size_t position() const {
    ++cacheAccesses;
    return 0;
  }
  void seek(size_t) { ++cacheAccesses; }
  int available() const {
    ++cacheAccesses;
    return 0;
  }
};

class TestStorage {
 public:
  bool exists(const char*) {
    ++cacheAccesses;
    return false;
  }
  bool remove(const char*) {
    ++cacheAccesses;
    return false;
  }
  bool openFileForRead(const char*, const std::string&, HalFile&) {
    ++cacheAccesses;
    return false;
  }
  bool openFileForWrite(const char*, const std::string&, HalFile&) {
    ++cacheAccesses;
    return false;
  }
};
inline TestStorage Storage;

class BookMetadataCache {
 public:
  void createSpineEntry(const std::string&) { ++cacheAccesses; }
};
