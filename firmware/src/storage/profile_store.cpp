#include "storage/profile_store.h"

#include <Preferences.h>

#include "telemetry/debug_console.h"

namespace followbox {
namespace {

// NVS namespace + keys. Bump kSchemaVersion whenever the record layout changes
// so an old image is read as defaults instead of being misinterpreted.
constexpr char kNamespace[] = "fb_profile";
constexpr char kKeySchema[] = "schema";
constexpr char kKeyWizard[] = "wizard";
constexpr uint16_t kSchemaVersion = 1;

}  // namespace

bool ProfileStore::begin() {
  // Probe the namespace once so load()/save() can open it read-only or
  // read-write as needed; Preferences is reopened per operation to keep the
  // handle lifetime short.
  Preferences prefs;
  opened_ = prefs.begin(kNamespace, /*readOnly=*/false);
  if (opened_) {
    prefs.end();
  } else {
    FB_LOGW("profile_store: NVS open failed, using defaults");
  }
  return opened_;
}

RuntimeProfile ProfileStore::load() {
  RuntimeProfile profile;  // safe defaults

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
    cached_ = profile;
    cache_valid_ = true;
    return profile;
  }

  const uint16_t schema = prefs.getUShort(kKeySchema, 0);
  if (schema == kSchemaVersion) {
    profile.install_wizard_complete = prefs.getBool(kKeyWizard, false);
  } else if (schema != 0) {
    FB_LOGW("profile_store: schema %u != %u, using defaults", schema,
            kSchemaVersion);
  }
  prefs.end();

  cached_ = profile;
  cache_valid_ = true;
  FB_LOGI("profile_store: loaded wizard=%d",
          profile.install_wizard_complete ? 1 : 0);
  return profile;
}

bool ProfileStore::save(const RuntimeProfile& profile) {
  // Skip the Flash write entirely when nothing changed (avoids wear).
  if (cache_valid_ &&
      cached_.install_wizard_complete == profile.install_wizard_complete) {
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    FB_LOGE("profile_store: save open failed");
    return false;
  }
  prefs.putUShort(kKeySchema, kSchemaVersion);
  prefs.putBool(kKeyWizard, profile.install_wizard_complete);
  prefs.end();

  cached_ = profile;
  cache_valid_ = true;
  FB_LOGI("profile_store: saved wizard=%d",
          profile.install_wizard_complete ? 1 : 0);
  return true;
}

}  // namespace followbox
