#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class Stream {
 public:
  virtual ~Stream() = default;
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t*, size_t length) { return length; }
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  virtual void flush() {}
};
struct HalFile {
  std::string* contents = nullptr;
  size_t write(const uint8_t* data, size_t length) {
    contents->append(reinterpret_cast<const char*>(data), length);
    return length;
  }
  bool isOpen() const { return contents != nullptr; }
  void close() { contents = nullptr; }
};
struct StorageStub {
  std::map<std::string, std::string> files;
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    files[path].clear();
    file.contents = &files[path];
    return true;
  }
  bool replaceFile(const char* from, const char* to) {
    files[to] = files.at(from);
    files.erase(from);
    return true;
  }
};
inline StorageStub Storage;
