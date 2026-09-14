#include "KOReaderSyncClient.h"

#include <ArduinoJson.h>
#include <HalMemory.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <base64.h>

#include <cstring>
#include <string>

#include "KOReaderCredentialStore.h"

#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
#include <BookOrbitStatsClient.h>
#endif

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
#include <HTTPClient.h>
#include <Memory.h>
#include <TailscaleManager.h>
#include <TailscaleNetworkClient.h>
#endif

int KOReaderSyncClient::lastHttpCode = 0;
KOReaderSyncClient::Diagnostic KOReaderSyncClient::lastDiagnostic;

namespace {
// Device identifier for CrossPoint reader
constexpr char DEVICE_NAME[] = "CrossPoint";
constexpr char DEVICE_ID[] = "crosspoint-reader";

// wolfSSL uses the default allocator, which can use PSRAM on supported builds.
// Keep a free-space floor and room for a full TLS record when the server does
// not negotiate our smaller record limit. These are preflight margins, not a
// guarantee that a handshake will fit.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;
constexpr int32_t HTTP_TIMEOUT_MS = 15000;

// x-auth-* is the native KOSync scheme; Basic auth supports compatible servers.
void applyAuthHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("x-auth-user", KOREADER_STORE.getUsername());
  http.addHeader("x-auth-key", KOREADER_STORE.getMd5Password());
  const std::string credentials = KOREADER_STORE.getUsername() + ":" + KOREADER_STORE.getPassword();
  const String encoded = base64::encode(credentials.c_str());
  http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
}

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
void applyAuthHeaders(HTTPClient& http) {
  http.addHeader("x-auth-user", KOREADER_STORE.getUsername().c_str());
  http.addHeader("x-auth-key", KOREADER_STORE.getMd5Password().c_str());
  const std::string credentials = KOREADER_STORE.getUsername() + ":" + KOREADER_STORE.getPassword();
  const String encoded = base64::encode(credentials.c_str());
  http.addHeader("Authorization", String("Basic ") + encoded);
}
#endif

