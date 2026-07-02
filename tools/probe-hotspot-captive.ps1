# Captive-portal + DNS flood stress for FollowBox hotspot.
#
# The PC stayed rock-solid under plain HTTP + WebSocket load, matching history
# ("PC can't repro"). The one thing a PHONE does that curl does NOT is fire a
# burst of OS connectivity-probe requests AND DNS lookups the moment it joins a
# no-internet AP. This script reproduces that burst from the PC and watches for
# a REAL device reset using a SINGLE, reliable now_ms reader (/api/state): a
# genuine reboot makes now_ms jump toward 0. HTTP failures / AP disappearance
# are also tracked.
#
# SAFETY: PC internet stays on ETHERNET; WLAN only joins FollowBox
# (192.168.4.x). A scheduled task hard-disconnects WLAN after -SafetyNetSeconds.
param(
  [string]$InterfaceName = "WLAN",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FbHost = "192.168.4.1",
  [int]$ProbeSeconds = 45,
  [int]$SafetyNetSeconds = 150
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "hotspot-captive-probe-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$logFile = Join-Path $runDir "diag.log"
$summaryFile = Join-Path $runDir "summary.json"
$taskName = "FollowBoxCaptiveProbeSafety_$runId"

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

$otaRaw = & curl.exe -sS --max-time 5 "http://$FbHost/api/ota/status" 2>$null | Out-String
Log "firmware = $(Field $otaRaw 'current_version')"

# OS connectivity-probe URLs a phone fires on joining a no-internet AP.
$probeUrls = @(
  '/generate_204','/gen_204','/connectivity-check.html','/hotspot-detect.html',
  '/library/test/success.html','/connecttest.txt','/ncsi.txt','/success.txt',
  '/favicon.ico','/','/index.html'
)
# Hostnames a phone resolves during captive detection (DNS hijack target).
$probeNames = @('connectivitycheck.gstatic.com','clients3.google.com','www.google.com',
  'captive.apple.com','www.msftconnecttest.com','connectivitycheck.android.com')

$deadline = (Get-Date).AddSeconds($ProbeSeconds)
$tick=0; $httpOk=0; $httpFail=0; $dnsOk=0; $dnsFail=0; $resetDetected=$false
$firstNowMs=$null; $lastNowMs=$null; $stateFail=0
$apReadySeen=@(); $apRecSeen=@()
while ((Get-Date) -lt $deadline) {
  $tick++
  # 1) burst of probe URLs (like a phone's connectivity checks)
  foreach ($u in $probeUrls) {
    $c = & curl.exe -sS --max-time 3 -o NUL -w "%{http_code}" "http://$FbHost$u" 2>$null
    if ($c -match '2\d\d') { $httpOk++ } else { $httpFail++ }
  }
  # 2) DNS lookups against the ESP DNS hijack (:53)
  foreach ($nm in $probeNames) {
    try {
      $r = Resolve-DnsName -Name $nm -Server $FbHost -DnsOnly -QuickTimeout -ErrorAction Stop
      if ($r) { $dnsOk++ }
    } catch { $dnsFail++ }
  }
  # 3) single reliable now_ms reader -> real reset detection
  $ss = & curl.exe -sS --max-time 4 "http://$FbHost/api/state" 2>$null | Out-String
  $nm2 = Field $ss 'now_ms'
  if ($nm2 -ne $null) {
    $nmv = [int64]$nm2
    if ($firstNowMs -eq $null) { $firstNowMs = $nmv }
    if ($lastNowMs -ne $null -and $nmv -lt ($lastNowMs - 3000)) {
      $resetDetected = $true
      Log "!!! REAL RESET: now_ms $lastNowMs -> $nmv (reboot/brownout/WDT)"
    }
    $lastNowMs = $nmv
  } else { $stateFail++ }
  # 4) wifi status snapshot
  $st = & curl.exe -sS --max-time 4 "http://$FbHost/api/wifi/status" 2>$null | Out-String
  $ard = Field $st 'ap_ready'; if ($ard -ne $null) { $apReadySeen += $ard }
  $ar = Field $st 'ap_recoveries'; if ($ar -ne $null) { $apRecSeen += $ar }
  if ($tick % 5 -eq 0) { Log "tick=$tick httpOk=$httpOk httpFail=$httpFail dnsOk=$dnsOk dnsFail=$dnsFail now_ms=$lastNowMs stateFail=$stateFail" }
}
Log "probe done ticks=$tick httpOk=$httpOk httpFail=$httpFail dnsOk=$dnsOk dnsFail=$dnsFail stateFail=$stateFail resetDetected=$resetDetected firstNowMs=$firstNowMs lastNowMs=$lastNowMs"

netsh wlan disconnect interface="$InterfaceName" | Out-Null
try { schtasks /Delete /F /TN $taskName | Out-Null; Log "safety-net removed" } catch {}

$summary = [ordered]@{
  runId = $runId
  firmware_version = (Field $otaRaw 'current_version')
  ticks = $tick
  http_ok = $httpOk
  http_fail = $httpFail
  dns_ok = $dnsOk
  dns_fail = $dnsFail
  state_fail = $stateFail
  reset_detected = $resetDetected
  first_now_ms = $firstNowMs
  last_now_ms = $lastNowMs
  ap_ready_seen = ($apReadySeen | Select-Object -Unique)
  ap_recoveries_seen = ($apRecSeen | Select-Object -Unique)
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
Write-Host "`n===== SUMMARY ====="
$summary | ConvertTo-Json -Depth 6
Log "DONE summary=$summaryFile"
