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
  static constexpr size_t AUTH_KEY_CAPACITY = 96;
  static constexpr char AUTH_KEY_PATH[] = "/.crosspoint/tailscale-auth.key";

  enum class AuthKeyStatus : uint8_t { Missing, Loaded, Invalid };

  struct microlink_s* client = nullptr;
  uint32_t priorityPeer = 0;
  char authKey[AUTH_KEY_CAPACITY] = {};
  bool authKeyFromStorage = false;

  AuthKeyStatus loadAuthKey();
  void consumeAuthKey();
  bool start(uint32_t peerIp);
#endif
};

#define TAILSCALE TailscaleManager::getInstance()
