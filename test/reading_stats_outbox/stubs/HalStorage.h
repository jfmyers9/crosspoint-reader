#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct StorageStub;
extern StorageStub Storage;

class HalFile {
 public:
  std::vector<uint8_t>* bytes = nullptr;
  size_t offset = 0;
  bool shortWrite = false;
  void flush() {}
  size_t size() { return bytes->size(); }
  int read(void* destination, size_t count) {
    const size_t available = std::min(count, bytes->size() - offset);
    std::memcpy(destination, bytes->data() + offset, available);
    offset += available;
    return static_cast<int>(available);
  }
  size_t write(const uint8_t* source, size_t count) {
    if (shortWrite) count /= 2;
    bytes->insert(bytes->end(), source, source + count);
    return count;
  }
};

struct StorageStub {
  std::map<std::string, std::vector<uint8_t>> files;
  bool shortWrite = false;
  bool failRename = false;
  bool corruptReadback = false;
  bool exists(const char* path) { return files.count(path) != 0; }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    auto found = files.find(path);
    if (found == files.end()) return false;
    if (corruptReadback && !found->second.empty()) found->second.back() ^= 1;
    file.bytes = &found->second;
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    auto& bytes = files[path];
    bytes.clear();
    file.bytes = &bytes;
    file.shortWrite = shortWrite;
    return true;
  }
  bool rename(const char* from, const char* to) {
    if (failRename || exists(to)) return false;
    auto node = files.extract(from);
    if (node.empty()) return false;
    node.key() = to;
    files.insert(std::move(node));
    return true;
  }
};
