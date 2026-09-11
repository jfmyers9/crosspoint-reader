#include "BookOrbitStats.h"

#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
#include <HalClock.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <ReadingStatsOutbox.h>
#include <ReadingStatsRecorder.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "BookOrbitStatsClient.h"

namespace {
constexpr char ROOT[] = "/.crosspoint/reading-stats";
constexpr char API_SUFFIX[] = "/api/v1/koreader";
constexpr uint32_t PROGRESS_UNITS = 10000;
constexpr size_t MAX_QUEUE_BATCHES = 2048;
constexpr size_t MAX_UPLOAD_REQUESTS = 32;
constexpr uint64_t UPLOAD_BUDGET_MS = 45000;
constexpr uint64_t CLOCK_REFRESH_MS = 6ULL * 60 * 60 * 1000;

uint64_t nowMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }

bool endsWith(const char* value, const char* suffix) {
  const size_t length = strlen(value);
  const size_t suffixLength = strlen(suffix);
  return length >= suffixLength && strcmp(value + length - suffixLength, suffix) == 0;
}

bool isBookOrbit() {
  return KOREADER_STORE.hasCredentials() && endsWith(KOREADER_STORE.getBaseUrl().c_str(), API_SUFFIX);
}

// Fixed buffers keep recording allocation-free apart from HAL file handles.
// History lives on SD; the second batch is reused for validation and upload.
struct StatsState {
  char scope[96] = {};
  char bootId[17] = {};
  ReadingStatsOutbox::Batch pending;
  ReadingStatsOutbox::Batch scratch;
  size_t queueBatches = 0;
  bool recording = false;
  bool anchored = false;
  uint64_t anchorMs = 0;
  int64_t anchorEpoch = 0;
  uint64_t lastClockRefreshMs = 0;
  bool clockRefreshAttempted = false;

  static bool accept(void* context, const ReadingStatsEvent& event);
  ReadingStatsRecorder recorder{accept, this};

  bool flush() {
    if (pending.count == 0) return true;
    if (queueBatches >= MAX_QUEUE_BATCHES) {
      LOG_ERR("BOSTATS", "Outbox full; retaining pending events");
      return false;
    }
    if (!ReadingStatsOutbox::save(scope, pending)) return false;
    ++queueBatches;
    pending.count = 0;
    return true;
  }
};

StatsState stats;

bool StatsState::accept(void* context, const ReadingStatsEvent& event) {
  auto& self = *static_cast<StatsState*>(context);
  if (self.pending.count == 8 && !self.flush()) return false;
  self.pending.events[self.pending.count++] = event;
  return true;
}

// A failed sink can retain one event beyond the full batch. Drain it before
// changing books or rebooting, after a flush has made room.
bool finishRecording() {
  const bool paused = stats.recorder.pause(nowMs());
  if (!stats.flush()) return false;
  if (!paused && !stats.recorder.pause(nowMs())) return false;
  return stats.flush();
}

bool configureScope() {
  if (!isBookOrbit()) return false;
  const char* device = BookOrbitStatsClient::deviceId();
  if (!device[0]) return false;
  // Include device identity so moving an SD card cannot replay another device's events.
  MD5Builder md5;
  md5.begin();
  const std::string url = KOREADER_STORE.getBaseUrl();
  md5.add(url.c_str());
  md5.add("\n");
  md5.add(KOREADER_STORE.getUsername().c_str());
  md5.add("\n");
  md5.add(device);
  md5.calculate();
  char scope[96];
  snprintf(scope, sizeof(scope), "%s/%s", ROOT, md5.toString().c_str());
  if (strcmp(stats.scope, scope) == 0) return true;
  if (!stats.flush()) return false;
  if (!Storage.ensureDirectoryExists(scope)) {
    LOG_ERR("BOSTATS", "Cannot create outbox directory");
    return false;
  }
  size_t queueBatches = 0;
  auto dir = Storage.open(scope);
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("BOSTATS", "Cannot scan outbox directory");
    return false;
  }
  while (auto file = dir.openNextFile()) {
    char name[64];
    file.getName(name, sizeof(name));
    if (!file.isDirectory() && endsWith(name, ".bin")) ++queueBatches;
  }
  snprintf(stats.scope, sizeof(stats.scope), "%s", scope);
  stats.anchored = false;
  stats.queueBatches = queueBatches;
  return true;
}

