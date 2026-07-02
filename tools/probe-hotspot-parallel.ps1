# Parallel-connection burst stress -- the strongest PC-side attempt to trigger
# the intermittent crash a phone causes.
#
# A mobile browser opening http://192.168.4.1 fires ~8-10 PARALLEL TCP
# connections (index.html + app.js + controls.js + ota.js + style.css +
# pico.css + favicon + /api/state + /api/local-auth/status + the /ws/state
# upgrade) and frequently aborts them mid-response (tab switch / reconnect).
# ESPAsyncWebServer + AsyncTCP are well known to use-after-free under exactly
# that pattern -- and plain sequential curl (my earlier probes) never exercises
# it. This script reproduces the parallel burst + mid-response aborts and
# watches now_ms / boot_count for a reboot (the AP-drop mechanism).
#
# SAFETY: PC internet stays on ETHERNET; WLAN only joins FollowBox
# (192.168.4.x). A scheduled task hard-disconnects WLAN after -SafetyNetSeconds.
param(
  [string]$InterfaceName = "WLAN",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FbHost = "192.168.4.1",
  [int]$ProbeSeconds = 60,
  [int]$SafetyNetSeconds = 120,
  [int]$Parallel = 12
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "hotspot-parallel-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$logFile = Join-Path $runDir "diag.log"
$summaryFile = Join-Path $runDir "summary.json"
$taskName = "FollowBoxParallelSafety_$runId"

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
if ($eth) { Log "internet lifeline: $($eth.InterfaceAlias) gw=$($eth.IPv4DefaultGateway.NextHop)" }
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

$w0 = & curl.exe -sS --max-time 5 "http://$FbHost/api/wifi/status" 2>$null | Out-String
$baseBoot = Field $w0 'boot_count'; $baseReason = Field $w0 'reset_reason'
Log "BASELINE reset_reason=$baseReason boot_count=$baseBoot"

Add-Type -AssemblyName System.Net.Http
$assets = @('/','/index.html','/app.js','/js/controls.js','/js/ota.js','/style.css','/pico.min.css','/favicon.ico','/api/state','/api/local-auth/status','/api/wifi/status','/api/ota/status')

$deadline = (Get-Date).AddSeconds($ProbeSeconds)
$round=0; $reqTotal=0; $reqErr=0; $reboots=0; $lastBoot=$baseBoot; $lastNowMs=$null; $httpFail=0
$rebootEvents=@()
while ((Get-Date) -lt $deadline) {
  $round++
  # ---- parallel burst: $Parallel concurrent GETs, some aborted mid-flight ----
  $clients=@(); $tasks=@()
  for ($p=0; $p -lt $Parallel; $p++) {
    $h = New-Object System.Net.Http.HttpClient
    if (($p % 3) -eq 0) { $to = 150 } else { $to = 2500 }  # every 3rd aborts mid-response
    $h.Timeout = [TimeSpan]::FromMilliseconds($to)
    $clients += $h
    $url = "http://$FbHost" + $assets[$p % $assets.Count]
    $tasks += $h.GetAsync($url)
    $reqTotal++
  }
  try { [System.Threading.Tasks.Task]::WaitAll($tasks, 3000) | Out-Null } catch {}
  foreach ($t in $tasks) { if ($t.IsFaulted -or $t.IsCanceled) { $reqErr++ } }
  foreach ($h in $clients) { try { $h.CancelPendingRequests(); $h.Dispose() } catch {} }

  # ---- reboot check (single now_ms reader) ----
  $st = & curl.exe -sS --max-time 4 "http://$FbHost/api/wifi/status" 2>$null | Out-String
  $ss = & curl.exe -sS --max-time 4 "http://$FbHost/api/state" 2>$null | Out-String
  if ([string]::IsNullOrWhiteSpace($st) -or $st -notmatch 'ap_ready') { $httpFail++ }
  $bc = Field $st 'boot_count'; $rr = Field $st 'reset_reason'; $nm = Field $ss 'now_ms'
  $rb=$false; $why=""
  if ($bc -ne $null -and $lastBoot -ne $null -and [int]$bc -gt [int]$lastBoot) { $rb=$true; $why+="boot_count $lastBoot->$bc; " }
  if ($nm -ne $null -and $lastNowMs -ne $null -and [int64]$nm -lt ([int64]$lastNowMs - 3000)) { $rb=$true; $why+="now_ms $lastNowMs->$nm; " }
  if ($rb) { $reboots++; $m="!!! REBOOT #$reboots round=$round reset_reason=$rr boot_count=$bc ($why)"; Log $m; $rebootEvents+=$m }
  if ($bc -ne $null) { $lastBoot=$bc }
  if ($nm -ne $null -and [int64]$nm -gt 0) { $lastNowMs=$nm }
  if ($round % 10 -eq 0) { Log "round=$round reqTotal=$reqTotal reqErr=$reqErr reboots=$reboots httpFail=$httpFail now_ms=$lastNowMs reason=$rr boot=$bc" }
}
Log "done rounds=$round reqTotal=$reqTotal reqErr=$reqErr reboots=$reboots httpFail=$httpFail finalReason=$rr finalBoot=$lastBoot"

netsh wlan disconnect interface="$InterfaceName" | Out-Null
try { schtasks /Delete /F /TN $taskName | Out-Null; Log "safety-net removed" } catch {}

$summary = [ordered]@{
  runId=$runId; parallel=$Parallel; baseline_reset_reason=$baseReason; baseline_boot_count=$baseBoot
  rounds=$round; requests=$reqTotal; request_errors=$reqErr; http_fail=$httpFail
  reboots_detected=$reboots; reboot_events=$rebootEvents; final_reset_reason=$rr; final_boot_count=$lastBoot
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
Write-Host "`n===== SUMMARY ====="
$summary | ConvertTo-Json -Depth 6
Log "DONE summary=$summaryFile"
