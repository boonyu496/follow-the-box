# Quick, safe reader for FollowBox /api/ota/status + /api/logs.
#
# Use it (a) right after a flash to confirm the running version and boot
# reset_reason, and (b) right after a PHONE reproduces the hotspot drop -- the
# boot breadcrumb "boot: fw=... reset_reason=..." in the log ring then tells us
# whether the device rebooted (BROWNOUT / TASK_WDT / PANIC) or the phone simply
# left a still-running AP.
#
# SAFETY: PC internet stays on ETHERNET; WLAN only joins FollowBox
# (192.168.4.x). A scheduled task hard-disconnects WLAN after -SafetyNetSeconds.
param(
  [string]$InterfaceName = "WLAN",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FbHost = "192.168.4.1",
  [int]$SafetyNetSeconds = 60
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "followbox-logs-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$taskName = "FollowBoxLogReadSafety_$runId"

function Log { param([string]$m) Write-Host ("[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }

$runAt = (Get-Date).AddSeconds($SafetyNetSeconds)
try {
  schtasks /Create /F /TN $taskName /SC ONCE /ST $runAt.ToString("HH:mm:ss") `
    /TR "powershell -NoProfile -WindowStyle Hidden -Command netsh wlan disconnect interface=`"$InterfaceName`"" | Out-Null
  Log "safety-net: WLAN auto-disconnect at $($runAt.ToString('HH:mm:ss')) (+${SafetyNetSeconds}s)"
} catch { Log "safety-net FAILED: $($_.Exception.Message)" }

$profilePath = Join-Path $runDir "fb-profile.xml"
@"
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$FollowBoxSsid</name>
  <SSIDConfig><SSID><name>$FollowBoxSsid</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
  <MSM><security>
    <authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>
    <sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>$FollowBoxPassword</keyMaterial></sharedKey>
  </security></MSM>
</WLANProfile>
"@ | Set-Content -LiteralPath $profilePath -Encoding ASCII
netsh wlan add profile filename="$profilePath" user=current | Out-Null
Remove-Item -LiteralPath $profilePath -Force -ErrorAction SilentlyContinue
Log "connecting WLAN to $FollowBoxSsid"
netsh wlan connect name="$FollowBoxSsid" interface="$InterfaceName" | Out-Null
$srcIp = ""
for ($i=0; $i -lt 25; $i++) {
  Start-Sleep -Milliseconds 800
  try { $srcIp = (Get-NetIPAddress -InterfaceAlias $InterfaceName -AddressFamily IPv4 -ErrorAction Stop | Where-Object { $_.IPAddress -like "192.168.4.*" } | Select-Object -First 1 -ExpandProperty IPAddress) } catch { $srcIp = "" }
  if ($srcIp) { break }
}
Log "followbox src ip='$srcIp'"

$ota = & curl.exe -sS --max-time 5 "http://$FbHost/api/ota/status" 2>$null | Out-String
$wifi = & curl.exe -sS --max-time 5 "http://$FbHost/api/wifi/status" 2>$null | Out-String
$logs = & curl.exe -sS --max-time 6 "http://$FbHost/api/logs" 2>$null | Out-String
$ota.Trim()  | Set-Content -LiteralPath (Join-Path $runDir "ota.json")  -Encoding UTF8
$wifi.Trim() | Set-Content -LiteralPath (Join-Path $runDir "wifi.json") -Encoding UTF8
$logs.Trim() | Set-Content -LiteralPath (Join-Path $runDir "logs.json") -Encoding UTF8

netsh wlan disconnect interface="$InterfaceName" | Out-Null
try { schtasks /Delete /F /TN $taskName | Out-Null } catch {}

Write-Host "`n===== /api/ota/status ====="
Write-Host $ota.Trim()
Write-Host "`n===== /api/wifi/status ====="
Write-Host $wifi.Trim()
Write-Host "`n===== /api/logs (boot/reset lines) ====="
if ($logs -match 'boot:\s*fw=') {
  ($logs -split '\},\{') | Where-Object { $_ -match 'boot:|reset_reason|wifi_ap:|ap-recover|brownout|BROWNOUT|WDT|PANIC' } | ForEach-Object { Write-Host $_ }
} else {
  Write-Host $logs.Trim()
}
Write-Host "`nsaved -> $runDir"
