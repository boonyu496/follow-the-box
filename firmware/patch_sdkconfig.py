"""Deprecated PlatformIO hook.

FollowBox now targets ESP32-S3-DevKitC-1-N32R16V, which uses Octal flash
and Octal PSRAM. Do not disable OPI flash detection for this board.
This script is intentionally a no-op and is not referenced by platformio.ini.
"""

Import("env")


def warn_deprecated(*args, **kwargs):
    print("[patch_sdkconfig] Deprecated no-op: N32R16V requires OPI flash support.")


env.AddPreAction("buildboot", warn_deprecated)
