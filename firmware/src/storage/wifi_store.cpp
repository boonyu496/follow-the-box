#include "storage/wifi_store.h"

#include <Preferences.h>

#include <cstring>

#include "telemetry/debug_console.h"

namespace followbox {
namespace {

constexpr char kNamespace[] = "fb_wifi";
constexpr char kKeySchema[] = "schema";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPass[] = "pass";
constexpr uint16_t kSchemaVersion = 1;

}  // namespace

bool WifiStore::begin() {
  Preferences prefs;
  opened_ = prefs.begin(kNamespace, /*readOnly=*/false);
  if (opened_) {
    prefs.end();
  } else {
    FB_LOGW("wifi_store: NVS open failed, STA provisioning unavailable");
  }
  return opened_;
}

WifiCredentials WifiStore::load() {
  WifiCredentials creds;  // empty = not provisioned

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
    return creds;
  }
  const uint16_t schema = prefs.getUShort(kKeySchema, 0);
  if (schema == kSchemaVersion) {
    prefs.getString(kKeySsid, creds.ssid, sizeof(creds.ssid));
    prefs.getString(kKeyPass, creds.password, sizeof(creds.password));
  } else if (schema != 0) {
    FB_LOGW("wifi_store: schema %u != %u, ignoring stored credentials", schema,
            kSchemaVersion);
  }
  prefs.end();

  if (creds.valid()) {
    FB_LOGI("wifi_store: loaded ssid=%s", creds.ssid);
  } else {
    FB_LOGI("wifi_store: no stored WiFi, AP-only until provisioned");
  }
  return creds;
}

bool WifiStore::save(const WifiCredentials& creds) {
  if (!creds.valid()) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    FB_LOGE("wifi_store: save open failed");
    return false;
  }
  prefs.putUShort(kKeySchema, kSchemaVersion);
  prefs.putString(kKeySsid, creds.ssid);
  prefs.putString(kKeyPass, creds.password);
  prefs.end();
  FB_LOGI("wifi_store: saved ssid=%s", creds.ssid);
  return true;
}

bool WifiStore::clear() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    return false;
  }
  prefs.clear();
  prefs.end();
  FB_LOGI("wifi_store: cleared");
  return true;
}

}  // namespace followbox
