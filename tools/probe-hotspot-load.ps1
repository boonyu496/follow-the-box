# Full-load FollowBox hotspot probe that mimics what a PHONE does.
#
# The light `GET /` probe never dropped the AP, matching history ("PC can't
# repro"). A phone does more: it opens a persistent WebSocket (/ws/state) that
# makes the ESP push telemetry continuously (heavy, sustained radio TX), loads
# every asset, and re-runs captive-portal checks. This script reproduces the
# heavy path and, crucially, detects a device RESET/brownout by watching the
# monotonic `now_ms` field in the telemetry JSON: if now_ms jumps DOWN, the
# ESP32-S3 rebooted (brownout/WDT) -> that reboot is what drops the phone.
#
# SAFETY (agent stays online): PC internet is on ETHERNET (192.168.135.x). We
# only put the WLAN adapter on FollowBox (192.168.4.x, non-overlapping) and a
# scheduled task hard-disconnects WLAN after -SafetyNetSeconds no matter what.
param(
  [string]$InterfaceName = "WLAN",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FbHost = "192.168.4.1",
  [int]$ProbeSeconds = 50,
  [int]$SafetyNetSeconds = 150,
  [int]$WsClients = 2
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "hotspot-load-probe-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$logFile = Join-Path $runDir "diag.log"
$summaryFile = Join-Path $runDir "summary.json"
$taskName = "FollowBoxLoadProbeSafety_$runId"

function Log { param([string]$m)
  $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $m
  $line | Tee-Object -FilePath $logFile -Append | Out-Null
  Write-Host $line
}
function Field($raw, $name) {
  if ($raw -match ('"' + $name + '":(true|false|-?\d+)')) { return $Matches[1] }
  if ($raw -match ('"' + $name + '":"([^"]*)"')) { return $Matches[1] }
  return $null
}

# --- lifeline + safety net ---
$eth = Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway -and $_.InterfaceAlias -ne $InterfaceName } | Select-Object -First 1
if ($eth) { Log "internet lifeline: $($eth.InterfaceAlias) gw=$($eth.IPv4DefaultGateway.NextHop)" } else { Log "WARNING: no non-WLAN default gateway" }
$runAt = (Get-Date).AddSeconds($SafetyNetSeconds)
try {
  schtasks /Create /F /TN $taskName /SC ONCE /ST $runAt.ToString("HH:mm:ss") `
    /TR "powershell -NoProfile -WindowStyle Hidden -Command netsh wlan disconnect interface=`"$InterfaceName`"" | Out-Null
  Log "safety-net: WLAN auto-disconnect at $($runAt.ToString('HH:mm:ss')) (+${SafetyNetSeconds}s)"
} catch { Log "safety-net FAILED: $($_.Exception.Message)" }

# --- ensure profile + connect ---
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

# --- baseline ---
$otaRaw = & curl.exe -sS --max-time 5 "http://$FbHost/api/ota/status" 2>$null | Out-String
$wifiRaw = & curl.exe -sS --max-time 5 "http://$FbHost/api/wifi/status" 2>$null | Out-String
Log "firmware = $(Field $otaRaw 'current_version')  net_mode=$(Field $wifiRaw 'net_mode') wifi_mode=$(Field $wifiRaw 'wifi_mode')"

# --- full page asset load (mimic browser opening 192.168.4.1) ---
foreach ($asset in @('/', '/index.html', '/app.js', '/js/controls.js', '/js/ota.js', '/style.css', '/pico.min.css')) {
  $c = & curl.exe -sS --max-time 5 -o NUL -w "%{http_code}" "http://$FbHost$asset" 2>$null
  Log "asset $asset -> $c"
}

# --- open persistent WebSocket clients to /ws/state (the heavy sustained load) ---
$sockets = @()
$cts = New-Object System.Threading.CancellationTokenSource
for ($n=0; $n -lt $WsClients; $n++) {
  try {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $uri = [Uri]("ws://$FbHost/ws/state")
    $ws.ConnectAsync($uri, $cts.Token).Wait(5000) | Out-Null
    if ($ws.State -eq 'Open') { $sockets += $ws; Log "ws#$n connected state=$($ws.State)" }
    else { Log "ws#$n NOT open state=$($ws.State)" }
  } catch { Log "ws#$n connect error: $($_.Exception.Message)" }
}

