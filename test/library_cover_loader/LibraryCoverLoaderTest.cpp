#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "LibraryCoverLoader.h"

using namespace std::chrono_literals;

struct HostTask {
  std::mutex mutex;
  std::condition_variable cv;
  unsigned notifications = 0;
  std::thread thread;
};

namespace {
thread_local HostTask* currentTask = nullptr;
bool failTaskCreation = false;
int tasksCreated = 0;
int tasksDeleted = 0;
std::atomic<bool> decodeEntered{false};
std::atomic<bool> decodeFinished{false};
std::atomic<bool> holdDecode{false};
std::atomic<bool> deleteBeforeDecodeFinished{false};
std::atomic<bool> decodeSuccess{true};
int requestedHeight = 0;

template <typename Predicate>
bool waitUntil(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
  return predicate();
}

class LibraryCoverLoaderTest : public testing::Test {
 protected:
  void SetUp() override {
    failTaskCreation = false;
    tasksCreated = tasksDeleted = 0;
    decodeEntered = decodeFinished = holdDecode = deleteBeforeDecodeFinished = false;
    decodeSuccess = true;
    requestedHeight = 0;
  }
};
}  // namespace

int xTaskCreate(void (*fn)(void*), const char*, unsigned, void* context, unsigned, TaskHandle_t* result) {
  if (failTaskCreation) return 0;
  auto* task = new HostTask;
  *result = task;
  ++tasksCreated;
  task->thread = std::thread([task, fn, context] {
    currentTask = task;
    fn(context);
  });
  return pdPASS;
}

void xTaskNotifyGive(TaskHandle_t task) {
  std::lock_guard<std::mutex> lock(task->mutex);
  ++task->notifications;
  task->cv.notify_one();
}

unsigned ulTaskNotifyTake(int, uint32_t) {
  std::unique_lock<std::mutex> lock(currentTask->mutex);
  currentTask->cv.wait(lock, [] { return currentTask->notifications != 0; });
  const unsigned count = currentTask->notifications;
  currentTask->notifications = 0;
  return count;
}

void vTaskDelay(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void vTaskSuspend(TaskHandle_t) {}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1024; }
void vTaskDelete(TaskHandle_t task) {
  if (decodeEntered && !decodeFinished) deleteBeforeDecodeFinished = true;
  task->thread.join();
  ++tasksDeleted;
  delete task;
}

bool loadLibraryBookDetails(const std::string& path, int coverHeight, LibraryBookDetails& out) {
  requestedHeight = coverHeight;
  decodeEntered = true;
  while (holdDecode) std::this_thread::sleep_for(1ms);
  if (decodeSuccess) out = {path, "Author", "/cached-cover.bmp"};
  decodeFinished = true;
  return decodeSuccess;
}

TEST_F(LibraryCoverLoaderTest, ReturnsResultWithIndexAndGenerationAndReusesTask) {
  LibraryCoverLoader loader;
  LibraryBookDetails result;
  int index = -1;
  uint32_t generation = 0;
  EXPECT_FALSE(loader.takeResult(result, index, generation));
  ASSERT_TRUE(loader.start("/first.epub", 7, 42));
  ASSERT_TRUE(waitUntil([&] { return loader.ready(); }));
  EXPECT_FALSE(loader.start("/overwrite.epub", 8, 43));
  ASSERT_TRUE(loader.takeResult(result, index, generation));
  EXPECT_EQ(result.title, "/first.epub");
  EXPECT_EQ(result.author, "Author");
  EXPECT_EQ(result.coverPath, "/cached-cover.bmp");
  EXPECT_EQ(index, 7);
  EXPECT_EQ(generation, 42u);
  EXPECT_EQ(requestedHeight, 96);
  EXPECT_FALSE(loader.takeResult(result, index, generation));
  ASSERT_TRUE(loader.start("/second.epub", 9, 44));
  ASSERT_TRUE(waitUntil([&] { return loader.ready(); }));
  ASSERT_TRUE(loader.takeResult(result, index, generation));
  EXPECT_EQ(result.title, "/second.epub");
  EXPECT_EQ(index, 9);
  EXPECT_EQ(generation, 44u);
  EXPECT_EQ(tasksCreated, 1);
  loader.stop();
  EXPECT_EQ(tasksDeleted, 1);
}

