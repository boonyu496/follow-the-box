# Passive FollowBox reboot monitor for PHONE-driven hotspot-drop repro.
#
# The PC cannot reproduce the drop (proven: stable under HTTP + WebSocket +
# captive/DNS flood). So run THIS on the PC as a passive watcher, then use your
# PHONE to reproduce: join FollowBox and open http://192.168.4.1. The monitor
# polls the device once a second and FLAGS a reboot the instant it happens,
# capturing WHY:
#   * reset_reason=PANIC / TASK_WDT / INT_WDT -> firmware fault under phone load
#   * reset_reason=POWERON + boot_count back to 1 + now_ms reset -> BROWNOUT /
#     power sag (EN reset clears the RTC counter on this board)
#   * boot_count climbs, now_ms resets -> device rebooted (not "phone just left")
#   * NO reboot but phone still drops + ap_clients drops phone only -> the phone
#     left a HEALTHY AP (OS no-internet auto-disconnect), not a device fault.
#
# SAFETY: PC internet stays on ETHERNET; WLAN only joins FollowBox
# (192.168.4.x). A scheduled task hard-disconnects WLAN after -SafetyNetSeconds
# no matter what, so the machine can never be stranded offline.
param(
  [string]$InterfaceName = "WLAN",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FbHost = "192.168.4.1",
  [int]$MonitorSeconds = 150,
  [int]$SafetyNetSeconds = 180
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "hotspot-monitor-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$jsonl = Join-Path $runDir "samples.jsonl"
$logFile = Join-Path $runDir "diag.log"
$summaryFile = Join-Path $runDir "summary.json"
$taskName = "FollowBoxMonitorSafety_$runId"

function Log { param([string]$m)
  $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m
  $line | Tee-Object -FilePath $logFile -Append | Out-Null
  Write-Host $line
}
function Field($raw, $name) {
  if ($raw -match ('"' + $name + '":(true|false|-?\d+)')) { return $Matches[1] }
  if ($raw -match ('"' + $name + '":"([^"]*)"')) { return $Matches[1] }
  return $null
}

$eth = Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway -and $_.InterfaceAlias -ne $InterfaceName } | Select-Object -First 1
if ($eth) { Log "internet lifeline: $($eth.InterfaceAlias) gw=$($eth.IPv4DefaultGateway.NextHop)" } else { Log "WARNING: no non-WLAN default gateway" }

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
Log "connecting WLAN to $FollowBoxSsid (PC = passive watcher)"
netsh wlan connect name="$FollowBoxSsid" interface="$InterfaceName" | Out-Null
$srcIp = ""
for ($i=0; $i -lt 25; $i++) {
  Start-Sleep -Milliseconds 800
  try { $srcIp = (Get-NetIPAddress -InterfaceAlias $InterfaceName -AddressFamily IPv4 -ErrorAction Stop | Where-Object { $_.IPAddress -like "192.168.4.*" } | Select-Object -First 1 -ExpandProperty IPAddress) } catch { $srcIp = "" }
  if ($srcIp) { break }
}
Log "followbox src ip='$srcIp'"

$wifi0 = & curl.exe -sS --max-time 5 "http://$FbHost/api/wifi/status" 2>$null | Out-String
$baseBoot = Field $wifi0 'boot_count'; $baseReason = Field $wifi0 'reset_reason'
Log "BASELINE reset_reason=$baseReason boot_count=$baseBoot  <-- now open http://192.168.4.1 ON YOUR PHONE to reproduce"

$deadline = (Get-Date).AddSeconds($MonitorSeconds)
$tick=0; $reboots=0; $lastBoot=$baseBoot; $lastReason=$baseReason
$lastNowMs=$null; $lastClients=$null; $httpFail=0; $rebootEvents=@()
while ((Get-Date) -lt $deadline) {
  $tick++
  $ts = (Get-Date).ToString("HH:mm:ss")
  $st = & curl.exe -sS --max-time 4 "http://$FbHost/api/wifi/status" 2>$null | Out-String
  $ss = & curl.exe -sS --max-time 4 "http://$FbHost/api/state" 2>$null | Out-String
  if ([string]::IsNullOrWhiteSpace($st) -or $st -notmatch 'ap_ready') { $httpFail++ }
  $bc = Field $st 'boot_count'; $rr = Field $st 'reset_reason'
  $cl = Field $st 'ap_clients'; $apr = Field $st 'ap_ready'
  $nm = Field $ss 'now_ms'
  $rebootNow = $false; $why = ""
  if ($bc -ne $null -and $lastBoot -ne $null -and [int]$bc -gt [int]$lastBoot) { $rebootNow=$true; $why += "boot_count $lastBoot->$bc; " }
  if ($nm -ne $null -and $lastNowMs -ne $null -and [int64]$nm -lt ([int64]$lastNowMs - 3000)) { $rebootNow=$true; $why += "now_ms $lastNowMs->$nm; " }
  if ($rr -ne $null -and $lastReason -ne $null -and $rr -ne $lastReason) { $why += "reason $lastReason->$rr; " }
  if ($rebootNow) {
    $reboots++
    $msg = "!!! REBOOT #$reboots at $ts reset_reason=$rr boot_count=$bc  ($why)"
    Log $msg
    $rebootEvents += $msg
  }
  $rec = [ordered]@{ at=$ts; tick=$tick; boot_count=$bc; reset_reason=$rr; ap_clients=$cl; ap_ready=$apr; now_ms=$nm; http_fail=($st -notmatch 'ap_ready') }
  ($rec | ConvertTo-Json -Compress) | Add-Content -LiteralPath $jsonl
  if ($tick % 10 -eq 0) { Log "t=$tick reason=$rr boot=$bc clients=$cl ap_ready=$apr now_ms=$nm reboots=$reboots httpFail=$httpFail" }
  if ($bc -ne $null) { $lastBoot = $bc }
  if ($rr -ne $null) { $lastReason = $rr }
  if ($nm -ne $null -and [int64]$nm -gt 0) { $lastNowMs = $nm }
  $lastClients = $cl
  Start-Sleep -Milliseconds 700
}

netsh wlan disconnect interface="$InterfaceName" | Out-Null
try { schtasks /Delete /F /TN $taskName | Out-Null; Log "safety-net removed" } catch {}

$summary = [ordered]@{
  runId = $runId
  baseline_reset_reason = $baseReason
  baseline_boot_count = $baseBoot
  final_reset_reason = $lastReason
  final_boot_count = $lastBoot
  ticks = $tick
  reboots_detected = $reboots
  reboot_events = $rebootEvents
  http_fail = $httpFail
  jsonl = $jsonl
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
Write-Host "`n===== SUMMARY ====="
$summary | ConvertTo-Json -Depth 6
Log "DONE summary=$summaryFile"