uint32_t anchorChecksum(uint64_t monotonic, int64_t epoch) {
  // Integrity check for the tiny immutable boot-to-UTC mapping.
  uint32_t value = 2166136261U;
  for (unsigned i = 0; i < 8; ++i) {
    value = (value ^ static_cast<uint8_t>(monotonic >> (8 * i))) * 16777619U;
    value = (value ^ static_cast<uint8_t>(static_cast<uint64_t>(epoch) >> (8 * i))) * 16777619U;
  }
  return value;
}

bool readAnchorPath(const char* path, uint64_t& monotonic, int64_t& epoch) {
  auto file = Storage.open(path);
  if (!file) return false;
  char data[80] = {};
  if (file.fileSize() >= sizeof(data)) return false;
  const int bytes = file.read(data, sizeof(data) - 1);
  if (bytes <= 0) return false;
  unsigned long long readMs = 0;
  long long readEpoch = 0;
  unsigned checksum = 0;
  if (sscanf(data, "BOSC1 %llu %lld %x", &readMs, &readEpoch, &checksum) != 3 || readEpoch < 1704067200LL ||
      readEpoch >= 4102444800LL || checksum != anchorChecksum(readMs, readEpoch))
    return false;
  monotonic = readMs;
  epoch = readEpoch;
  return true;
}

bool readAnchor(const char* bootId, uint64_t& monotonic, int64_t& epoch) {
  char path[128];
  snprintf(path, sizeof(path), "%s/%s.clk", stats.scope, bootId);
  return readAnchorPath(path, monotonic, epoch);
}

bool saveAnchor(uint64_t monotonic, int64_t epoch) {
  static char path[128];
  static char temporary[128];
  snprintf(path, sizeof(path), "%s/%s.clk", stats.scope, stats.bootId);
  snprintf(temporary, sizeof(temporary), "%s/%s.clock.tmp", stats.scope, stats.bootId);
  // Never change an anchor used by an already uploaded event.
  if (Storage.exists(path)) return false;
  {
    auto file = Storage.open(temporary, O_WRONLY | O_CREAT | O_TRUNC);
    char data[80];
    const int length =
        snprintf(data, sizeof(data), "BOSC1 %llu %lld %08x\n", static_cast<unsigned long long>(monotonic),
                 static_cast<long long>(epoch), anchorChecksum(monotonic, epoch));
    if (!file || file.write(data, length) != static_cast<size_t>(length)) {
      LOG_ERR("BOSTATS", "Cannot persist time anchor");
      return false;
    }
    file.flush();
  }
  uint64_t checkMs;
  int64_t checkEpoch;
  if (!readAnchorPath(temporary, checkMs, checkEpoch) || checkMs != monotonic || checkEpoch != epoch ||
      !Storage.rename(temporary, path)) {
    LOG_ERR("BOSTATS", "Cannot publish time anchor");
    return false;
  }
  return true;
}

void anchorClock() {
  if (stats.anchored || !stats.scope[0]) return;
  if (!readAnchor(stats.bootId, stats.anchorMs, stats.anchorEpoch)) {
    if (!halClock.getUtcTime(stats.anchorEpoch)) return;
    stats.anchorMs = nowMs();
    if (!saveAnchor(stats.anchorMs, stats.anchorEpoch)) return;
  }
  stats.anchored = true;
  stats.recorder.setTimeAnchor(stats.anchorMs, stats.anchorEpoch);
}

bool resolveTimes(ReadingStatsOutbox::Batch& batch) {
  uint64_t anchorMs = 0;
  int64_t anchorEpoch = 0;
  bool loaded = false;
  for (size_t i = 0; i < batch.count; ++i) {
    auto& event = batch.events[i];
    if (event.startEpochSeconds != 0) continue;
    if (!loaded) {
      if (!readAnchor(batch.bootId, anchorMs, anchorEpoch)) return false;
      loaded = true;
    }
    if (event.startMonotonicMs >= anchorMs) {
      event.startEpochSeconds = anchorEpoch + static_cast<int64_t>((event.startMonotonicMs - anchorMs) / 1000);
    } else {
      event.startEpochSeconds = anchorEpoch - static_cast<int64_t>((anchorMs - event.startMonotonicMs + 999) / 1000);
    }
    if (event.startEpochSeconds < 1704067200LL || event.startEpochSeconds >= 4102444800LL) return false;
  }
  return true;
}

