#pragma once

#include <cstdint>
#include <string>

class TailscaleManager {
 public:
  static TailscaleManager& getInstance();

  // Establishes the WireGuard session needed by a 100.64.0.0/10 URL.
  // Other URLs are left on the normal Wi-Fi transport.
  bool prepareUrl(const std::string& url);
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
