#include "LocalReadingStats.h"

#if defined(CROSSPOINT_ENABLE_LOCAL_READING_STATS)
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <esp_timer.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "ReadingStatsRecorder.h"

namespace {
constexpr char ROOT[] = "/.crosspoint/local-reading-stats";
constexpr char GLOBAL[] = "/.crosspoint/local-reading-stats/global.bin";
constexpr uint32_t MAGIC = 0x3153524c;
struct Record {
  uint32_t magic = MAGIC;
  uint32_t version = 1;
  LocalStats::Totals totals;
  uint32_t checksum = 0;
};
// Reusable static records avoid >256-byte stack frames and per-event allocation.
Record globalRecord, bookRecord, scratch;
bool globalLoaded = false, bookLoaded = false, globalDirty = false, bookDirty = false;
bool globalBackup = false, bookBackup = false;
char bookFile[100]{};
LocalStats::Pace pace;
uint64_t lastWriteMs = 0;
int64_t clockEpoch = 0;
uint64_t pageClockMs = 0;
int64_t pageClockEpoch = 0;
uint32_t lastUnits = 0;
bool pageOpen = false;
bool recording = false;
bool saveFailed = false;
uint64_t nowMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
void refreshClock(uint64_t) {
  clockEpoch = 0;
  halClock.getUtcTime(clockEpoch);
}
uint32_t checksum(const Record& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < offsetof(Record, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619U;
  return hash;
}
bool valid(const Record& record) {
  if (record.magic != MAGIC || record.version != 1 || record.checksum != checksum(record)) return false;
  for (unsigned i = 0; i < LocalStats::DAYS; ++i) {
    const auto& day = record.totals.daily[i];
    if ((!day.dateKey && day.seconds) || day.seconds > record.totals.seconds) return false;
    if (day.dateKey && (day.dateKey < 20240101 || day.dateKey > 21000101 || day.dateKey % 100 < 1 ||
                        day.dateKey % 100 > 31 || day.dateKey / 100 % 100 < 1 || day.dateKey / 100 % 100 > 12))
      return false;
    for (unsigned j = 0; j < i; ++j)
      if (day.dateKey && day.dateKey == record.totals.daily[j].dateKey) return false;
  }
  return true;
}
bool readRecord(const char* path) {
  HalFile file;
  return Storage.openFileForRead("LOCALSTATS", path, file) && file.size() == sizeof(scratch) &&
         file.read(&scratch, sizeof(scratch)) == sizeof(scratch) && valid(scratch);
}
bool load(const char* path, Record& target, bool& fromBackup) {
  fromBackup = false;
  if (!Storage.ready()) return false;
  if (readRecord(path)) {
    target = scratch;
    return true;
  }
  char backup[110];
  snprintf(backup, sizeof(backup), "%s.bak", path);
  if (readRecord(backup)) {
    target = scratch;
    fromBackup = true;
    return true;
  }
  char staged[110];
  snprintf(staged, sizeof(staged), "%s.tmp", path);
  // An interrupted first save may have only its validated staged record.
  if (!Storage.exists(path) && !Storage.exists(backup) && readRecord(staged)) {
    target = scratch;
    return Storage.rename(staged, path);
  }
  if (Storage.exists(path) || Storage.exists(backup) || Storage.exists(staged)) {
    LOG_ERR("LOCALSTATS", "Unreadable history retained: %s", path);
    return false;
  }
  target = {};
  return true;
}
bool save(const char* path, Record& record, bool& fromBackup) {
  if (!Storage.ready() || !Storage.ensureDirectoryExists(ROOT)) return false;
  char staged[110], backup[110];
  snprintf(staged, sizeof(staged), "%s.tmp", path);
  snprintf(backup, sizeof(backup), "%s.bak", path);
  record.checksum = checksum(record);
  {
    HalFile file;
    if (!Storage.openFileForWrite("LOCALSTATS", staged, file) || file.write(&record, sizeof(record)) != sizeof(record))
      return false;
    file.flush();
  }
  if (!readRecord(staged) || memcmp(&record, &scratch, sizeof(record))) return false;
  if (Storage.exists(path)) {
    if (fromBackup) {
      if (!Storage.remove(path)) return false;
    } else {
      if (Storage.exists(backup) && !Storage.remove(backup)) return false;
      if (!Storage.rename(path, backup)) return false;
      fromBackup = true;
    }
  }
  if (!Storage.rename(staged, path)) return false;
  fromBackup = false;
  return true;
}
void flush(uint64_t now) {
  lastWriteMs = now;
  if (globalLoaded && globalDirty && save(GLOBAL, globalRecord, globalBackup)) globalDirty = false;
  if (bookLoaded && bookDirty && save(bookFile, bookRecord, bookBackup)) bookDirty = false;
  saveFailed = globalDirty || bookDirty;
}
bool accept(void*, const ReadingStatsEvent& event) {
  int64_t epoch = 0;
  if (pageClockEpoch > 0 && clockEpoch > 0 && event.startMonotonicMs >= pageClockMs)
    epoch = pageClockEpoch + static_cast<int64_t>((event.startMonotonicMs - pageClockMs) / 1000);
  LocalStats::add(globalRecord.totals, epoch, event.durationSeconds);
  LocalStats::add(bookRecord.totals, epoch, event.durationSeconds);
  pace.credit(event.durationSeconds);
  globalDirty = bookDirty = true;
  return true;
}
ReadingStatsRecorder recorder(accept, nullptr);
}  // namespace

void LocalReadingStats::beginBook(const std::string& path) {
  pause();
  recording = false;
  pace.reset();
  // Never discard unsaved history on a failed book switch. Recording resumes
  // when the same pending records can be saved successfully.
  if (bookDirty && bookLoaded) return;
  if (!globalLoaded) globalLoaded = load(GLOBAL, globalRecord, globalBackup);
  MD5Builder md5;
  md5.begin();
  md5.add(path.c_str());
  md5.calculate();
  snprintf(bookFile, sizeof(bookFile), "%s/%s.bin", ROOT, md5.toString().c_str());
  bookLoaded = load(bookFile, bookRecord, bookBackup);
  bookDirty = false;
  recording = bookLoaded && globalLoaded;
  pace.reset();
}
void LocalReadingStats::showPage(float progress) {
  if (!recording || !std::isfinite(progress) || progress < 0 || progress > 1) return;
  const uint64_t now = nowMs();
  refreshClock(now);
  const uint32_t units = static_cast<uint32_t>(std::lround(progress * 10000));
  recorder.showPage(units, 10000, now);
  pace.show(units);
  if (!pageOpen || units != lastUnits) {
    pageClockMs = now;
    pageClockEpoch = clockEpoch;
  }
  pageOpen = true;
  lastUnits = units;
}
void LocalReadingStats::suspend() {
  const uint64_t now = nowMs();
  refreshClock(now);
  recorder.suspend(now);
  pageOpen = false;
}
void LocalReadingStats::pause() {
  const uint64_t now = nowMs();
  refreshClock(now);
  recorder.pause(now);
  pageOpen = false;
  pace.breakSequence();
  flush(now);
}
void LocalReadingStats::breakSequence() {
  const uint64_t now = nowMs();
  refreshClock(now);
  recorder.pause(now);
  pageOpen = false;
  pace.breakSequence();
}
void LocalReadingStats::resetPace() {
  breakSequence();
  pace.reset();
}
void LocalReadingStats::checkpoint() {
  if (!globalLoaded) globalLoaded = load(GLOBAL, globalRecord, globalBackup);
  const uint64_t now = nowMs();
  refreshClock(now);
  recorder.checkpoint(now);
  if (now - lastWriteMs >= 30000) flush(now);
}
void LocalReadingStats::getSummary(Summary& out) {
  out.clockValid = clockEpoch > 0;
  out.storageHealthy = recording && globalLoaded && bookLoaded && !saveFailed;
  out.totalSeconds = globalRecord.totals.seconds;
  out.bookSeconds = bookRecord.totals.seconds;
  LocalStats::recentDays(globalRecord.totals, clockEpoch, out.daily);
  LocalStats::recentDays(bookRecord.totals, clockEpoch, out.bookDaily);
  out.todaySeconds = out.clockValid ? out.daily[0].seconds : 0;
}
bool LocalReadingStats::estimateSeconds(float remainingProgress, uint32_t& seconds) {
  return recording && !saveFailed && pace.estimate(remainingProgress, seconds);
}
#else
void LocalReadingStats::beginBook(const std::string&) {}
void LocalReadingStats::showPage(float) {}
void LocalReadingStats::suspend() {}
void LocalReadingStats::pause() {}
void LocalReadingStats::checkpoint() {}
void LocalReadingStats::resetPace() {}
void LocalReadingStats::breakSequence() {}
void LocalReadingStats::getSummary(Summary& out) { out = {}; }
bool LocalReadingStats::estimateSeconds(float, uint32_t&) { return false; }
#endif
