#include "LibraryCoverLoader.h"

#include <Arduino.h>
#include <Logging.h>

#include <utility>

#include "components/themes/BaseTheme.h"

LibraryCoverLoader::~LibraryCoverLoader() { stop(); }

bool LibraryCoverLoader::start(const std::string& path, int index, uint32_t generation) {
  if (taskFailed || state.load() != State::Idle) return false;
  if (!task) {
    stopRequested.store(false);
    stopped.store(false);
    // Reuse one worker stack; ZIP/image decoding must not run on the input task's small stack.
    if (xTaskCreate(&LibraryCoverLoader::run, "LibraryCovers", 8192, this, 1, &task) != pdPASS) {
      LOG_ERR("LIB", "OOM: cover loader task");
      task = nullptr;
      taskFailed = true;
      return false;
    }
  }
  loadPath = path;
  loadIndex = index;
  loadGeneration = generation;
  result = {};
  state.store(State::Working);
  xTaskNotifyGive(task);
  return true;
}

bool LibraryCoverLoader::takeResult(LibraryBookDetails& details, int& index, uint32_t& generation) {
  if (!ready()) return false;
  details = std::move(result);
  index = loadIndex;
  generation = loadGeneration;
  state.store(State::Idle);
  return true;
}

void LibraryCoverLoader::run(void* context) {
  auto* self = static_cast<LibraryCoverLoader*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self->stopRequested.load()) break;
    if (!self->working()) continue;
    LOG_DBG("LIB", "Preview start: heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
    loadLibraryBookDetails(self->loadPath, BaseTheme::LIBRARY_COVER_HEIGHT, self->result);
    LOG_DBG("LIB", "Preview complete: heap=%u, stack remaining=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    self->state.store(State::Ready);
  }
  // Parser objects and SD handles must unwind before the owner deletes the task.
  self->stopped.store(true);
  vTaskSuspend(nullptr);
}

void LibraryCoverLoader::stop() {
  if (task) {
    stopRequested.store(true);
    xTaskNotifyGive(task);
    while (!stopped.load()) vTaskDelay(pdMS_TO_TICKS(10));
    vTaskDelete(task);
    task = nullptr;
  }
  state.store(State::Idle);
  result = {};
  loadPath.clear();
}
