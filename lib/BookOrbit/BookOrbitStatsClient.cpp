#include "BookOrbitStatsClient.h"

#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
#include <ArduinoJson.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>
#include <ReadingStatsRecorder.h>
#include <esp_mac.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr size_t MAX_RESPONSE_BYTES = 2048;
bool uploadWasUnmatched = false;

void addDevice(JsonDocument& doc) {
  doc["deviceId"] = BookOrbitStatsClient::deviceId();
  doc["deviceModel"] = "CrossPoint";
  doc["pluginVersion"] = "crosspoint-1";
}

bool post(const char* suffix, JsonDocument& request, JsonDocument& response) {
  if (request.overflowed()) {
    LOG_ERR("BOStats", "OOM: request JSON");
    return false;
  }
  // At most eight events; serialization allocates once per sync batch, outside reading.
  std::string payload;
  payload.reserve(measureJson(request));
  serializeJson(request, payload);
  std::string body;
  if (KOReaderSyncClient::postExtension(suffix, payload, body) != KOReaderSyncClient::OK) {
    LOG_ERR("BOStats", "Statistics request failed");
    return false;
  }
  if (body.size() > MAX_RESPONSE_BYTES || deserializeJson(response, body, DeserializationOption::NestingLimit(6))) {
    LOG_ERR("BOStats", "Invalid statistics response");
    return false;
  }
  return true;
}
}  // namespace

const char* BookOrbitStatsClient::deviceId() {
  static const std::array<char, 13> identity = [] {
    std::array<char, 13> value{};
    uint8_t mac[6]{};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
      LOG_ERR("BOStats", "Cannot read device identity");
      return value;
    }
    // The server uses the first eight characters for session IDs: put device-specific bytes first.
    snprintf(value.data(), value.size(), "%02x%02x%02x%02x%02x%02x", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    return value;
  }();
  return identity.data();
}

bool BookOrbitStatsClient::upload(const char* bookHash, const ReadingStatsEvent* events, size_t count) {
  uploadWasUnmatched = false;
  if (!bookHash || strlen(bookHash) != 32 || !events || count == 0 || count > MAX_BATCH_EVENTS || !deviceId()[0]) {
    LOG_ERR("BOStats", "Invalid statistics batch");
    return false;
  }
  for (size_t i = 0; i < 32; ++i) {
    if (!((bookHash[i] >= '0' && bookHash[i] <= '9') || (bookHash[i] >= 'a' && bookHash[i] <= 'f'))) {
      LOG_ERR("BOStats", "Invalid book hash");
      return false;
    }
  }
  JsonDocument request;
  addDevice(request);
  auto book = request["books"].to<JsonArray>().add<JsonObject>();
  book["hash"] = bookHash;
  auto rows = book["events"].to<JsonArray>();
  int64_t watermark = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto& event = events[i];
    if (event.startEpochSeconds <= 0 || event.durationSeconds > 86400 || event.totalPages != 10000 ||
        event.page > event.totalPages) {
      LOG_ERR("BOStats", "Invalid statistics event");
      return false;
    }
    auto row = rows.add<JsonObject>();
    // Logical whole-book progress units, never chapter-local or physical page counts.
    row["page"] = event.page;
    row["totalPages"] = event.totalPages;
    row["startTime"] = event.startEpochSeconds;
    row["durationSeconds"] = event.durationSeconds;
    if (event.startEpochSeconds > watermark) watermark = event.startEpochSeconds;
  }
  JsonDocument response;
  if (!post("/plugin/page-stats", request, response)) return false;
  const auto results = response["results"].as<JsonArrayConst>();
  const auto unmatched = response["unmatched"].as<JsonArrayConst>();
  if (!results.isNull() && results.size() == 0 && !unmatched.isNull() && unmatched.size() == 1 &&
      unmatched[0].is<const char*>() && strcmp(unmatched[0].as<const char*>(), bookHash) == 0) {
    uploadWasUnmatched = true;
    LOG_ERR("BOStats", "Book hash is unmatched");
    return false;
  }
  if (results.isNull() || results.size() != 1 || unmatched.isNull() || unmatched.size() != 0) {
    LOG_ERR("BOStats", "Book not acknowledged");
    return false;
  }
  const auto result = results[0];
  if (!result["hash"].is<const char*>() || strcmp(result["hash"].as<const char*>(), bookHash) != 0 ||
      !result["accepted"].is<uint32_t>() || !result["duplicates"].is<uint32_t>() ||
      !result["watermark"].is<int64_t>() || result["watermark"].as<int64_t>() != watermark ||
      result["accepted"].as<uint32_t>() > count || result["duplicates"].as<uint32_t>() > count ||
      result["accepted"].as<uint32_t>() + result["duplicates"].as<uint32_t>() != count) {
    LOG_ERR("BOStats", "Incomplete statistics acknowledgement");
    return false;
  }
  return true;
}

bool BookOrbitStatsClient::lastUploadWasUnmatched() { return uploadWasUnmatched; }

bool BookOrbitStatsClient::completeSweep(uint32_t uploaded, uint32_t booksMatched) {
  if (!deviceId()[0]) {
    LOG_ERR("BOStats", "Device identity unavailable");
    return false;
  }
  JsonDocument request;
  addDevice(request);
  request["booksMatched"] = booksMatched;
  request["pageStatsUploaded"] = uploaded;
  request["annotationsUpserted"] = 0;
  JsonDocument response;
  if (!post("/plugin/sweeps", request, response)) return false;
  if (!response["ok"].is<bool>() || !response["ok"].as<bool>()) {
    LOG_ERR("BOStats", "Sweep not acknowledged");
    return false;
  }
  return true;
}
#endif