// True when free heap is too low to risk a TLS handshake.
bool insufficientHeap() {
  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < MIN_FREE_FOR_TLS || heap.largestBlockBytes < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("KOSync",
            "Insufficient allocatable heap for TLS handshake: %zu bytes free (need %u), %zu max alloc (need %u)",
            heap.freeBytes, MIN_FREE_FOR_TLS, heap.largestBlockBytes, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}

bool requiresTls(const std::string& url) { return url.compare(0, 8, "https://") == 0; }

#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
bool isBookOrbitUrl(const std::string& url) {
  constexpr char SUFFIX[] = "/api/v1/koreader";
  return url.size() >= sizeof(SUFFIX) - 1 &&
         url.compare(url.size() - (sizeof(SUFFIX) - 1), sizeof(SUFFIX) - 1, SUFFIX) == 0;
}
#endif

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
class BoundedResponseStream : public Stream {
 public:
  BoundedResponseStream(std::string& body, size_t limit) : body(body), limit(limit) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t count) override {
    if (count > limit - body.size()) return 0;
    body.append(reinterpret_cast<const char*>(bytes), count);
    return count;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  std::string& body;
  size_t limit;
};

int sendMagicDnsRequest(const char* method, const std::string& url, const std::string& payload, bool withAuth,
                        bool jsonBody, std::string* responseBody, size_t maxResponseBytes = 0) {
  if (url.compare(0, 7, "http://") != 0) {
    LOG_ERR("KOSync", "Tailnet MagicDNS currently requires plain HTTP: %s", url.c_str());
    return -1;
  }

  // These transport objects are too large for the activity task stack. Their
  // request-scoped RAII allocations are released together on every exit path.
  auto transport = makeUniqueNoThrow<TailscaleNetworkClient>();
  auto http = makeUniqueNoThrow<HTTPClient>();
  if (!transport || !http) {
    LOG_ERR("KOSync", "OOM: tailnet HTTP transport");
    return -1;
  }

  http->setConnectTimeout(HTTP_TIMEOUT_MS);
  http->setTimeout(HTTP_TIMEOUT_MS);
  if (!http->begin(*transport, url.c_str())) {
    LOG_ERR("KOSync", "Bad tailnet URL: %s", url.c_str());
    return -1;
  }
  http->addHeader("Accept", "application/vnd.koreader.v1+json");
  if (withAuth) applyAuthHeaders(*http);
  if (jsonBody) http->addHeader("Content-Type", "application/json");

  const int httpCode =
      strcmp(method, "GET") == 0
          ? http->GET()
          : http->sendRequest(method, reinterpret_cast<uint8_t*>(const_cast<char*>(payload.data())), payload.size());
  if (responseBody && httpCode > 0) {
    if (maxResponseBytes > 0) {
      responseBody->clear();
      responseBody->reserve(maxResponseBytes);
      BoundedResponseStream sink(*responseBody, maxResponseBytes);
      if (http->writeToStream(&sink) < 0) {
        LOG_ERR("KOSync", "Incomplete or oversized extension response");
        http->end();
        return -1;
      }
    } else {
      const String body = http->getString();
      responseBody->assign(body.c_str(), body.length());
    }
  }
  http->end();
  return httpCode;
}
#endif

int sendSyncRequest(const char* method, const std::string& url, const std::string& payload, bool withAuth,
                    bool jsonBody, std::string* responseBody = nullptr, size_t maxResponseBytes = 0) {
  using Stage = KOReaderSyncClient::FailureStage;
  auto& diagnostic = KOReaderSyncClient::lastDiagnostic;
  const uint32_t started = millis();
  const auto finish = [&](int code) {
    diagnostic.code = code;
    diagnostic.elapsedMs = millis() - started;
    if (code <= 0 && diagnostic.stage == Stage::NONE) {
      diagnostic.stage = WiFi.status() == WL_CONNECTED ? Stage::TRANSPORT : Stage::WIFI_DISCONNECTED;
    }
    if (code <= 0 || code >= 400) {
      LOG_ERR("KOSync", "%s failed: stage=%d code=%d elapsed=%lu ms wifi=%d rssi=%d", method,
              static_cast<int>(diagnostic.stage), code, static_cast<unsigned long>(diagnostic.elapsedMs),
              static_cast<int>(WiFi.status()), WiFi.RSSI());
    }
    return code;
  };
  if (WiFi.status() != WL_CONNECTED) {
    diagnostic.stage = Stage::WIFI_DISCONNECTED;
    return finish(-1);
  }
#if defined(CROSSPOINT_ENABLE_TAILSCALE)
  if (TAILSCALE.isMagicDnsUrl(url)) {
    return finish(sendMagicDnsRequest(method, url, payload, withAuth, jsonBody, responseBody, maxResponseBytes));
  }
  if (!TAILSCALE.prepareUrl(url)) {
    diagnostic.stage = Stage::TAILNET;
    return finish(-1);
  }
#endif

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    diagnostic.stage = Stage::INVALID_URL;
    return finish(-1);
  }
  http.addHeader("Accept", "application/vnd.koreader.v1+json");
  if (withAuth) applyAuthHeaders(http);
  if (jsonBody) http.addHeader("Content-Type", "application/json");
  int httpCode;
  if (responseBody && maxResponseBytes > 0) {
    responseBody->clear();
    responseBody->reserve(maxResponseBytes);
    httpCode = http.sendRequest(method, reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
                                [&](const uint8_t* bytes, size_t count) {
                                  if (count > maxResponseBytes - responseBody->size()) return false;
                                  responseBody->append(reinterpret_cast<const char*>(bytes), count);
                                  return true;
                                });
    if (!http.responseComplete()) {
      LOG_ERR("KOSync", "Incomplete or oversized extension response");
      if (httpCode > 0) diagnostic.stage = Stage::INCOMPLETE_RESPONSE;
      httpCode = -1;
    }
  } else {
    httpCode = strcmp(method, "GET") == 0 ? http.GET() : http.sendRequest(method, payload);
    if (responseBody && httpCode > 0) *responseBody = http.getString();
    if (httpCode > 0 && !http.responseComplete()) {
      diagnostic.stage = Stage::INCOMPLETE_RESPONSE;
      httpCode = -1;
    }
  }
  http.end();
  return finish(httpCode);
}
}  // namespace

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  lastHttpCode = 0;
  lastDiagnostic = {};
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/users/auth";
  LOG_DBG("KOSync", "Authenticating: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (requiresTls(url) && insufficientHeap()) return LOW_MEMORY;

  const int httpCode = sendSyncRequest("GET", url, {}, true, false);
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Auth response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;
  // Any 2xx is success. The reference kosync server answers 200, but
  // KOSync-compatible implementations differ (BookLore/grimmory is a Spring
  // service and uses the idiomatic codes) — see issue #2876.
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::createUser() {
  lastHttpCode = 0;
  lastDiagnostic = {};
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/users/create";
  LOG_DBG("KOSync", "Creating account: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (requiresTls(url) && insufficientHeap()) return LOW_MEMORY;

  JsonDocument doc;
  doc["username"] = KOREADER_STORE.getUsername();
  doc["password"] = KOREADER_STORE.getMd5Password();
  std::string body;
  serializeJson(doc, body);

  const int httpCode = sendSyncRequest("POST", url, body, false, true);
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Create user response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;
  if (httpCode >= 200 && httpCode < 300) return OK;  // 2xx: created (see #2876)
  if (httpCode == 402) return USER_EXISTS;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                          KOReaderProgress& outProgress) {
  lastHttpCode = 0;
  lastDiagnostic = {};
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress/" + documentHash;
  LOG_DBG("KOSync", "Getting progress: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (requiresTls(url) && insufficientHeap()) return LOW_MEMORY;

  std::string responseBody;
  const int httpCode = sendSyncRequest("GET", url, {}, true, false, &responseBody);
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Get progress response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;

  // 204 = success with no stored progress for this document (Spring-style
  // KOSync implementations; the reference server answers 200 with an empty
  // object instead). Map it to the same graceful no-remote-progress path as
  // 404 rather than falling through to SERVER_ERROR — see issue #2876.
  if (httpCode == 204) return NOT_FOUND;

  if (httpCode >= 200 && httpCode < 300) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, responseBody.c_str());

    if (error) {
      LOG_ERR("KOSync", "JSON parse failed: %s", error.c_str());
      return JSON_ERROR;
    }

    outProgress.document = documentHash;
    outProgress.progress = doc["progress"].as<std::string>();
    outProgress.percentage = doc["percentage"].as<float>();
    outProgress.device = doc["device"].as<std::string>();
    outProgress.deviceId = doc["device_id"].as<std::string>();
    outProgress.timestamp = doc["timestamp"].as<int64_t>();

    outProgress.position.reset();
    if (KOREADER_STORE.usesCrossPointSyncServer()) {
      const JsonObjectConst pos = doc["position"].as<JsonObjectConst>();
      if (!pos.isNull()) {
        KOReaderRichPosition rich;
        rich.pctQ = pos["pctQ"].as<uint32_t>();
        rich.spineIndex = pos["spine"].as<uint16_t>();
        rich.pageNumber = pos["page"].as<uint16_t>();
        const uint16_t pages = pos["pages"].as<uint16_t>();
        rich.totalPages = pages > 0 ? pages : 1;
        const uint16_t para = pos["para"].as<uint16_t>();
        if (para > 0) rich.paragraphIndex = para;
        rich.xpath = pos["xpath"].as<const char*>() ? pos["xpath"].as<const char*>() : "";
        LOG_DBG("KOSync", "Got rich position: spine=%u page=%u/%u para=%u", rich.spineIndex, rich.pageNumber,
                rich.totalPages, para);
        outProgress.position = std::move(rich);
      }
    }

    LOG_DBG("KOSync", "Got progress: %.2f%% at %s", outProgress.percentage * 100, outProgress.progress.c_str());
    return OK;
  }

  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode == 404) return NOT_FOUND;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  lastHttpCode = 0;
  lastDiagnostic = {};
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress";
  LOG_DBG("KOSync", "Updating progress: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (requiresTls(url) && insufficientHeap()) return LOW_MEMORY;

  // Build JSON body
  JsonDocument doc;
  doc["document"] = progress.document;
  if (progress.metadata.has_value()) {
    auto meta = doc["metadata"].to<JsonObject>();
    meta["filename"] = progress.metadata->filename;
    meta["title"] = progress.metadata->title;
    meta["authors"] = progress.metadata->authors;
  }
  doc["progress"] = progress.progress;
  doc["percentage"] = progress.percentage;
  doc["device"] = DEVICE_NAME;
  doc["device_id"] = DEVICE_ID;
#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
  if (isBookOrbitUrl(KOREADER_STORE.getBaseUrl())) {
    const char* identity = BookOrbitStatsClient::deviceId();
    if (!identity[0]) {
      LOG_ERR("KOSync", "Device identity unavailable");
      return SERVER_ERROR;
    }
    doc["device_id"] = identity;
  }
#endif
  if (progress.position.has_value() && KOREADER_STORE.usesCrossPointSyncServer()) {
    // CrossPoint-specific extension: do not send it to third-party KOSync servers.
    const auto& p = *progress.position;
    auto pos = doc["position"].to<JsonObject>();
    pos["pctQ"] = p.pctQ;
    pos["spine"] = p.spineIndex;
    pos["page"] = p.pageNumber;
    pos["pages"] = p.totalPages;
    if (p.paragraphIndex.has_value()) pos["para"] = *p.paragraphIndex;
    // Server rejects the whole position object if xpath exceeds 120 bytes.
    if (!p.xpath.empty() && p.xpath.size() <= 120) pos["xpath"] = p.xpath;
  }

  std::string body;
  serializeJson(doc, body);

  LOG_DBG("KOSync", "Request body: %s", body.c_str());

  const int httpCode = sendSyncRequest("PUT", url, body, true, true);
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Update progress response: %d", httpCode);

  if (httpCode <= 0) return NETWORK_ERROR;
  // Any 2xx accepts the progress. The reference kosync server answers 200,
  // but Spring-based KOSync implementations (BookLore/grimmory) answer a PUT
  // with the idiomatic 201/204, which used to land in SERVER_ERROR and made
  // every sync against them fail after a successful pull — issue #2876.
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
KOReaderSyncClient::Error KOReaderSyncClient::postExtension(const char* suffix, const std::string& payload,
                                                            std::string& response) {
  lastHttpCode = 0;
  lastDiagnostic = {};
  response.clear();
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_ERR("KOSync", "No extension credentials configured");
    return NO_CREDENTIALS;
  }
  const std::string baseUrl = KOREADER_STORE.getBaseUrl();
  if (!isBookOrbitUrl(baseUrl) || !suffix ||
      (strcmp(suffix, "/plugin/page-stats") != 0 && strcmp(suffix, "/plugin/sweeps") != 0)) {
    LOG_ERR("KOSync", "Unsupported statistics endpoint");
    return SERVER_ERROR;
  }
  const std::string url = baseUrl + suffix;
  if (requiresTls(url) && insufficientHeap()) return LOW_MEMORY;
  lastHttpCode = sendSyncRequest("POST", url, payload, true, true, &response, 2048);
  if (lastHttpCode <= 0) return NETWORK_ERROR;
  if (lastHttpCode >= 200 && lastHttpCode < 300) return OK;
  if (lastHttpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}
#endif

const char* KOReaderSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return "No credentials configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case SERVER_ERROR:
      return "Server error (try again later)";
    case JSON_ERROR:
      return "JSON parse error";
    case NOT_FOUND:
      return "No progress found";
    case LOW_MEMORY:
      return "Not enough memory for sync — please retry";
    default:
      return "Unknown error";
  }
}
