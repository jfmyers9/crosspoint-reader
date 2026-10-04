#pragma once
#include <string>
extern bool epubLoads;
class Epub {
 public:
  Epub(const std::string&, const char*) {}
  void setupCacheDir() {}
  bool load(bool, bool) { return epubLoads; }
};
