#pragma once

#include <string>

// Reader calls are serialized by RenderLock. sync() runs with the reader paused.
class BookOrbitStats {
 public:
#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
  static void beginBook(const std::string& path);
  static void showPage(float progress);
  static void suspend();
  static void pause();
  static void checkpoint();
  static void sync();
#else
  static void beginBook(const std::string&) {}
  static void showPage(float) {}
  static void suspend() {}
  static void pause() {}
  static void checkpoint() {}
  static void sync() {}
#endif
};
