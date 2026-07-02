#pragma once

#include <cstddef>

namespace followbox {

// Persisted STA WiFi credentials, written by the H5 provisioning endpoint
// (POST /api/wifi) and read once at boot. Empty ssid means "not provisioned":
// the box then runs AP-only and the cloud link stays down. Defaults are safe:
// a blank/corrupt NVS region simply yields no STA attempt.
struct WifiCredentials {
  // 802.11 limits: SSID <= 32 bytes, WPA passphrase <= 63 chars.
  char ssid[33] = {0};
  char password[65] = {0};

  bool valid() const { return ssid[0] != '\0'; }
};

// NVS persistence for WifiCredentials. Same contract as ProfileStore:
// load() once at boot, save() only on an explicit provisioning request, no
// high-frequency Flash writes, no motion/safety logic.
//
// The box also persists the user's chosen network mode here:
//   - link disabled (default): pure hotspot (AP-only), rock-solid local
//     control with zero AP+STA radio contention.
//   - link enabled: hotspot + join the user's WiFi (AP+STA) for the cloud /
//     remote-control link, chosen explicitly from the H5 panel.
class WifiStore {
 public:
  bool begin();
  WifiCredentials load();
  bool save(const WifiCredentials& creds);
  bool clear();

  // Persisted network mode. false = hotspot-only (default), true = link mode
  // (AP+STA). Load once at boot; save only on an explicit H5 mode switch.
  bool loadLinkEnabled();
  bool saveLinkEnabled(bool enabled);

 private:
  bool opened_ = false;
};

}  // namespace followbox
