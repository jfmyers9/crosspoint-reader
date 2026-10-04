#pragma once

#include <Arduino.h>

#include <map>
#include <set>
#include <string>

class HalFile {
 public:
  size_t size() const { return bytes; }
  size_t bytes = 0;
};

class HalStorage {
 public:
  std::map<std::string, std::string> files;
  std::set<std::string> failedReads;
  std::set<std::string> failedRemovals;
  std::set<std::string> failedRenameSources;
  bool failWrite = false;
  bool shortWriteReportsSuccess = false;
  bool failOpen = false;
  unsigned writes = 0;

  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  bool mkdir(const char*) { return true; }
  bool exists(const char* path) const { return files.count(path) != 0; }
  String readFile(const char* path) const {
    const auto it = files.find(path);
    return it == files.end() || failedReads.count(path) ? String{} : String(it->second);
  }
  bool writeFile(const char* path, const String& content) {
    ++writes;
    // Model the SDK's destructive replacement, including partial writes.
    files[path] = std::string(content.c_str(), content.length());
    if (failWrite || shortWriteReportsSuccess) {
      files[path].resize(files[path].size() / 2);
    }
    return !failWrite;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) const {
    if (failOpen || !exists(path) || failedReads.count(path)) {
      return false;
    }
    file.bytes = files.at(path).size();
    return true;
  }
  bool remove(const char* path) {
    return !failedRemovals.count(path) && files.erase(path) != 0;
  }
  bool rename(const char* from, const char* to) {
    if (failedRenameSources.count(from) || !exists(from) || exists(to)) {
      return false;
    }
    files[to] = files.at(from);
    files.erase(from);
    return true;
  }
};

#define Storage HalStorage::getInstance()
