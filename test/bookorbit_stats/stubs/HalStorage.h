#pragma once

#include <fcntl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

class HalFile {
 public:
  std::vector<uint8_t>* bytes = nullptr;
  std::string path;
  std::vector<std::string> children;
  size_t offset = 0;
  bool directory = false;
  explicit operator bool() const { return bytes || directory; }
  bool isDirectory() const { return directory; }
  void close() { bytes = nullptr; }
  void flush() {}
  size_t size() const { return bytes ? bytes->size() : 0; }
  size_t fileSize() const { return size(); }
  int read(void* destination, size_t count) {
    if (!bytes) return -1;
    const size_t available = std::min(count, bytes->size() - offset);
    std::memcpy(destination, bytes->data() + offset, available);
    offset += available;
    return static_cast<int>(available);
  }
  int read() {
    uint8_t value;
    return read(&value, 1) == 1 ? value : -1;
  }
  size_t write(const void* source, size_t count) {
    if (!bytes) return 0;
    const auto* begin = static_cast<const uint8_t*>(source);
    bytes->insert(bytes->end(), begin, begin + count);
    return count;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  void getName(char* destination, size_t count) const {
    std::snprintf(destination, count, "%s", path.substr(path.find_last_of('/') + 1).c_str());
  }
  HalFile openNextFile();
};

struct StorageStub {
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> directories;
  bool failRename = false;
  bool exists(const char* path) const { return files.count(path) || directories.count(path); }
  bool mkdir(const char* path, bool) {
    directories.insert(path);
    return true;
  }
  bool ensureDirectoryExists(const char* path) { return mkdir(path, true); }
  HalFile open(const char* path, int mode = O_RDONLY) {
    HalFile file;
    file.path = path;
    if (directories.count(path)) {
      file.directory = true;
      const std::string prefix = std::string(path) + "/";
      for (const auto& [name, bytes] : files) {
        (void)bytes;
        if (name.starts_with(prefix)) file.children.push_back(name);
      }
    } else if (mode & O_CREAT) {
      if ((mode & O_EXCL) && exists(path)) return file;
      file.bytes = &files[path];
      if (mode & O_TRUNC) file.bytes->clear();
    } else {
      auto found = files.find(path);
      if (found != files.end()) file.bytes = &found->second;
    }
    return file;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file = open(path);
    return static_cast<bool>(file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    return static_cast<bool>(file);
  }
  bool rename(const char* from, const char* to) {
    if (failRename || exists(to)) return false;
    auto node = files.extract(from);
    if (node.empty()) return false;
    node.key() = to;
    files.insert(std::move(node));
    return true;
  }
  bool remove(const char* path) { return files.erase(path) != 0; }
};

extern StorageStub Storage;
inline HalFile HalFile::openNextFile() {
  return offset < children.size() ? Storage.open(children[offset++].c_str()) : HalFile{};
}
