#pragma once

#include <cstdint>

namespace followbox {

// Persisted runtime profile (NVS-backed). Holds the durable result of the
// install wizard - the gate the safety manager checks before allowing
// AUTO_FOLLOW. Defaults are deliberately the *safe* values so a blank or
// corrupt NVS region behaves as "wizard not done" and keeps autonomous motion
// locked out.
struct RuntimeProfile {
  bool install_wizard_complete = false;
};

// NVS persistence for RuntimeProfile.
//
// Per FIRMWARE-SPEC the storage layer must never write Flash at high frequency:
// load() runs once at boot, and save() only commits when a field actually
// changed (dirty check), so normal operation performs zero Flash writes. This
// class is pure persistence - it owns no motion, safety or GPIO logic; callers
// apply the loaded values to SystemState themselves.
class ProfileStore {
 public:
  // Open the NVS namespace. Returns false if NVS is unavailable (callers then
  // fall back to the safe defaults already in RuntimeProfile).
  bool begin();

  // Read the stored profile, or safe defaults when nothing/older schema is
  // stored. Never throws; a malformed record yields defaults.
  RuntimeProfile load();

  // Persist `profile`, writing only the keys whose value changed since the last
  // load/save. Returns true if the commit succeeded (or nothing needed writing).
  bool save(const RuntimeProfile& profile);

 private:
  bool opened_ = false;
  // Mirror of what is believed to be on Flash, to skip redundant writes.
  RuntimeProfile cached_;
  bool cache_valid_ = false;
};

}  // namespace followbox
