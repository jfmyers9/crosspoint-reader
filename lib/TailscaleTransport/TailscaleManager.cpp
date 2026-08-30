#include "TailscaleManager.h"

#include <Arduino.h>
#include <Logging.h>

#include <cctype>
#include <cstring>

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
#include <HalStorage.h>
#include <WireGuardLwIP.h>
#include <microlink.h>
#include <wireguardif.h>
#endif

namespace {
constexpr uint32_t TAILNET_MASK = 0xFFC00000U;
constexpr uint32_t TAILNET_PREFIX = 0x64400000U;
constexpr uint32_t CONNECT_TIMEOUT_MS = 45000;
constexpr uint32_t PEER_CONNECT_TIMEOUT_MS = 15000;
constexpr size_t HOST_CAPACITY = 64;

struct UrlPeer {
  char host[HOST_CAPACITY] = {};
  uint32_t ip = 0;
  uint16_t port = 0;
  bool magicDns = false;
};

bool hasMagicDnsSuffix(const char* host) {
  if (!host) return false;
  constexpr char SUFFIX[] = ".ts.net";
  const size_t hostLen = strlen(host);
  constexpr size_t SUFFIX_LEN = sizeof(SUFFIX) - 1;
  if (hostLen <= SUFFIX_LEN) return false;
  for (size_t i = 0; i < SUFFIX_LEN; ++i) {
    const unsigned char c = static_cast<unsigned char>(host[hostLen - SUFFIX_LEN + i]);
    if (tolower(c) != SUFFIX[i]) return false;
  }
  return true;
}

uint32_t parseTailnetIp(const char* host) {
  uint32_t ip = 0;
  unsigned int octets[4] = {};
  char trailing = '\0';
  if (sscanf(host, "%u.%u.%u.%u%c", &octets[0], &octets[1], &octets[2], &octets[3], &trailing) != 4) return 0;
  for (const unsigned int octet : octets) {
    if (octet > 255) return 0;
    ip = (ip << 8) | octet;
  }
  return (ip & TAILNET_MASK) == TAILNET_PREFIX ? ip : 0;
}

UrlPeer parseTailnetPeer(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return {};

  const bool https = url.compare(0, schemeEnd, "https") == 0;
  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  const size_t authorityEnd = pathStart == std::string::npos ? url.size() : pathStart;
  const size_t colon = url.find(':', hostStart);
  const size_t hostEnd = colon != std::string::npos && colon < authorityEnd ? colon : authorityEnd;
  if (hostEnd <= hostStart || hostEnd - hostStart >= HOST_CAPACITY) return {};

  UrlPeer peer;
  memcpy(peer.host, url.data() + hostStart, hostEnd - hostStart);
  peer.ip = parseTailnetIp(peer.host);
  peer.magicDns = hasMagicDnsSuffix(peer.host);
  if (peer.ip == 0 && !peer.magicDns) return {};

  unsigned long port = https ? 443 : 80;
  if (hostEnd < authorityEnd) {
    char* end = nullptr;
    port = strtoul(url.c_str() + hostEnd + 1, &end, 10);
    if (end != url.c_str() + authorityEnd || port == 0 || port > UINT16_MAX) return {};
  }
  peer.port = static_cast<uint16_t>(port);
  return peer;
}
}  // namespace

TailscaleManager& TailscaleManager::getInstance() {
  static TailscaleManager instance;
  return instance;
}

bool TailscaleManager::prepareUrl(const std::string& url) {
  const UrlPeer peer = parseTailnetPeer(url);
  if (peer.ip == 0 && !peer.magicDns) return true;

#if !defined(CROSSPOINT_ENABLE_TAILSCALE)
  return true;
#else
  uint32_t resolvedIp = 0;
  return prepareHost(peer.host, peer.port, resolvedIp);
#endif
}

bool TailscaleManager::isMagicDnsUrl(const std::string& url) const { return parseTailnetPeer(url).magicDns; }

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
bool TailscaleManager::isMagicDnsHost(const char* host) const { return hasMagicDnsSuffix(host); }

bool TailscaleManager::prepareHost(const char* host, uint16_t port, uint32_t& peerIp) {
  peerIp = parseTailnetIp(host);
  const bool magicDns = hasMagicDnsSuffix(host);
  if (peerIp == 0 && !magicDns) return false;
  if (!start(peerIp)) return false;

  if (magicDns) {
    peerIp = microlink_resolve(client, host);
    if (peerIp == 0) {
      LOG_ERR("TAIL", "Could not resolve tailnet host: %s", host);
      return false;
    }
    char peerIpText[16] = {};
    microlink_ip_to_str(peerIp, peerIpText);
    LOG_INF("TAIL", "Resolved %s to %s", host, peerIpText);
  }

  if (microlink_prepare_peer(client, peerIp, PEER_CONNECT_TIMEOUT_MS) != ESP_OK) {
    LOG_ERR("TAIL", "Could not establish peer tunnel");
    return false;
  }
  return true;
}
#endif

