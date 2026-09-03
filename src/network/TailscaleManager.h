#pragma once

#include <cstdint>
#include <string>

class TailscaleManager {
 public:
  static TailscaleManager& getInstance();

  // Establishes the WireGuard session needed by a tailnet IP or .ts.net URL.
  // Other URLs are left on the normal Wi-Fi transport.
  bool prepareUrl(const std::string& url);
  bool isMagicDnsUrl(const std::string& url) const;
#if defined(CROSSPOINT_ENABLE_TAILSCALE)
  bool isMagicDnsHost(const char* host) const;
  bool prepareHost(const char* host, uint16_t port, uint32_t& peerIp);
#endif
  void shutdown();

 private:
  TailscaleManager() = default;
  ~TailscaleManager() = default;
  TailscaleManager(const TailscaleManager&) = delete;
  TailscaleManager& operator=(const TailscaleManager&) = delete;

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
  struct microlink_s* client = nullptr;
  uint32_t priorityPeer = 0;
  bool start(uint32_t peerIp);
#endif
};

#define TAILSCALE TailscaleManager::getInstance()
