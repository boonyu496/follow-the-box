param(
  [string]$InterfaceName = "WLAN",
  [string]$InternetSsid = "quanyuxixi2022",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxHost = "192.168.4.1",
  [int]$RestoreDeadlineSeconds = 90
)
$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "ota-verify-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$taskName = "FollowBoxWifiRestore_$runId"

# Safety net: force reconnect internet after deadline no matter what.
$restoreCmd = "netsh wlan connect name=`"$InternetSsid`" interface=`"$InterfaceName`""
$runAt = (Get-Date).AddSeconds($RestoreDeadlineSeconds)
schtasks /Create /F /TN $taskName /SC ONCE /ST $runAt.ToString("HH:mm:ss") `
  /TR "powershell -NoProfile -WindowStyle Hidden -Command $restoreCmd" | Out-Null

netsh wlan connect name="$FollowBoxSsid" interface="$InterfaceName" | Out-Null
$srcIp = ""
for ($i=0; $i -lt 25; $i++) {
  Start-Sleep -Milliseconds 800
  try {
    $srcIp = (Get-NetIPAddress -InterfaceAlias $InterfaceName -AddressFamily IPv4 -ErrorAction Stop |
      Where-Object { $_.IPAddress -like "192.168.4.*" } | Select-Object -First 1 -ExpandProperty IPAddress)
  } catch { $srcIp = "" }
  if ($srcIp) { break }
}
$result = [ordered]@{ connected_ip = $srcIp }
if ($srcIp) {
  $result.ota_status = (& curl.exe -sS --max-time 5 "http://$FollowBoxHost/api/ota/status" 2>$null | Out-String).Trim()
  $result.wifi_status = (& curl.exe -sS --max-time 5 "http://$FollowBoxHost/api/wifi/status" 2>$null | Out-String).Trim()
  $result.state = (& curl.exe -sS --max-time 5 "http://$FollowBoxHost/api/state" 2>$null | Out-String).Trim()
  $result.root_code = (& curl.exe -sS --max-time 5 -o NUL -w "%{http_code}" "http://$FollowBoxHost/" 2>$null)
}

# Restore internet
netsh wlan connect name="$InternetSsid" interface="$InterfaceName" | Out-Null
$restored = $false
for ($i=0; $i -lt 30; $i++) {
  Start-Sleep -Milliseconds 800
  $ssid = (netsh wlan show interfaces | Where-Object { $_ -match '^\s*SSID\s+:' -and $_ -notmatch 'BSSID' } | Select-Object -First 1)
  if ($ssid -match $InternetSsid) { $restored = $true; break }
}
$result.internet_restored = $restored
try { schtasks /Delete /F /TN $taskName | Out-Null } catch {}

$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runDir "verify.json") -Encoding UTF8
$result | ConvertTo-Json -Depth 6
