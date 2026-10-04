#pragma once
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>

struct StoredFile {
  std::string content;
  size_t reportedSize = 0;
  bool directory = false;
};
inline size_t bytesRead = 0;
class HalFile {
 public:
  const StoredFile* data = nullptr;
  size_t offset = 0;
  size_t size() const { return data->reportedSize ? data->reportedSize : data->content.size(); }
  bool isDirectory() const { return data->directory; }
  int read() {
    if (offset >= data->content.size()) return -1;
    ++bytesRead;
    return static_cast<unsigned char>(data->content[offset++]);
  }
  int read(void* destination, size_t length) {
    auto* out = static_cast<unsigned char*>(destination);
    size_t copied = 0;
    while (copied < length) {
      const int c = read();
      if (c < 0) break;
      out[copied++] = static_cast<unsigned char>(c);
    }
    return static_cast<int>(copied);
  }
};
struct StorageStub {
  std::map<std::string, StoredFile> files;
  bool failOpen = false;
  bool exists(const char* path) const { return files.count(path); }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (failOpen || !exists(path)) return false;
    file.data = &files.at(path);
    return true;
  }
};
inline StorageStub Storage;