bool markSweep() {
  char path[128];
  snprintf(path, sizeof(path), "%s/sweep.pending", stats.scope);
  if (Storage.exists(path)) return true;
  {
    auto file = Storage.open(path, O_WRONLY | O_CREAT | O_EXCL);
    if (!file || file.write(static_cast<uint8_t>(1)) != 1) {
      LOG_ERR("BOSTATS", "Cannot persist sweep intent");
      return false;
    }
    file.flush();
  }
  auto file = Storage.open(path);
  return file && file.read() == 1;
}
}  // namespace

void BookOrbitStats::beginBook(const std::string& path) {
  stats.recording = false;
  if (!finishRecording() || !configureScope()) return;
  if (!stats.bootId[0]) {
    snprintf(stats.bootId, sizeof(stats.bootId), "%08x%08x", static_cast<unsigned>(esp_random()),
             static_cast<unsigned>(esp_random()));
  }
  if (!stats.flush()) return;
  // Use content identity even when progress sync is configured for filename matching.
  const std::string hash = KOReaderDocumentId::calculate(path);
  if (hash.size() != 32) {
    LOG_ERR("BOSTATS", "Cannot identify book for statistics");
    return;
  }
  stats.recorder = ReadingStatsRecorder(StatsState::accept, &stats);
  snprintf(stats.pending.bookHash, sizeof(stats.pending.bookHash), "%s", hash.c_str());
  snprintf(stats.pending.bootId, sizeof(stats.pending.bootId), "%s", stats.bootId);
  anchorClock();
  if (stats.anchored) stats.recorder.setTimeAnchor(stats.anchorMs, stats.anchorEpoch);
  stats.recording = true;
}

void BookOrbitStats::showPage(float progress) {
  if (!stats.recording || !std::isfinite(progress)) return;
  const auto units = static_cast<uint32_t>(std::clamp(progress, 0.0f, 1.0f) * PROGRESS_UNITS + 0.5f);
  if (!stats.recorder.showPage(units, PROGRESS_UNITS, nowMs())) {
    LOG_ERR("BOSTATS", "Cannot record page; pending event retained");
  }
}

void BookOrbitStats::suspend() {
  if (stats.recording && !stats.recorder.suspend(nowMs())) LOG_ERR("BOSTATS", "Cannot suspend recorder");
}

void BookOrbitStats::pause() {
  if (!stats.recording) return;
  if (!finishRecording()) LOG_ERR("BOSTATS", "Cannot finalize reading interval");
}

void BookOrbitStats::checkpoint() {
  if (!stats.recording) return;
  anchorClock();
  if (!stats.recorder.checkpoint(nowMs())) LOG_ERR("BOSTATS", "Cannot checkpoint reading interval");
  stats.flush();
}

