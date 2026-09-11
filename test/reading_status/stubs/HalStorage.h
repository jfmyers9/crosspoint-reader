#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

inline std::map<std::string, std::vector<uint8_t>> testFiles;
inline int testWrites = 0;
inline bool failRename = false;
class HalFile {
  std::vector<uint8_t>* bytes = nullptr;
  size_t position = 0;
 public:
  void open(std::vector<uint8_t>& data) { bytes = &data; position = 0; }
  int read(void* out, size_t count) {
    count = std::min(count, bytes->size() - position);
    memcpy(out, bytes->data() + position, count);
    position += count;
    return count;
  }
  size_t write(const void* data, size_t count) {
    ++testWrites;
    bytes->resize(position + count);
    memcpy(bytes->data() + position, data, count);
    position += count;
    return count;
  }
  uint64_t fileSize64() { return bytes->size(); }
  void flush() {}
};
class TestStorage {
 public:
  bool exists(const char* path) { return testFiles.count(path); }
  bool remove(const char* path) { return testFiles.erase(path); }
  bool rename(const char* oldPath, const char* newPath) {
    if (failRename || !exists(oldPath) || exists(newPath)) return false;
    testFiles[newPath] = std::move(testFiles[oldPath]);
    testFiles.erase(oldPath);
    return true;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (!exists(path)) return false;
    file.open(testFiles[path]);
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    testFiles[path].clear();
    file.open(testFiles[path]);
    return true;
  }
  bool ensureDirectoryExists(const char*) { return true; }
};
inline TestStorage Storage;
