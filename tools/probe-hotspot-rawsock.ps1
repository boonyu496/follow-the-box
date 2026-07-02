# Raw-TCP connection-exhaustion burst -- a distinct AsyncTCP crash class from
# the parallel-HTTP test. Opens many simultaneous raw sockets to :80, some
# sending nothing / partial requests, holds them, then closes -- stressing the
# AsyncTCP pcb pool / heap the way a flaky mobile stack (retransmits, half-open
# connections, rapid reconnects) can. Watches now_ms / boot_count for a reboot.
#
# SAFETY: PC internet stays on ETHERNET; WLAN only joins FollowBox. Scheduled
# task hard-disconnects WLAN after -SafetyNetSeconds.
param(
  [string]$InterfaceName = "WLAN",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FbHost = "192.168.4.1",
  [int]$ProbeSeconds = 50,
  [int]$SafetyNetSeconds = 110,
  [int]$Sockets = 40
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "hotspot-rawsock-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$logFile = Join-Path $runDir "diag.log"
$taskName = "FollowBoxRawSockSafety_$runId"
function Log { param([string]$m) $l="[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"),$m; $l|Tee-Object -FilePath $logFile -Append|Out-Null; Write-Host $l }
function Field($raw,$name){ if($raw -match ('"'+$name+'":(true|false|-?\d+)')){return $Matches[1]}; if($raw -match ('"'+$name+'":"([^"]*)"')){return $Matches[1]}; return $null }

$eth = Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway -and $_.InterfaceAlias -ne $InterfaceName } | Select-Object -First 1
if ($eth) { Log "internet lifeline: $($eth.InterfaceAlias) gw=$($eth.IPv4DefaultGateway.NextHop)" }
$runAt = (Get-Date).AddSeconds($SafetyNetSeconds)
try { schtasks /Create /F /TN $taskName /SC ONCE /ST $runAt.ToString("HH:mm:ss") /TR "powershell -NoProfile -WindowStyle Hidden -Command netsh wlan disconnect interface=`"$InterfaceName`"" | Out-Null; Log "safety-net at $($runAt.ToString('HH:mm:ss'))" } catch { Log "safety-net FAILED" }

$profilePath = Join-Path $runDir "fb-profile.xml"
@"
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$FollowBoxSsid</name><SSIDConfig><SSID><name>$FollowBoxSsid</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType><connectionMode>manual</connectionMode>
  <MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>
  <sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>$FollowBoxPassword</keyMaterial></sharedKey></security></MSM>
</WLANProfile>
"@ | Set-Content -LiteralPath $profilePath -Encoding ASCII
netsh wlan add profile filename="$profilePath" user=current | Out-Null
Remove-Item -LiteralPath $profilePath -Force -ErrorAction SilentlyContinue
Log "connecting WLAN to $FollowBoxSsid"
netsh wlan connect name="$FollowBoxSsid" interface="$InterfaceName" | Out-Null
$srcIp=""; for ($i=0;$i -lt 25;$i++){ Start-Sleep -Milliseconds 800; try{$srcIp=(Get-NetIPAddress -InterfaceAlias $InterfaceName -AddressFamily IPv4 -ErrorAction Stop|Where-Object{$_.IPAddress -like "192.168.4.*"}|Select-Object -First 1 -ExpandProperty IPAddress)}catch{$srcIp=""}; if($srcIp){break} }
Log "src ip='$srcIp'"
$w0 = & curl.exe -sS --max-time 5 "http://$FbHost/api/wifi/status" 2>$null | Out-String
$baseBoot = Field $w0 'boot_count'; $baseReason = Field $w0 'reset_reason'
Log "BASELINE reset_reason=$baseReason boot_count=$baseBoot"

$deadline=(Get-Date).AddSeconds($ProbeSeconds)
$round=0;$reboots=0;$lastBoot=$baseBoot;$lastNowMs=$null;$openErr=0;$rebootEvents=@()
while((Get-Date) -lt $deadline){
  $round++
  $socks=@()
  for($s=0;$s -lt $Sockets;$s++){
    try{
      $tcp=New-Object System.Net.Sockets.TcpClient
      $iar=$tcp.BeginConnect($FbHost,80,$null,$null)
      $null=$iar.AsyncWaitHandle.WaitOne(400)
      $socks+=$tcp
      # send a partial request on ~half so the server waits for more (half-open)
      if($tcp.Connected -and ($s % 2 -eq 0)){ $ns=$tcp.GetStream(); $b=[Text.Encoding]::ASCII.GetBytes("GET / HTTP/1.1`r`nHost: 192.168.4.1`r`n"); $ns.Write($b,0,$b.Length) }
    }catch{ $openErr++ }
  }
  Start-Sleep -Milliseconds 500
  foreach($t in $socks){ try{$t.Close()}catch{} }
  # reboot check
  $st = & curl.exe -sS --max-time 4 "http://$FbHost/api/wifi/status" 2>$null | Out-String
  $ss = & curl.exe -sS --max-time 4 "http://$FbHost/api/state" 2>$null | Out-String
  $bc=Field $st 'boot_count'; $rr=Field $st 'reset_reason'; $nm=Field $ss 'now_ms'
  $rb=$false;$why=""
  if($bc -ne $null -and $lastBoot -ne $null -and [int]$bc -gt [int]$lastBoot){$rb=$true;$why+="boot_count $lastBoot->$bc; "}
  if($nm -ne $null -and $lastNowMs -ne $null -and [int64]$nm -lt ([int64]$lastNowMs-3000)){$rb=$true;$why+="now_ms $lastNowMs->$nm; "}
  if($rb){$reboots++;$m="!!! REBOOT #$reboots round=$round reset_reason=$rr boot_count=$bc ($why)";Log $m;$rebootEvents+=$m}
  if($bc -ne $null){$lastBoot=$bc}
  if($nm -ne $null -and [int64]$nm -gt 0){$lastNowMs=$nm}
  if($round % 10 -eq 0){ Log "round=$round openErr=$openErr reboots=$reboots now_ms=$lastNowMs reason=$rr boot=$bc" }
}
Log "done rounds=$round openErr=$openErr reboots=$reboots finalReason=$rr finalBoot=$lastBoot"
netsh wlan disconnect interface="$InterfaceName" | Out-Null
try { schtasks /Delete /F /TN $taskName | Out-Null } catch {}
Write-Host "`n===== SUMMARY ====="
[ordered]@{ sockets=$Sockets; baseline_reset_reason=$baseReason; baseline_boot_count=$baseBoot; rounds=$round; reboots_detected=$reboots; reboot_events=$rebootEvents; final_reset_reason=$rr; final_boot_count=$lastBoot } | ConvertTo-Json -Depth 6
