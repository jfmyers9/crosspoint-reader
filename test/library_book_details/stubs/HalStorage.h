#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct TestFile {
  std::vector<uint8_t> bytes;
  uint16_t date = 1;
  uint16_t time = 1;
};
inline std::map<std::string, TestFile> testFiles;

class HalFile {
  TestFile* file = nullptr;
  size_t position = 0;

 public:
  void open(TestFile& value) {
    file = &value;
    position = 0;
  }
  int read(void* buffer, size_t count) {
    if (!file) return -1;
    count = std::min(count, file->bytes.size() - position);
    memcpy(buffer, file->bytes.data() + position, count);
    position += count;
    return count;
  }
  int read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
  }
  size_t write(const void* buffer, size_t count) {
    if (!file) return 0;
    file->bytes.resize(position + count);
    memcpy(file->bytes.data() + position, buffer, count);
    position += count;
    return count;
  }
  uint64_t fileSize64() const { return file ? file->bytes.size() : 0; }
  bool getModifyDateTime(uint16_t* date, uint16_t* time) {
    if (!file) return false;
    *date = file->date;
    *time = file->time;
    return true;
  }
  bool close() {
    file = nullptr;
    return true;
  }
};

class TestStorage {
 public:
  bool exists(const char* path) { return testFiles.count(path) != 0; }
  bool remove(const char* path) { return testFiles.erase(path) != 0; }
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    auto it = testFiles.find(path);
    if (it == testFiles.end()) return false;
    out.open(it->second);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    auto& file = testFiles[path];
    file.bytes.clear();
    out.open(file);
    return true;
  }
  bool ensureDirectoryExists(const char*) { return true; }
};
inline TestStorage Storage;