TEST_F(LibraryCoverLoaderTest, RefusesBusyStartWithoutReplacingActiveRequest) {
  LibraryCoverLoader loader;
  holdDecode = true;
  ASSERT_TRUE(loader.start("/active.epub", 3, 11));
  const bool entered = waitUntil([] { return decodeEntered.load(); });
  EXPECT_TRUE(entered);
  EXPECT_TRUE(loader.working());
  EXPECT_FALSE(loader.start("/wrong.epub", 4, 12));
  holdDecode = false;
  ASSERT_TRUE(waitUntil([&] { return loader.ready(); }));
  LibraryBookDetails result;
  int index;
  uint32_t generation;
  ASSERT_TRUE(loader.takeResult(result, index, generation));
  EXPECT_EQ(result.title, "/active.epub");
  EXPECT_EQ(index, 3);
  EXPECT_EQ(generation, 11u);
}

TEST_F(LibraryCoverLoaderTest, StopWaitsForDecodeAndAllowsRestart) {
  LibraryCoverLoader loader;
  holdDecode = true;
  ASSERT_TRUE(loader.start("/active.epub", 1, 1));
  EXPECT_TRUE(waitUntil([] { return decodeEntered.load(); }));
  std::thread release([] {
    std::this_thread::sleep_for(25ms);
    holdDecode = false;
  });
  loader.stop();
  release.join();
  EXPECT_TRUE(decodeFinished);
  EXPECT_FALSE(deleteBeforeDecodeFinished);
  EXPECT_EQ(tasksDeleted, 1);
  EXPECT_FALSE(loader.ready());
  EXPECT_FALSE(loader.working());
  loader.stop();
  EXPECT_EQ(tasksDeleted, 1);
  ASSERT_TRUE(loader.start("/next.epub", 2, 2));
  ASSERT_TRUE(waitUntil([&] { return loader.ready(); }));
  EXPECT_EQ(tasksCreated, 2);
}

TEST_F(LibraryCoverLoaderTest, TaskAllocationFailureRequiresResetAndCanRecover) {
  LibraryCoverLoader loader;
  failTaskCreation = true;
  EXPECT_FALSE(loader.start("/book.epub", 1, 1));
  EXPECT_TRUE(loader.failed());
  EXPECT_FALSE(loader.working());
  failTaskCreation = false;
  EXPECT_FALSE(loader.start("/book.epub", 1, 1));
  EXPECT_EQ(tasksCreated, 0);
  loader.resetFailure();
  EXPECT_FALSE(loader.failed());
  ASSERT_TRUE(loader.start("/book.epub", 1, 1));
  ASSERT_TRUE(waitUntil([&] { return loader.ready(); }));
}

TEST_F(LibraryCoverLoaderTest, FailedDecodeReturnsEmptyDetailsWithoutStaleCover) {
  LibraryCoverLoader loader;
  ASSERT_TRUE(loader.start("/good.epub", 1, 1));
  ASSERT_TRUE(waitUntil([&] { return loader.ready(); }));
  LibraryBookDetails result;
  int index;
  uint32_t generation;
  ASSERT_TRUE(loader.takeResult(result, index, generation));
  decodeSuccess = false;
  ASSERT_TRUE(loader.start("/bad.epub", 2, 2));
  ASSERT_TRUE(waitUntil([&] { return loader.ready(); }));
  ASSERT_TRUE(loader.takeResult(result, index, generation));
  EXPECT_TRUE(result.title.empty());
  EXPECT_TRUE(result.coverPath.empty());
  EXPECT_EQ(index, 2);
  EXPECT_EQ(generation, 2u);
  EXPECT_FALSE(loader.failed());
}
