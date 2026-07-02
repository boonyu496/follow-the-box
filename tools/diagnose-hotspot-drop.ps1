param(
  [string]$InterfaceName = "WLAN",
  [string]$InternetSsid = "quanyuxixi2022",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FollowBoxHost = "192.168.4.1",
  [int]$FollowBoxSeconds = 60,
  [int]$RestoreDeadlineSeconds = 150
)

# Offline-survivable hotspot drop diagnostic.
# While connected to FollowBox the PC has no internet, so this script writes
# everything to disk and registers a scheduled task that force-reconnects the
# internet WiFi after $RestoreDeadlineSeconds no matter what (safety net so the
# machine/agent comes back online even if this script is interrupted).

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "hotspot-drop-diag-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$jsonl = Join-Path $runDir "wifi-status.jsonl"
$logFile = Join-Path $runDir "diag.log"
$summaryFile = Join-Path $runDir "summary.json"
$taskName = "FollowBoxWifiRestore_$runId"

function Log { param([string]$m)
  $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $m
  $line | Tee-Object -FilePath $logFile -Append
}

function Get-Iface {
  $lines = netsh wlan show interfaces
  $o = [ordered]@{ state=""; ssid=""; bssid=""; signal=""; channel="" }
  foreach ($l in $lines) {
    if ($l -match '^\s*State\s+:') { $o.state = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*SSID\s+:' -and $l -notmatch 'BSSID') { $o.ssid = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*AP BSSID\s+:') { $o.bssid = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*Signal\s+:') { $o.signal = (($l -split ":",2)[1]).Trim() }
    elseif ($l -match '^\s*Channel\s+:') { $o.channel = (($l -split ":",2)[1]).Trim() }
  }
  return $o
}

# --- 1. Safety net: scheduled task to force-restore internet WiFi ---
$restoreCmd = "netsh wlan connect name=`"$InternetSsid`" interface=`"$InterfaceName`""
$runAt = (Get-Date).AddSeconds($RestoreDeadlineSeconds)
try {
  schtasks /Create /F /TN $taskName /SC ONCE /ST $runAt.ToString("HH:mm:ss") `
    /TR "powershell -NoProfile -WindowStyle Hidden -Command $restoreCmd" | Out-Null
  Log "safety-net scheduled task $taskName at $($runAt.ToString('HH:mm:ss'))"
} catch {
  Log "safety-net schedule FAILED: $($_.Exception.Message)"
}

# --- 2. Ensure FollowBox profile exists ---
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

# --- 3. Connect to FollowBox, wait for 192.168.4.x ---
Log "connecting to $FollowBoxSsid"
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
Log "followbox source ip = '$srcIp' iface=$((Get-Iface) | ConvertTo-Json -Compress)"

# --- 4. Poll /api/wifi/status for $FollowBoxSeconds while hammering root page ---
$samples = New-Object System.Collections.ArrayList
$rootFails = 0; $rootOk = 0; $statusFails = 0
$deadline = (Get-Date).AddSeconds($FollowBoxSeconds)
$tick = 0
while ((Get-Date) -lt $deadline) {
  $tick++
  $ts = (Get-Date).ToString("o")
  $ifc = Get-Iface
  # Load root page (simulate browser opening 192.168.4.1)
  $rc = & curl.exe -sS --max-time 4 -o NUL -w "%{http_code}" "http://$FollowBoxHost/" 2>$null
  if ($rc -match '2\d\d') { $rootOk++ } else { $rootFails++ }
  # Poll wifi status
  $statusBody = & curl.exe -sS --max-time 4 "http://$FollowBoxHost/api/wifi/status" 2>$null | Out-String
  $rec = [ordered]@{ at=$ts; tick=$tick; pc_ssid=$ifc.ssid; pc_channel=$ifc.channel; pc_signal=$ifc.signal; root_code=$rc; status_raw=$statusBody.Trim() }
  if ([string]::IsNullOrWhiteSpace($statusBody) -or $statusBody -notmatch 'ap_ready') { $statusFails++ }
  ($rec | ConvertTo-Json -Compress) | Add-Content -LiteralPath $jsonl
  [void]$samples.Add($rec)
  Start-Sleep -Milliseconds 700
}
Log "collected $tick samples rootOk=$rootOk rootFails=$rootFails statusFails=$statusFails"

# --- 5. Restore internet WiFi ---
Log "restoring $InternetSsid"
netsh wlan connect name="$InternetSsid" interface="$InterfaceName" | Out-Null
$restored = $false
for ($i=0; $i -lt 30; $i++) {
  Start-Sleep -Milliseconds 800
  $ifc = Get-Iface
  if ($ifc.ssid -eq $InternetSsid -and $ifc.state -match 'connected') { $restored = $true; break }
}
Log "restored=$restored"

# --- 6. Cleanup safety net ---
try { schtasks /Delete /F /TN $taskName | Out-Null; Log "safety-net removed" } catch {}

# --- 7. Summarize ap_recoveries / channel / sta_status transitions ---
function Field($raw, $name) {
  if ($raw -match ('"' + $name + '":(true|false|-?\d+)')) { return $Matches[1] }
  if ($raw -match ('"' + $name + '":"([^"]*)"')) { return $Matches[1] }
  return $null
}
$apRec = @(); $apClients = @(); $apReady = @(); $wifiChan = @(); $staStatus = @()
foreach ($s in $samples) {
  $r = $s.status_raw
  if ($r) {
    $apRec += (Field $r 'ap_recoveries')
    $apClients += (Field $r 'ap_clients')
    $apReady += (Field $r 'ap_ready')
    $wifiChan += (Field $r 'wifi_channel')
    $staStatus += (Field $r 'sta_status')
  }
}
$summary = [ordered]@{
  runId = $runId
  followbox_source_ip = $srcIp
  samples = $tick
  root_ok = $rootOk
  root_fails = $rootFails
  status_fails = $statusFails
  ap_recoveries_seen = ($apRec | Where-Object { $_ -ne $null } | Select-Object -Unique)
  ap_clients_seen = ($apClients | Where-Object { $_ -ne $null } | Select-Object -Unique)
  ap_ready_seen = ($apReady | Where-Object { $_ -ne $null } | Select-Object -Unique)
  wifi_channel_seen = ($wifiChan | Where-Object { $_ -ne $null } | Select-Object -Unique)
  sta_status_seen = ($staStatus | Where-Object { $_ -ne $null } | Select-Object -Unique)
  restored = $restored
  jsonl = $jsonl
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
$summary | ConvertTo-Json -Depth 6
Log "DONE summary=$summaryFile"
