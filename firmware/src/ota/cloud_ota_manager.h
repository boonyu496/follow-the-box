#pragma once

#include <cstddef>
#include <cstdint>

namespace followbox {

class CloudOtaManager {
 public:
  using SafetyCallback = void (*)(bool active);

  void begin(SafetyCallback safety_callback);
  void update(uint32_t now_ms);
  bool inProgress() const { return in_progress_; }

 private:
  struct Manifest {
    bool valid = false;
    bool force = false;
    char version[40] = {0};
    char url[192] = {0};
    char md5[40] = {0};
    int size = 0;
  };

  bool configured() const;
  bool fetchManifest(Manifest& manifest);
  bool performUpdate(const Manifest& manifest, char* reason, size_t reason_size);
  void reportResult(const char* version, bool ok, const char* reason);
  void setInProgress(bool active);

  SafetyCallback safety_callback_ = nullptr;
  uint32_t last_check_ms_ = 0;
  bool in_progress_ = false;
  char last_attempt_version_[40] = {0};
};

}  // namespace followbox
