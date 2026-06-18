#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
FIRMWARE_DIR="${REPO_ROOT}/firmware"
VISION_CAM_DIR="${REPO_ROOT}/vision_cam"
LOCAL_BIN_DIR="${HOME}/.local/bin"

usage() {
  cat <<'EOF'
Usage:
  ./tools/followbox-wsl-toolchain.sh install [--no-build] [--skip-vision]
  ./tools/followbox-wsl-toolchain.sh env <firmware|vision_cam>
  ./tools/followbox-wsl-toolchain.sh run <firmware|vision_cam> [pio args...]
  ./tools/followbox-wsl-toolchain.sh smoke

Examples:
  ./tools/followbox-wsl-toolchain.sh install
  ./tools/followbox-wsl-toolchain.sh run firmware run -e esp32-s3-devkitc-1
  ./tools/followbox-wsl-toolchain.sh run firmware run -e esp32-s3-devkitc-1 -t uploadfs
  ./tools/followbox-wsl-toolchain.sh run vision_cam run -t upload

Notes:
  - WSL uses project-local PlatformIO homes named .pio-core-wsl to avoid
    mixing Linux toolchains with Windows .pio-core caches.
  - USB flashing from Linux normally requires WSL2 plus usbipd-win attachment.
EOF
}

log() {
  printf '[followbox-wsl] %s\n' "$*"
}

fail() {
  printf '[followbox-wsl] ERROR: %s\n' "$*" >&2
  exit 1
}

require_dir() {
  local path="$1"
  [[ -d "$path" ]] || fail "Missing directory: $path"
}

project_dir_for() {
  case "${1:-}" in
    firmware) printf '%s\n' "$FIRMWARE_DIR" ;;
    vision_cam) printf '%s\n' "$VISION_CAM_DIR" ;;
    *) fail "Unknown project: ${1:-}" ;;
  esac
}

platformio_home_for() {
  local project_dir="$1"
  printf '%s/.pio-core-wsl\n' "$project_dir"
}

sudo_cmd() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

ensure_local_bin_on_path() {
  export PATH="${LOCAL_BIN_DIR}:${PATH}"
}

ensure_platformio() {
  ensure_local_bin_on_path

  if ! command -v pipx >/dev/null 2>&1; then
    log "Installing pipx into ${LOCAL_BIN_DIR}"
    python3 -m pip install --user pipx
  fi

  python3 -m pipx ensurepath >/dev/null 2>&1 || true
  ensure_local_bin_on_path

  if ! command -v pio >/dev/null 2>&1; then
    log "Installing PlatformIO Core with pipx"
    pipx install platformio
  else
    log "Upgrading PlatformIO Core"
    pipx upgrade platformio || true
  fi
}

install_packages() {
  command -v apt-get >/dev/null 2>&1 || fail "This installer expects Ubuntu/Debian with apt-get."

  log "Installing Ubuntu packages required for PlatformIO and local smoke builds"
  sudo_cmd apt-get update
  sudo_cmd apt-get install -y \
    build-essential \
    ca-certificates \
    curl \
    git \
    libusb-1.0-0 \
    libusb-1.0-0-dev \
    pkg-config \
    pipx \
    python3 \
    python3-pip \
    python3-venv \
    unzip \
    usbutils \
    xz-utils
}

print_wsl_note() {
  if grep -qi 'wsl2' /proc/version 2>/dev/null; then
    log "WSL2 detected. USB flashing can work after usbipd-win attach on Windows."
  else
    log "WSL2 was not detected. Build-only workflows will work, but USB flashing from Linux usually needs WSL2."
  fi
}

prepare_project_home() {
  local project_dir="$1"
  local home_dir
  home_dir="$(platformio_home_for "$project_dir")"
  mkdir -p "$home_dir"
}

run_pio_in_project() {
  local project_name="$1"
  shift

  local project_dir
  local home_dir
  project_dir="$(project_dir_for "$project_name")"
  require_dir "$project_dir"
  home_dir="$(platformio_home_for "$project_dir")"
  prepare_project_home "$project_dir"
  ensure_platformio

  PLATFORMIO_CORE_DIR="$home_dir" \
  PLATFORMIO_HOME_DIR="$home_dir" \
    pio -d "$project_dir" "$@"
}

print_env() {
  local project_dir
  local home_dir
  project_dir="$(project_dir_for "$1")"
  home_dir="$(platformio_home_for "$project_dir")"

  cat <<EOF
export PATH="${LOCAL_BIN_DIR}:\$PATH"
export PLATFORMIO_CORE_DIR="${home_dir}"
export PLATFORMIO_HOME_DIR="${home_dir}"
cd "${project_dir}"
EOF
}

run_smoke() {
  run_pio_in_project firmware run -e esp32-s3-devkitc-1
  run_pio_in_project firmware run -e esp32-s3-devkitc-1 -t buildfs
  run_pio_in_project vision_cam run
}

install_toolchain() {
  local build_after_install=1
  local include_vision=1

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --no-build)
        build_after_install=0
        ;;
      --skip-vision)
        include_vision=0
        ;;
      *)
        fail "Unknown install option: $1"
        ;;
    esac
    shift
  done

  require_dir "$FIRMWARE_DIR"
  install_packages
  ensure_platformio
  prepare_project_home "$FIRMWARE_DIR"
  if [[ "$include_vision" -eq 1 ]]; then
    prepare_project_home "$VISION_CAM_DIR"
  fi

  print_wsl_note
  log "PlatformIO will use ${FIRMWARE_DIR}/.pio-core-wsl for firmware"
  if [[ "$include_vision" -eq 1 ]]; then
    log "PlatformIO will use ${VISION_CAM_DIR}/.pio-core-wsl for vision_cam"
  fi

  if [[ "$build_after_install" -eq 1 ]]; then
    log "Warming firmware toolchain"
    run_pio_in_project firmware run -e esp32-s3-devkitc-1
    run_pio_in_project firmware run -e esp32-s3-devkitc-1 -t buildfs

    if [[ "$include_vision" -eq 1 ]]; then
      log "Warming vision_cam toolchain"
      run_pio_in_project vision_cam run
    fi
  else
    log "Skipping builds. Use the 'run' or 'smoke' subcommands later."
  fi

  cat <<EOF

Ready.

Next commands:
  source <(${SCRIPT_DIR}/followbox-wsl-toolchain.sh env firmware)
  pio run -e esp32-s3-devkitc-1
  pio run -e esp32-s3-devkitc-1 -t buildfs

USB flashing from WSL2 usually needs a Windows-side attach first, for example:
  usbipd list
  usbipd bind --busid <BUSID>
  usbipd attach --wsl --busid <BUSID>
EOF
}

main() {
  local command="${1:-}"
  [[ -n "$command" ]] || {
    usage
    exit 1
  }
  shift || true

  case "$command" in
    install)
      install_toolchain "$@"
      ;;
    env)
      [[ $# -eq 1 ]] || fail "env expects exactly one project name."
      print_env "$1"
      ;;
    run)
      [[ $# -ge 2 ]] || fail "run expects a project name followed by pio arguments."
      local project="$1"
      shift
      run_pio_in_project "$project" "$@"
      ;;
    smoke)
      [[ $# -eq 0 ]] || fail "smoke takes no arguments."
      run_smoke
      ;;
    -h|--help|help)
      usage
      ;;
    *)
      fail "Unknown command: $command"
      ;;
  esac
}

main "$@"