# --- probe loop: drain WS, watch now_ms for reset, poll ap_recoveries ---
$buf = New-Object 'System.Byte[]' 8192
$seg = New-Object System.ArraySegment[byte] -ArgumentList (,$buf)
$deadline = (Get-Date).AddSeconds($ProbeSeconds)
$tick=0; $wsMsgs=0; $wsDrops=0; $resetDetected=$false
$lastNowMs = -1; $minNowMsAfterMax = $null; $maxNowMs = -1
$apRecSeen=@(); $apReadySeen=@(); $apClientsSeen=@(); $rootFail=0; $rootOk=0
$firstNowMs = $null; $lastGoodNowMs = $null
while ((Get-Date) -lt $deadline) {
  $tick++
  # drain any queued WS frames (non-blocking-ish via short timed receive)
  for ($si=0; $si -lt $sockets.Count; $si++) {
    $ws = $sockets[$si]
    if ($ws.State -ne 'Open') { $wsDrops++; continue }
    try {
      $recvCts = New-Object System.Threading.CancellationTokenSource
      $recvCts.CancelAfter(500)
      $task = $ws.ReceiveAsync($seg, $recvCts.Token)
      $task.Wait()
      $res = $task.Result
      if ($res.Count -gt 0) {
        $wsMsgs++
        $txt = [System.Text.Encoding]::UTF8.GetString($buf, 0, $res.Count)
        $nm = Field $txt 'now_ms'
        if ($nm -ne $null) {
          $nmv = [int64]$nm
          if ($firstNowMs -eq $null) { $firstNowMs = $nmv }
          if ($nmv -gt $maxNowMs) { $maxNowMs = $nmv }
          if ($lastGoodNowMs -ne $null -and $nmv -lt ($lastGoodNowMs - 3000)) {
            $resetDetected = $true
            Log "!!! RESET DETECTED: now_ms dropped $lastGoodNowMs -> $nmv (device rebooted / brownout / WDT)"
          }
          $lastGoodNowMs = $nmv
        }
      }
    } catch {
      # timeout or closed
      if ($ws.State -ne 'Open') { $wsDrops++ }
    }
  }
  # periodic HTTP poll (every ~2 ticks)
  if ($tick % 2 -eq 0) {
    $rc = & curl.exe -sS --max-time 4 -o NUL -w "%{http_code}" "http://$FbHost/" 2>$null
    if ($rc -match '2\d\d') { $rootOk++ } else { $rootFail++ }
    $st = & curl.exe -sS --max-time 4 "http://$FbHost/api/wifi/status" 2>$null | Out-String
    $ar = Field $st 'ap_recoveries'; if ($ar -ne $null) { $apRecSeen += $ar }
    $ard = Field $st 'ap_ready'; if ($ard -ne $null) { $apReadySeen += $ard }
    $acl = Field $st 'ap_clients'; if ($acl -ne $null) { $apClientsSeen += $acl }
    $sa = Field $st 'api/state'
    # also read now_ms via /api/state as a fallback reset detector
    $ss = & curl.exe -sS --max-time 4 "http://$FbHost/api/state" 2>$null | Out-String
    $nm2 = Field $ss 'now_ms'
    if ($nm2 -ne $null) {
      $nmv2 = [int64]$nm2
      if ($lastGoodNowMs -ne $null -and $nmv2 -lt ($lastGoodNowMs - 3000)) {
        $resetDetected = $true
        Log "!!! RESET DETECTED via /api/state: now_ms $lastGoodNowMs -> $nmv2"
      }
      if ($nmv2 -gt 0) { $lastGoodNowMs = $nmv2 }
    }
  }
  Start-Sleep -Milliseconds 250
}
Log "probe done: ticks=$tick wsMsgs=$wsMsgs wsDrops=$wsDrops rootOk=$rootOk rootFail=$rootFail resetDetected=$resetDetected firstNowMs=$firstNowMs lastNowMs=$lastGoodNowMs"

# --- teardown WS ---
foreach ($ws in $sockets) { try { $ws.Abort(); $ws.Dispose() } catch {} }

# --- restore ---
netsh wlan disconnect interface="$InterfaceName" | Out-Null
try { schtasks /Delete /F /TN $taskName | Out-Null; Log "safety-net removed" } catch {}

$summary = [ordered]@{
  runId = $runId
  firmware_version = (Field $otaRaw 'current_version')
  net_mode = (Field $wifiRaw 'net_mode')
  wifi_mode = (Field $wifiRaw 'wifi_mode')
  ws_clients_requested = $WsClients
  ws_connected = $sockets.Count
  ws_messages = $wsMsgs
  ws_drops = $wsDrops
  reset_detected = $resetDetected
  first_now_ms = $firstNowMs
  last_now_ms = $lastGoodNowMs
  root_ok = $rootOk
  root_fail = $rootFail
  ap_ready_seen = ($apReadySeen | Select-Object -Unique)
  ap_recoveries_seen = ($apRecSeen | Select-Object -Unique)
  ap_clients_seen = ($apClientsSeen | Select-Object -Unique)
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
Write-Host "`n===== SUMMARY ====="
$summary | ConvertTo-Json -Depth 6
Log "DONE summary=$summaryFile"
