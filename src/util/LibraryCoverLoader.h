#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <string>

#include "LibraryBookDetails.h"

// One background decoder per active screen. Call the public API from the input task.
class LibraryCoverLoader {
 public:
  LibraryCoverLoader() = default;
  ~LibraryCoverLoader();
  LibraryCoverLoader(const LibraryCoverLoader&) = delete;
  LibraryCoverLoader& operator=(const LibraryCoverLoader&) = delete;

  bool start(const std::string& path, int index, uint32_t generation);
  bool ready() const { return state.load() == State::Ready; }
  bool working() const { return state.load() == State::Working; }
  bool failed() const { return taskFailed; }
  bool takeResult(LibraryBookDetails& details, int& index, uint32_t& generation);
  void stop();
  void resetFailure() { taskFailed = false; }

 private:
  enum class State { Idle, Working, Ready };
  static void run(void* context);
  TaskHandle_t task = nullptr;
  std::atomic<State> state{State::Idle};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> stopped{false};
  std::string loadPath;
  LibraryBookDetails result;
  int loadIndex = -1;
  uint32_t loadGeneration = 0;
  bool taskFailed = false;
};
