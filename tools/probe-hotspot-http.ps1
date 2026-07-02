# Focused, time-bounded FollowBox hotspot-drop HTTP probe.
#
# WHY THIS IS SAFE FOR THE AGENT:
#   The PC has internet via ETHERNET (192.168.135.x, gw 192.168.135.1). This
#   script only uses the WLAN adapter to join the FollowBox AP (192.168.4.x).
#   Those subnets do not overlap, and we FORCE the WLAN route metric high so the
#   Ethernet default route always wins -> the agent keeps internet the entire
#   time. On top of that, a scheduled task hard-disconnects WLAN after
#   -SafetyNetSeconds no matter what, so nothing can leave the machine stranded.
#
# WHAT IT ANSWERS:
#   1. /api/ota/status  -> current_version  (is the OLD AP_STA firmware running,
#                          or the HotspotOnly-default build we expect?)
#   2. /api/wifi/status -> net_mode / wifi_mode / ap_recoveries / ap_clients
#   3. GET /            -> reproduces the phone opening 192.168.4.1, the drop
#                          trigger. We watch whether the AP association / http
#                          endpoint dies and whether ap_recoveries climbs.
param(
  [string]$InterfaceName = "WLAN",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FbHost = "192.168.4.1",
  [int]$ProbeSeconds = 45,
  [int]$SafetyNetSeconds = 120
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "hotspot-http-probe-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$jsonl = Join-Path $runDir "samples.jsonl"
$logFile = Join-Path $runDir "diag.log"
$summaryFile = Join-Path $runDir "summary.json"
$taskName = "FollowBoxHttpProbeSafety_$runId"

function Log { param([string]$m)
  $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $m
  $line | Tee-Object -FilePath $logFile -Append | Out-Null
  Write-Host $line
}

function Get-Iface {
  $lines = netsh wlan show interfaces
  $o = [ordered]@{ state=""; ssid=""; bssid=""; signal=""; channel="" }
  foreach ($l in $lines) {
    if ($l -match '^\s*State\s+:') { $o.state = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*SSID\s+:' -and $l -notmatch 'BSSID') { $o.ssid = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*(AP )?BSSID\s+:') { $o.bssid = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*Signal\s+:') { $o.signal = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*Channel\s+:') { $o.channel = (($l -split ":",2)[1]).Trim() }
  }
  return $o
}

# --- 0. Confirm Ethernet still provides internet (agent lifeline) ---
$eth = Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway -and $_.InterfaceAlias -ne $InterfaceName } | Select-Object -First 1
if ($eth) { Log "internet lifeline: $($eth.InterfaceAlias) ip=$($eth.IPv4Address.IPAddress) gw=$($eth.IPv4DefaultGateway.NextHop)" }
else { Log "WARNING: no non-WLAN default gateway found; agent internet not guaranteed" }

# --- 1. Safety net: hard-disconnect WLAN from FollowBox after deadline ---
$runAt = (Get-Date).AddSeconds($SafetyNetSeconds)
$disconnectCmd = "netsh wlan disconnect interface=`"$InterfaceName`""
try {
  schtasks /Create /F /TN $taskName /SC ONCE /ST $runAt.ToString("HH:mm:ss") `
    /TR "powershell -NoProfile -WindowStyle Hidden -Command $disconnectCmd" | Out-Null
  Log "safety-net: WLAN auto-disconnect scheduled at $($runAt.ToString('HH:mm:ss')) (+${SafetyNetSeconds}s)"
} catch {
  Log "safety-net schedule FAILED: $($_.Exception.Message)"
}

# --- 2. Force WLAN route metric high so Ethernet default route always wins ---
$wlanIf = Get-NetIPInterface -InterfaceAlias $InterfaceName -AddressFamily IPv4 -ErrorAction SilentlyContinue
$origMetric = $null
if ($wlanIf) {
  $origMetric = $wlanIf.InterfaceMetric
  try {
    Set-NetIPInterface -InterfaceAlias $InterfaceName -AddressFamily IPv4 -InterfaceMetric 9000 -ErrorAction Stop
    Log "WLAN metric $origMetric -> 9000 (Ethernet stays default route)"
  } catch { Log "could not raise WLAN metric: $($_.Exception.Message)" }
}

# --- 3. Ensure FollowBox WLAN profile ---
$profilePath = Join-Path $runDir "fb-profile.xml"
@"
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$FollowBoxSsid</name>
  <SSIDConfig><SSID><name>$FollowBoxSsid</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM><security>
    <authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>
    <sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>$FollowBoxPassword</keyMaterial></sharedKey>
  </security></MSM>
</WLANProfile>
"@ | Set-Content -LiteralPath $profilePath -Encoding ASCII
netsh wlan add profile filename="$profilePath" user=current | Out-Null
Remove-Item -LiteralPath $profilePath -Force -ErrorAction SilentlyContinue

# --- 4. Connect WLAN to FollowBox, wait for 192.168.4.x ---
Log "connecting WLAN to $FollowBoxSsid"
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
$ifc0 = Get-Iface
Log "followbox src ip='$srcIp' ssid='$($ifc0.ssid)' channel='$($ifc0.channel)' signal='$($ifc0.signal)'"

