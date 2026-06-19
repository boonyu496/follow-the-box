#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace followbox {

class CloudOtaManager {
 public:
  using SafetyCallback = void (*)(bool active);

  enum class State : uint8_t {
    kIdle,
    kChecking,
    kUpdateAvailable,
    kInstalling,
    kRebooting,
    kFailed,
  };

  struct Status {
    State state = State::kIdle;
    bool configured = false;
    bool update_available = false;
    uint32_t checked_at_ms = 0;
    char current_version[40] = {0};
    char available_version[40] = {0};
    char reason[80] = {0};
  };

  void begin(SafetyCallback safety_callback);
  void update(uint32_t now_ms);
  bool inProgress() const { return in_progress_.load(); }
  bool requestCheck();
  bool requestInstall(const char* version);
  Status status() const;
  static const char* stateName(State state);

 private:
  struct Manifest {
    bool valid = false;
    bool update_available = false;
    bool install_requested = false;
    char version[40] = {0};
    char url[192] = {0};
    char md5[40] = {0};
    char request_id[48] = {0};
    int size = 0;
  };

  bool configured() const;
  bool fetchRequest(Manifest& manifest);
  bool performUpdate(const Manifest& manifest, char* reason, size_t reason_size);
  void reportResult(const char* request_id, const char* version, bool ok,
                    const char* reason);
  void setInProgress(bool active);
  void setStatus(State state, bool update_available, const char* version,
                 const char* reason, uint32_t checked_at_ms);

  SafetyCallback safety_callback_ = nullptr;
  uint32_t last_check_ms_ = 0;
  std::atomic<bool> in_progress_{false};
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_;
  bool check_requested_ = false;
  bool local_install_requested_ = false;
  char local_install_version_[40] = {0};
  char last_request_id_[48] = {0};
};

}  // namespace followbox
