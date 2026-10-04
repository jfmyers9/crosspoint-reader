#include "BookOrbitAnnotationsClient.h"

#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
#include <ArduinoJson.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include "BookOrbitStatsClient.h"

namespace {
bool uploadWasUnmatched = false;

// ArduinoJson stops at the closing brace; keep the cursor to reject trailing garbage.
struct ResponseReader {
  const std::string& body;
  size_t offset = 0;
  int read() { return offset < body.size() ? static_cast<unsigned char>(body[offset++]) : -1; }
  size_t readBytes(char* buffer, size_t length) {
    size_t count = 0;
    while (count < length && offset < body.size()) buffer[count++] = body[offset++];
    return count;
  }
  bool onlyWhitespaceRemains() {
    for (int c = read(); c != -1; c = read()) {
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return true;
  }
};

bool bounded(const char* value, size_t max, bool empty = false) {
  return value && (empty || value[0]) && strnlen(value, max + 1) <= max;
}

bool validDate(const char* value) {
  if (!bounded(value, 19) || strlen(value) != 19) return false;
  for (size_t i = 0; i < 19; ++i) {
    const char separator = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 || i == 16 ? ':' : 0;
    if (separator ? value[i] != separator : value[i] < '0' || value[i] > '9') return false;
  }
  const auto pair = [value](size_t i) { return (value[i] - '0') * 10 + value[i + 1] - '0'; };
  const int year = pair(0) * 100 + pair(2);
  const int month = pair(5), day = pair(8);
  constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year == 0 || month < 1 || month > 12 || pair(11) > 23 || pair(14) > 59 || pair(17) > 59) return false;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day >= 1 && day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}

bool validPosition(const char* value) {
  // Provenance belongs to the caller; reject empty, oversized and obvious synthetic positions here.
  return bounded(value, 4000) && strncmp(value, "/body/DocFragment[", 18) == 0 && strstr(value, "]/body/") &&
         strstr(value, "/text()[");
}
}  // namespace

bool BookOrbitAnnotationsClient::upload(const char* bookHash, const char* datetime, const char* datetimeUpdated,
                                        const char* pos0, const char* pos1, const char* text, const char* note) {
  uploadWasUnmatched = false;
  if (!bounded(bookHash, 32) || strlen(bookHash) != 32 || !validDate(datetime) || !validDate(datetimeUpdated) ||
      !validPosition(pos0) || !validPosition(pos1) || !bounded(text, 2048) || !bounded(note, 128, true) ||
      !BookOrbitStatsClient::deviceId()[0]) {
    LOG_ERR("BOAnnotations", "Invalid annotation");
    return false;
  }
  for (size_t i = 0; i < 32; ++i) {
    if (!((bookHash[i] >= '0' && bookHash[i] <= '9') || (bookHash[i] >= 'a' && bookHash[i] <= 'f'))) return false;
  }

  JsonDocument request;
  request["deviceId"] = BookOrbitStatsClient::deviceId();
  request["deviceModel"] = "CrossPoint X4 Pro";
  request["pluginVersion"] = "crosspoint-1";
  auto book = request["books"].to<JsonArray>().add<JsonObject>();
  book["hash"] = bookHash;
  auto row = book["annotations"].to<JsonArray>().add<JsonObject>();
  row["datetime"] = datetime;
  row["datetimeUpdated"] = datetimeUpdated;
  row["drawer"] = "underscore";
  row["posFormat"] = "xpointer";
  row["pos0"] = pos0;
  row["pos1"] = pos1;
  row["text"] = text;
  row["note"] = note;
  if (request.overflowed()) return false;

  // Single bounded annotation, outside the reading hot path. Heap serialization avoids
  // a ~63KB worst-case escaped payload on the small firmware task stack.
  std::string payload;
  const size_t bytes = measureJson(request);
  payload.reserve(bytes);
  if (serializeJson(request, payload) != bytes || payload.size() != bytes) return false;
  std::string body;
  if (KOReaderSyncClient::postExtension("/plugin/annotations", payload, body) != KOReaderSyncClient::OK) return false;
  JsonDocument response;
  ResponseReader reader{body};
  if (body.size() > 2048 || deserializeJson(response, reader, DeserializationOption::NestingLimit(6)) ||
      response.overflowed() || !reader.onlyWhitespaceRemains())
    return false;
  const auto results = response["results"].as<JsonArrayConst>();
  const auto unmatched = response["unmatched"].as<JsonArrayConst>();
  if (results.isNull() || unmatched.isNull()) return false;
  if (results.size() == 0 && unmatched.size() == 1 && unmatched[0].is<const char*>() &&
      unmatched[0].as<JsonString>() == bookHash) {
    uploadWasUnmatched = true;
    return false;
  }
  if (results.size() != 1 || unmatched.size() != 0) return false;
  const auto result = results[0];
  // Zero is a valid acknowledgement for retries and server-side tombstones.
  return result["hash"].is<const char*>() && result["hash"].as<JsonString>() == bookHash &&
         result["upserted"].is<unsigned int>() && result["upserted"].as<unsigned int>() <= 1;
}

bool BookOrbitAnnotationsClient::lastUploadWasUnmatched() { return uploadWasUnmatched; }
#endif