# --- 5. Baseline: firmware version + wifi mode BEFORE hammering ---
$otaRaw = & curl.exe -sS --max-time 5 "http://$FbHost/api/ota/status" 2>$null | Out-String
$wifiRaw = & curl.exe -sS --max-time 5 "http://$FbHost/api/wifi/status" 2>$null | Out-String
Log "baseline /api/ota/status  = $($otaRaw.Trim())"
Log "baseline /api/wifi/status = $($wifiRaw.Trim())"

# --- 6. Probe loop: hammer GET / (drop trigger) + poll wifi status ---
$samples = New-Object System.Collections.ArrayList
$rootOk=0; $rootFail=0; $statusFail=0; $assocLost=0
$deadline = (Get-Date).AddSeconds($ProbeSeconds)
$tick = 0
while ((Get-Date) -lt $deadline) {
  $tick++
  $ts = (Get-Date).ToString("HH:mm:ss.fff")
  $ifc = Get-Iface
  $associated = ($ifc.ssid -eq $FollowBoxSsid -and $ifc.state -match 'connected')
  if (-not $associated) { $assocLost++ }
  $rc = & curl.exe -sS --max-time 4 -o NUL -w "%{http_code}" "http://$FbHost/" 2>$null
  if ($rc -match '2\d\d') { $rootOk++ } else { $rootFail++ }
  $statusBody = & curl.exe -sS --max-time 4 "http://$FbHost/api/wifi/status" 2>$null | Out-String
  if ([string]::IsNullOrWhiteSpace($statusBody) -or $statusBody -notmatch 'ap_ready') { $statusFail++ }
  $rec = [ordered]@{ at=$ts; tick=$tick; assoc=$associated; pc_ssid=$ifc.ssid; pc_channel=$ifc.channel; pc_signal=$ifc.signal; root_code=$rc; status_raw=$statusBody.Trim() }
  ($rec | ConvertTo-Json -Compress) | Add-Content -LiteralPath $jsonl
  [void]$samples.Add($rec)
  Start-Sleep -Milliseconds 600
}
Log "probe done: ticks=$tick rootOk=$rootOk rootFail=$rootFail statusFail=$statusFail assocLost=$assocLost"

# --- 7. Restore: disconnect WLAN, restore metric, remove safety net ---
netsh wlan disconnect interface="$InterfaceName" | Out-Null
if ($null -ne $origMetric) {
  try { Set-NetIPInterface -InterfaceAlias $InterfaceName -AddressFamily IPv4 -InterfaceMetric $origMetric -ErrorAction Stop; Log "WLAN metric restored -> $origMetric" } catch {}
}
try { schtasks /Delete /F /TN $taskName | Out-Null; Log "safety-net removed" } catch {}

# --- 8. Summarize ---
function Field($raw, $name) {
  if ($raw -match ('"' + $name + '":(true|false|-?\d+)')) { return $Matches[1] }
  if ($raw -match ('"' + $name + '":"([^"]*)"')) { return $Matches[1] }
  return $null
}
$apRec=@(); $apReady=@(); $apClients=@(); $wifiMode=@(); $wifiChan=@(); $netMode=@(); $staStatus=@()
foreach ($s in $samples) {
  $r = $s.status_raw
  if ($r) {
    $apRec += (Field $r 'ap_recoveries'); $apReady += (Field $r 'ap_ready')
    $apClients += (Field $r 'ap_clients'); $wifiMode += (Field $r 'wifi_mode')
    $wifiChan += (Field $r 'wifi_channel'); $netMode += (Field $r 'net_mode')
    $staStatus += (Field $r 'sta_status')
  }
}
$summary = [ordered]@{
  runId = $runId
  firmware_version = (Field $otaRaw 'current_version')
  baseline_net_mode = (Field $wifiRaw 'net_mode')
  baseline_wifi_mode = (Field $wifiRaw 'wifi_mode')
  baseline_wifi_channel = (Field $wifiRaw 'wifi_channel')
  followbox_source_ip = $srcIp
  ticks = $tick
  root_ok = $rootOk
  root_fail = $rootFail
  status_fail = $statusFail
  assoc_lost = $assocLost
  net_mode_seen = ($netMode | Where-Object { $_ -ne $null } | Select-Object -Unique)
  wifi_mode_seen = ($wifiMode | Where-Object { $_ -ne $null } | Select-Object -Unique)
  wifi_channel_seen = ($wifiChan | Where-Object { $_ -ne $null } | Select-Object -Unique)
  ap_ready_seen = ($apReady | Where-Object { $_ -ne $null } | Select-Object -Unique)
  ap_recoveries_seen = ($apRec | Where-Object { $_ -ne $null } | Select-Object -Unique)
  ap_clients_seen = ($apClients | Where-Object { $_ -ne $null } | Select-Object -Unique)
  sta_status_seen = ($staStatus | Where-Object { $_ -ne $null } | Select-Object -Unique)
  ota_status_raw = $otaRaw.Trim()
  wifi_status_raw = $wifiRaw.Trim()
  jsonl = $jsonl
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
Write-Host "`n===== SUMMARY ====="
$summary | ConvertTo-Json -Depth 6
Log "DONE summary=$summaryFile"
