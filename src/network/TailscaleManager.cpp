#include "TailscaleManager.h"

#include <Arduino.h>
#include <Logging.h>

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
#include <WireGuardLwIP.h>
#include <microlink.h>
#include <wireguardif.h>

#ifndef CROSSPOINT_TAILSCALE_AUTH_KEY
#define CROSSPOINT_TAILSCALE_AUTH_KEY ""
#endif
#endif

namespace {
constexpr uint32_t TAILNET_MASK = 0xFFC00000U;
constexpr uint32_t TAILNET_PREFIX = 0x64400000U;
constexpr uint32_t CONNECT_TIMEOUT_MS = 45000;

struct UrlPeer {
  uint32_t ip = 0;
  uint16_t port = 0;
};

UrlPeer parseTailnetPeer(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return {};

  const bool https = url.compare(0, schemeEnd, "https") == 0;
  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  const size_t authorityEnd = pathStart == std::string::npos ? url.size() : pathStart;
  const size_t colon = url.find(':', hostStart);
  const size_t hostEnd = colon != std::string::npos && colon < authorityEnd ? colon : authorityEnd;
  if (hostEnd <= hostStart || hostEnd - hostStart >= 16) return {};

  char host[16] = {};
  memcpy(host, url.data() + hostStart, hostEnd - hostStart);

  uint32_t ip = 0;
  unsigned int octets[4] = {};
  char trailing = '\0';
  if (sscanf(host, "%u.%u.%u.%u%c", &octets[0], &octets[1], &octets[2], &octets[3], &trailing) != 4) return {};
  for (const unsigned int octet : octets) {
    if (octet > 255) return {};
    ip = (ip << 8) | octet;
  }
  if ((ip & TAILNET_MASK) != TAILNET_PREFIX) return {};

  unsigned long port = https ? 443 : 80;
  if (hostEnd < authorityEnd) {
    char* end = nullptr;
    port = strtoul(url.c_str() + hostEnd + 1, &end, 10);
    if (end != url.c_str() + authorityEnd || port == 0 || port > UINT16_MAX) return {};
  }
  return {ip, static_cast<uint16_t>(port)};
}
}  // namespace

TailscaleManager& TailscaleManager::getInstance() {
  static TailscaleManager instance;
  return instance;
}

bool TailscaleManager::prepareUrl(const std::string& url) {
  const UrlPeer peer = parseTailnetPeer(url);
  if (peer.ip == 0) return true;

#if !defined(CROSSPOINT_ENABLE_TAILSCALE)
  return true;
#else
  if (!start(peer.ip)) return false;

  auto* probe = microlink_tcp_connect(client, peer.ip, peer.port, CONNECT_TIMEOUT_MS);
  if (!probe) {
    LOG_ERR("TAIL", "Could not establish peer tunnel");
    return false;
  }
  microlink_tcp_close(probe);
  return true;
#endif
}

void TailscaleManager::shutdown() {
#if defined(CROSSPOINT_ENABLE_TAILSCALE)
  if (!client) return;
  microlink_stop(client);
  microlink_destroy(client);
  client = nullptr;
  priorityPeer = 0;
#endif
}

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
bool TailscaleManager::start(uint32_t peerIp) {
  if (client && priorityPeer == peerIp && microlink_is_connected(client)) return true;
  if (client) {
    shutdown();
  }
  const microlink_config_t config = {
      .auth_key = CROSSPOINT_TAILSCALE_AUTH_KEY,
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
  if (CROSSPOINT_TAILSCALE_AUTH_KEY[0] == '\0') {
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
  LOG_INF("TAIL", "Connected as %s", microlink_default_device_name());
  return true;
}
#endif
