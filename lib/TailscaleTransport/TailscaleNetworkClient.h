#pragma once

#include <NetworkClient.h>

#include "TailscaleManager.h"

// Resolves fully qualified MagicDNS hosts through MicroLink, then connects to
// the resulting tailnet address. HTTP clients still retain the original host
// for their Host header.
class TailscaleNetworkClient final : public NetworkClient {
 public:
  using NetworkClient::connect;

  int connect(const char* host, uint16_t port) override { return connect(host, port, 15000); }

  int connect(const char* host, uint16_t port, int32_t timeoutMs) override {
    if (!TAILSCALE.isMagicDnsHost(host)) return NetworkClient::connect(host, port, timeoutMs);

    uint32_t peerIp = 0;
    if (!TAILSCALE.prepareHost(host, port, peerIp)) return 0;
    const IPAddress address((peerIp >> 24) & 0xFF, (peerIp >> 16) & 0xFF, (peerIp >> 8) & 0xFF, peerIp & 0xFF);
    return NetworkClient::connect(address, port, timeoutMs);
  }
};