void BookOrbitStats::sync() {
  const uint64_t startedMs = nowMs();
  stats.recording = false;
  const bool finalized = finishRecording();
  if (!configureScope()) return;
  if (!stats.bootId[0]) {
    snprintf(stats.bootId, sizeof(stats.bootId), "%08x%08x", static_cast<unsigned>(esp_random()),
             static_cast<unsigned>(esp_random()));
  }
  int64_t utc;
  if (!halClock.getUtcTime(utc) || !stats.clockRefreshAttempted ||
      nowMs() - stats.lastClockRefreshMs >= CLOCK_REFRESH_MS) {
    stats.clockRefreshAttempted = true;
    stats.lastClockRefreshMs = nowMs();
    halClock.syncFromNTP();
  }
  anchorClock();
  // A full queue must still be drained even when its RAM tail cannot yet flush.
  auto dir = Storage.open(stats.scope);
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("BOSTATS", "Cannot open upload queue");
    return;
  }
  static char sweepPath[128];
  snprintf(sweepPath, sizeof(sweepPath), "%s/sweep.pending", stats.scope);
  const bool hadSweepIntent = Storage.exists(sweepPath);
  size_t requests = 0;
  uint32_t uploadedEvents = 0;
  bool backlog = false;
  // Sync is serialized. Reuse fixed storage instead of allocating upload history
  // or putting the batch and filenames on the constrained reader task's stack.
  static ReadingStatsOutbox::Batch group;
  static char groupNames[ReadingStatsOutbox::MAX_EVENTS][64];
  static char batchPath[160];
  static char uploadedHashes[MAX_UPLOAD_REQUESTS][33];
  group.count = 0;
  size_t groupFiles = 0;
  uint32_t booksMatched = 0;
  const auto exhausted = [&]() { return requests >= MAX_UPLOAD_REQUESTS || nowMs() - startedMs >= UPLOAD_BUDGET_MS; };
  const auto uploadGroup = [&]() {
    if (group.count == 0) return true;
    if (exhausted()) return false;
    if (!markSweep()) return false;
    ++requests;
    if (!BookOrbitStatsClient::upload(group.bookHash, group.events, group.count)) {
      if (!BookOrbitStatsClient::lastUploadWasUnmatched()) return false;
      LOG_INF("BOSTATS", "Unmatched book retained; continuing other books");
      if (!hadSweepIntent && uploadedEvents == 0 && !Storage.remove(sweepPath)) {
        LOG_ERR("BOSTATS", "Cannot clear unused sweep marker");
      }
      group.count = 0;
      groupFiles = 0;
      return true;
    }
    bool known = false;
    for (uint32_t i = 0; i < booksMatched; ++i) {
      if (strcmp(uploadedHashes[i], group.bookHash) == 0) known = true;
    }
    if (!known) snprintf(uploadedHashes[booksMatched++], sizeof(uploadedHashes[0]), "%s", group.bookHash);
    uploadedEvents += group.count;
    for (size_t i = 0; i < groupFiles; ++i) {
      snprintf(batchPath, sizeof(batchPath), "%s/%s", stats.scope, groupNames[i]);
      if (!Storage.remove(batchPath)) {
        LOG_ERR("BOSTATS", "Cannot remove acknowledged batch; will retry");
        return false;
      }
      if (stats.queueBatches > 0) --stats.queueBatches;
    }
    group.count = 0;
    groupFiles = 0;
    return true;
  };
  while (auto file = dir.openNextFile()) {
    if (exhausted()) {
      backlog = true;
      break;
    }
    char name[64];
    file.getName(name, sizeof(name));
    if (file.isDirectory() || !endsWith(name, ".bin")) continue;
    file.close();  // Group acknowledgment may remove this immutable batch.
    if (exhausted()) {
      backlog = true;
      break;
    }
    {
      snprintf(batchPath, sizeof(batchPath), "%s/%s", stats.scope, name);
      if (!ReadingStatsOutbox::load(batchPath, stats.scratch) || !resolveTimes(stats.scratch)) {
        LOG_ERR("BOSTATS", "Retaining corrupt or undated statistics batch");
        continue;
      }
    }
    if (group.count != 0 && (strcmp(group.bookHash, stats.scratch.bookHash) != 0 ||
                             group.count + stats.scratch.count > ReadingStatsOutbox::MAX_EVENTS)) {
      if (!uploadGroup()) {
        backlog = true;
        break;
      }
    }
    if (group.count == 0) snprintf(group.bookHash, sizeof(group.bookHash), "%s", stats.scratch.bookHash);
    std::copy_n(stats.scratch.events, stats.scratch.count, group.events + group.count);
    group.count += stats.scratch.count;
    snprintf(groupNames[groupFiles++], sizeof(groupNames[0]), "%s", name);
    if (group.count == ReadingStatsOutbox::MAX_EVENTS && !uploadGroup()) {
      backlog = true;
      break;
    }
  }
  if (!backlog && !uploadGroup()) backlog = true;
  if (backlog) LOG_INF("BOSTATS", "Statistics upload paused; remaining batches retained");
  // New batches appended after directory iteration are handled next time.
  const bool hadPending = stats.pending.count != 0 || !finalized;
  if (!finishRecording() || backlog || hadPending || nowMs() - startedMs >= UPLOAD_BUDGET_MS) return;
  if ((hadSweepIntent || uploadedEvents > 0) && Storage.exists(sweepPath) &&
      BookOrbitStatsClient::completeSweep(uploadedEvents, booksMatched)) {
    if (!Storage.remove(sweepPath)) LOG_ERR("BOSTATS", "Cannot clear completed sweep marker");
  }
}
#endif