void TailscaleManager::shutdown() {
#if defined(CROSSPOINT_ENABLE_TAILSCALE)
  if (client) {
    microlink_destroy(client);
    client = nullptr;
  }
  priorityPeer = 0;
  memset(authKey, 0, sizeof(authKey));
  authKeyFromStorage = false;
#endif
}

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
TailscaleManager::AuthKeyStatus TailscaleManager::loadAuthKey() {
  memset(authKey, 0, sizeof(authKey));
  authKeyFromStorage = false;
  if (!Storage.exists(AUTH_KEY_PATH)) return AuthKeyStatus::Missing;

  HalFile file;
  if (!Storage.openFileForRead("TAIL", AUTH_KEY_PATH, file)) return AuthKeyStatus::Invalid;
  const size_t fileSize = file.fileSize();
  if (fileSize == 0) return AuthKeyStatus::Missing;
  if (fileSize >= sizeof(authKey)) {
    LOG_ERR("TAIL", "Invalid enrollment key file length");
    return AuthKeyStatus::Invalid;
  }
  const int bytesRead = file.read(authKey, fileSize);
  if (bytesRead != static_cast<int>(fileSize)) {
    LOG_ERR("TAIL", "Could not read complete enrollment key file");
    memset(authKey, 0, sizeof(authKey));
    return AuthKeyStatus::Invalid;
  }

  size_t first = 0;
  size_t last = fileSize;
  while (first < last && isspace(static_cast<unsigned char>(authKey[first]))) ++first;
  while (last > first && isspace(static_cast<unsigned char>(authKey[last - 1]))) --last;
  const size_t keyLength = last - first;
  if (first > 0 && keyLength > 0) memmove(authKey, authKey + first, keyLength);
  authKey[keyLength] = '\0';

  constexpr char AUTH_KEY_PREFIX[] = "tskey-auth-";
  if (keyLength <= sizeof(AUTH_KEY_PREFIX) - 1 || strncmp(authKey, AUTH_KEY_PREFIX, sizeof(AUTH_KEY_PREFIX) - 1) != 0) {
    LOG_ERR("TAIL", "Enrollment key file does not contain a Tailscale auth key");
    memset(authKey, 0, sizeof(authKey));
    return AuthKeyStatus::Invalid;
  }
  for (size_t i = 0; i < keyLength; ++i) {
    if (isspace(static_cast<unsigned char>(authKey[i]))) {
      LOG_ERR("TAIL", "Enrollment key contains whitespace");
      memset(authKey, 0, sizeof(authKey));
      return AuthKeyStatus::Invalid;
    }
  }

  authKeyFromStorage = true;
  LOG_INF("TAIL", "Loaded one-time enrollment key from SD");
  return AuthKeyStatus::Loaded;
}

void TailscaleManager::consumeAuthKey() {
  if (!authKeyFromStorage) return;
  if (Storage.remove(AUTH_KEY_PATH)) {
    LOG_INF("TAIL", "Removed enrollment key from SD after connecting");
  } else {
    HalFile file;
    if (Storage.openFileForWrite("TAIL", AUTH_KEY_PATH, file)) {
      LOG_ERR("TAIL", "Could not remove enrollment key file; emptied it instead");
    } else {
      LOG_ERR("TAIL", "Could not consume enrollment key; revoke it manually");
    }
  }
  authKeyFromStorage = false;
}

bool TailscaleManager::start(uint32_t peerIp) {
  if (client && priorityPeer == peerIp && microlink_is_connected(client)) return true;
  if (client) {
    shutdown();
  }
  const AuthKeyStatus authKeyStatus = loadAuthKey();
  if (authKeyStatus == AuthKeyStatus::Invalid) return false;

  const uint8_t connectionCount = authKeyStatus == AuthKeyStatus::Loaded ? 2 : 1;
  for (uint8_t connection = 0; connection < connectionCount; ++connection) {
    const microlink_config_t config = {
        .auth_key = authKey,
        .device_name = "crosspoint-x4pro",
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = true,
        .max_peers = CONFIG_ML_MAX_PEERS,
        .wifi_tx_power_dbm = 0,
        .priority_peer_ip = peerIp,
        .disco_heartbeat_ms = 0,
        .stun_interval_ms = 0,
        .ctrl_watchdog_ms = 0,
    };
    if (authKey[0] == '\0') {
      LOG_INF("TAIL", "Starting with stored node identity (no enrollment key)");
    }
    client = microlink_init(&config);
    if (!client || microlink_start(client) != ESP_OK) {
      LOG_ERR("TAIL", "Could not start MicroLink");
      shutdown();
      return false;
    }
    priorityPeer = peerIp;

    const unsigned long startedAt = millis();
    while (!microlink_is_connected(client) && millis() - startedAt < CONNECT_TIMEOUT_MS) {
      if (microlink_get_state(client) == ML_STATE_ERROR) break;
      delay(250);
    }
    if (!microlink_is_connected(client)) {
      LOG_ERR("TAIL", "Timed out connecting to tailnet");
      shutdown();
      return false;
    }

    if (connectionCount == 1 || connection == 1) {
      LOG_INF("TAIL", "Connected as %s", microlink_default_device_name());
      return true;
    }
    LOG_INF("TAIL", "Enrollment succeeded; reconnecting with stored node identity");
    consumeAuthKey();
    shutdown();
  }
  return false;
}
#endif
