#pragma once
#include <string>
extern bool epubLoads;
extern std::string tocTitle;
extern int tocIndex, lastTocSpine;
class Epub {
 public:
  Epub(const std::string&, const char*) {}
  void setupCacheDir() {}
  bool load(bool, bool) { return epubLoads; }
  int getTocIndexForSpineIndex(int spine) {
    lastTocSpine = spine;
    return tocIndex;
  }
  struct TocEntry {
    std::string title;
  };
  TocEntry getTocItem(int) { return {tocTitle}; }
};
