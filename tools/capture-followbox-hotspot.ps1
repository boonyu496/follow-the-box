param(
  [string]$InterfaceName = "WLAN",
  [string]$InternetSsid = "",
  [string]$FollowBoxSsid = "FollowBox",
  [string]$FollowBoxPassword = "followbox-dev-only",
  [string]$FollowBoxHost = "192.168.4.1",
  [int]$BrowserSeconds = 35,
  [switch]$SkipBrowser,
  [switch]$HiddenSsidProfile,
  [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $OutputRoot = Join-Path $repoRoot "output"
}
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path $OutputRoot "followbox-hotspot-capture-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$logFile = Join-Path $runDir "capture.log"
$summaryFile = Join-Path $runDir "summary.json"
$browserProcess = $null

function Write-Log {
  param([string]$Message)
  $line = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"), $Message
  $line | Tee-Object -FilePath $logFile -Append
}

function Add-SummaryEvent {
  param([string]$Name, [object]$Value)
  $script:summary.events += [ordered]@{
    at = (Get-Date).ToString("o")
    name = $Name
    value = $Value
  }
}

function Save-Summary {
  $script:summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryFile -Encoding UTF8
}

function Invoke-Capture {
  param(
    [string]$Name,
    [scriptblock]$Block
  )
  Write-Log "BEGIN $Name"
  try {
    $text = & $Block 2>&1 | Out-String
    if ($text.Trim().Length -gt 0) {
      $text.TrimEnd() | Tee-Object -FilePath $logFile -Append
    }
    Write-Log "END $Name exit=0"
    return $text
  } catch {
    Write-Log "END $Name exit=1 error=$($_.Exception.Message)"
    return ""
  }
}

function Get-CurrentSsid {
  $lines = netsh wlan show interfaces
  $line = $lines | Where-Object { $_ -match '^\s*SSID\s+:' -and $_ -notmatch 'BSSID' } | Select-Object -First 1
  if (-not $line) { return "" }
  return (($line -split ":", 2)[1]).Trim()
}

function Get-WlanSnapshot {
  $lines = netsh wlan show interfaces
  $state = ""
  $ssid = ""
  $bssid = ""
  $signal = ""
  $channel = ""
  foreach ($line in $lines) {
    if ($line -match '^\s*State\s+:') { $state = (($line -split ":", 2)[1]).Trim() }
    elseif ($line -match '^\s*SSID\s+:' -and $line -notmatch 'BSSID') { $ssid = (($line -split ":", 2)[1]).Trim() }
    elseif ($line -match '^\s*AP BSSID\s+:') { $bssid = (($line -split ":", 2)[1]).Trim() }
    elseif ($line -match '^\s*Signal\s+:') { $signal = (($line -split ":", 2)[1]).Trim() }
    elseif ($line -match '^\s*Channel\s+:') { $channel = (($line -split ":", 2)[1]).Trim() }
  }
  $ips = @()
  try {
    $ips = Get-NetIPAddress -InterfaceAlias $InterfaceName -AddressFamily IPv4 -ErrorAction Stop |
      Select-Object -ExpandProperty IPAddress
  } catch {}
  [ordered]@{
    state = $state
    ssid = $ssid
    bssid = $bssid
    signal = $signal
    channel = $channel
    ipv4 = @($ips)
  }
}

function Get-ProfileConnectionMode {
  param([string]$Ssid)
  if ([string]::IsNullOrWhiteSpace($Ssid)) { return "unknown" }
  $text = netsh wlan show profile name="$Ssid" 2>$null | Out-String
  if ($text -match 'Connection mode\s+:\s+Connect automatically') { return "auto" }
  if ($text -match 'Connection mode\s+:\s+Connect manually') { return "manual" }
  return "unknown"
}

function Set-ProfileMode {
  param([string]$Ssid, [string]$Mode)
  if ([string]::IsNullOrWhiteSpace($Ssid)) { return }
  if ($Mode -ne "auto" -and $Mode -ne "manual") { return }
  Invoke-Capture "set-profile-$Ssid-$Mode" {
    netsh wlan set profileparameter name="$Ssid" connectionmode=$Mode
  } | Out-Null
}

function Ensure-FollowBoxProfile {
  $profilePath = Join-Path $runDir "followbox-wlan-profile.xml"
  @"
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$FollowBoxSsid</name>
  <SSIDConfig>
    <SSID>
      <name>$FollowBoxSsid</name>
    </SSID>
    <nonBroadcast>$($HiddenSsidProfile.IsPresent.ToString().ToLowerInvariant())</nonBroadcast>
  </SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM>
    <security>
      <authEncryption>
        <authentication>WPA2PSK</authentication>
        <encryption>AES</encryption>
        <useOneX>false</useOneX>
      </authEncryption>
      <sharedKey>
        <keyType>passPhrase</keyType>
        <protected>false</protected>
        <keyMaterial>$FollowBoxPassword</keyMaterial>
      </sharedKey>
    </security>
  </MSM>
</WLANProfile>
"@ | Set-Content -LiteralPath $profilePath -Encoding ASCII
  Invoke-Capture "delete-old-followbox-profile" {
    netsh wlan delete profile name="$FollowBoxSsid"
  } | Out-Null
  Invoke-Capture "add-followbox-profile" {
    netsh wlan add profile filename="$profilePath" user=current
  } | Out-Null
  Remove-Item -LiteralPath $profilePath -Force -ErrorAction SilentlyContinue
}

function Get-FollowBoxSourceIp {
  try {
    return (Get-NetIPAddress -InterfaceAlias $InterfaceName -AddressFamily IPv4 -ErrorAction Stop |
      Where-Object { $_.IPAddress -like "192.168.4.*" } |
      Select-Object -First 1 -ExpandProperty IPAddress)
  } catch {
    return ""
  }
}

function Test-RestoredInternetIp {
  param([object]$Snapshot)
  foreach ($ip in @($Snapshot.ipv4)) {
    if ([string]::IsNullOrWhiteSpace($ip)) { continue }
    if ($ip -like "169.254.*") { continue }
    if ($ip -like "192.168.4.*") { continue }
    return $true
  }
  return $false
}

function Invoke-LocalCurl {
  param(
    [string]$Label,
    [string]$Path,
    [string]$SourceIp
  )
  $safeLabel = $Label -replace '[^A-Za-z0-9_.-]', '_'
  $headers = Join-Path $runDir "$safeLabel.headers.txt"
  $body = Join-Path $runDir "$safeLabel.body"
  $url = "http://$FollowBoxHost$Path"
  $args = @("-sS", "--max-time", "8", "-D", $headers, "-o", $body,
            "-w", "CURL_RESULT label=$Label code=%{http_code} remote=%{remote_ip} local=%{local_ip} total=%{time_total} size=%{size_download}`n")
  if (-not [string]::IsNullOrWhiteSpace($SourceIp)) {
    $args += @("--interface", $SourceIp)
  }
  $args += $url
  Write-Log "curl $Label path=$Path source=$SourceIp"
  $out = (& curl.exe @args 2>&1 | Out-String).Trim()
  $exit = $LASTEXITCODE
  if ($out.Length -gt 0) { $out | Tee-Object -FilePath $logFile -Append }
  $result = [ordered]@{
    label = $Label
    path = $Path
    exit = $exit
    output = $out
    headers = $headers
    body = $body
  }
  $script:summary.local_tests += $result
  if ($exit -ne 0 -or $out -notmatch 'code=2\d\d') {
    $script:summary.local_failures += $result
  }
}

function Test-LocalWebSocket {
  param([string]$Uri = "ws://192.168.4.1/ws/state")
  Write-Log "websocket begin $Uri"
  $messages = @()
  try {
    try { Add-Type -AssemblyName System.Net.WebSockets.Client -ErrorAction Stop | Out-Null } catch {}
    $ws = [System.Net.WebSockets.ClientWebSocket]::new()
    $cts = [Threading.CancellationTokenSource]::new()
    $cts.CancelAfter(8000)
    $ws.ConnectAsync([Uri]$Uri, $cts.Token).GetAwaiter().GetResult() | Out-Null
    $buffer = New-Object byte[] 8192
    for ($i = 0; $i -lt 3; $i++) {
      $seg = [ArraySegment[byte]]::new($buffer)
      $res = $ws.ReceiveAsync($seg, $cts.Token).GetAwaiter().GetResult()
      $messages += [Text.Encoding]::UTF8.GetString($buffer, 0, $res.Count)
    }
    $ws.Dispose()
    Write-Log "websocket ok messages=$($messages.Count)"
    $messages | Set-Content -LiteralPath (Join-Path $runDir "ws-state-samples.txt") -Encoding UTF8
    $script:summary.websocket = [ordered]@{ ok = $true; messages = $messages.Count }
  } catch {
    Write-Log "websocket fail $($_.Exception.Message)"
    $script:summary.websocket = [ordered]@{ ok = $false; error = $_.Exception.Message }
  }
}

function Start-LocalBrowser {
  $browser = $null
  foreach ($candidate in @("msedge.exe", "chrome.exe")) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) {
      $browser = $cmd.Source
      break
    }
  }
  if (-not $browser) {
    Write-Log "browser skipped: msedge.exe/chrome.exe not found"
    return $null
  }
  $profileDir = Join-Path $runDir "browser-profile"
  New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
  $args = @(
    "--user-data-dir=$profileDir",
    "--no-first-run",
    "--new-window",
    "--app=http://$FollowBoxHost/"
  )
  Write-Log "browser start exe=$browser seconds=$BrowserSeconds"
  return Start-Process -FilePath $browser -ArgumentList $args -PassThru
}

function Capture-CloudAfterReconnect {
  Write-Log "cloud capture begin"
  $health = Invoke-Capture "cloud-health" {
    Invoke-RestMethod -Uri "https://www.boonai.cn/fb/api/health" -TimeoutSec 12 |
      ConvertTo-Json -Depth 4
  }
  $script:summary.cloud_health = $health.Trim()
}

$summary = [ordered]@{
  started_at = (Get-Date).ToString("o")
  run_dir = $runDir
  interface = $InterfaceName
  internet_ssid = ""
  internet_profile_mode_before = "unknown"
  followbox_ssid = $FollowBoxSsid
  followbox_host = $FollowBoxHost
  connected_to_followbox = $false
  source_ip = ""
  drop_observed = $false
  local_tests = @()
  local_failures = @()
  websocket = $null
  cloud_health = ""
  events = @()
  finished_at = ""
}

try {
  Write-Log "capture dir=$runDir"
  $initialSsid = Get-CurrentSsid
  if ([string]::IsNullOrWhiteSpace($InternetSsid)) {
    $InternetSsid = $initialSsid
  }
  $summary.internet_ssid = $InternetSsid
  $summary.internet_profile_mode_before = Get-ProfileConnectionMode $InternetSsid
  Save-Summary

  Invoke-Capture "pre-netsh-interfaces" { netsh wlan show interfaces } | Out-Null
  Invoke-Capture "pre-followbox-scan" {
    $lines = netsh wlan show networks mode=bssid
    for ($i = 0; $i -lt $lines.Count; $i++) {
      if ($lines[$i] -match "^\s*SSID\s+\d+\s+:\s+$([regex]::Escape($FollowBoxSsid))\s*$") {
        $end = [Math]::Min($i + 18, $lines.Count - 1)
        $lines[$i..$end]
        break
      }
    }
  } | Out-Null

  Ensure-FollowBoxProfile
  Set-ProfileMode -Ssid $InternetSsid -Mode "manual"
  Set-ProfileMode -Ssid $FollowBoxSsid -Mode "manual"

  Invoke-Capture "disconnect-before-followbox" {
    netsh wlan disconnect interface="$InterfaceName"
  } | Out-Null
  Start-Sleep -Seconds 2
  Invoke-Capture "connect-followbox" {
    netsh wlan connect name="$FollowBoxSsid" ssid="$FollowBoxSsid" interface="$InterfaceName"
  } | Out-Null

  for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Seconds 1
    $snap = Get-WlanSnapshot
    Write-Log ("connect-poll={0} state={1} ssid={2} ip={3}" -f $i, $snap.state, $snap.ssid, (($snap.ipv4) -join ","))
    Add-SummaryEvent "connect-poll" $snap
    if ($snap.state -eq "connected" -and $snap.ssid -eq $FollowBoxSsid) {
      $ip = Get-FollowBoxSourceIp
      if (-not [string]::IsNullOrWhiteSpace($ip)) {
        $summary.connected_to_followbox = $true
        $summary.source_ip = $ip
        break
      }
    }
  }
  Save-Summary

  if (-not $summary.connected_to_followbox) {
    Write-Log "failed to connect FollowBox with 192.168.4.x address"
    return
  }

  Invoke-Capture "offline-ipconfig" { Get-NetIPConfiguration -InterfaceAlias $InterfaceName | Format-List * } | Out-Null
  Invoke-Capture "offline-route-192-168-4" { route print 192.168.4.1 } | Out-Null

  $sourceIp = $summary.source_ip
  Invoke-LocalCurl -Label "wifi-status-before-browser" -Path "/api/wifi/status" -SourceIp $sourceIp
  Invoke-LocalCurl -Label "root-before-browser" -Path "/" -SourceIp $sourceIp
  Invoke-LocalCurl -Label "style" -Path "/style.css" -SourceIp $sourceIp
  Invoke-LocalCurl -Label "appjs" -Path "/app.js" -SourceIp $sourceIp
  Invoke-LocalCurl -Label "helpers" -Path "/shared/helpers.js" -SourceIp $sourceIp
  Test-LocalWebSocket

  if (-not $SkipBrowser) {
    $browserProcess = Start-LocalBrowser
    for ($i = 0; $i -lt $BrowserSeconds; $i += 2) {
      Start-Sleep -Seconds 2
      $snap = Get-WlanSnapshot
      Write-Log ("browser-poll={0} state={1} ssid={2} ip={3}" -f $i, $snap.state, $snap.ssid, (($snap.ipv4) -join ","))
      Add-SummaryEvent "browser-poll" $snap
      if ($snap.state -ne "connected" -or $snap.ssid -ne $FollowBoxSsid -or -not ((@($snap.ipv4)) -contains $sourceIp)) {
        $summary.drop_observed = $true
        Write-Log "DROP_OBSERVED during browser-poll=$i"
        break
      }
      Invoke-LocalCurl -Label "wifi-status-browser-$i" -Path "/api/wifi/status" -SourceIp $sourceIp
    }
  }

  Invoke-LocalCurl -Label "wifi-status-after-browser" -Path "/api/wifi/status" -SourceIp $sourceIp
  Invoke-Capture "offline-final-netsh" { netsh wlan show interfaces } | Out-Null
  Save-Summary
} finally {
  if ($browserProcess) {
    try {
      $browserPid = $browserProcess.Id
      if ($browserPid -and -not $browserProcess.HasExited) {
        Write-Log "browser stop pid=$browserPid"
        Stop-Process -Id $browserPid -Force -ErrorAction SilentlyContinue
      } else {
        Write-Log "browser stop skipped pid=$browserPid exited=$($browserProcess.HasExited)"
      }
    } catch {
      Write-Log "browser stop skipped: $($_.Exception.Message)"
    }
  }

  Write-Log "restore WiFi begin target=$InternetSsid"
  Set-ProfileMode -Ssid $InternetSsid -Mode $summary.internet_profile_mode_before
  if (-not [string]::IsNullOrWhiteSpace($InternetSsid)) {
    Invoke-Capture "connect-internet-wifi" {
      netsh wlan connect name="$InternetSsid" ssid="$InternetSsid" interface="$InterfaceName"
    } | Out-Null
    for ($i = 0; $i -lt 20; $i++) {
      Start-Sleep -Seconds 1
      $snap = Get-WlanSnapshot
      Write-Log ("restore-poll={0} state={1} ssid={2} ip={3}" -f $i, $snap.state, $snap.ssid, (($snap.ipv4) -join ","))
      if ($snap.state -eq "connected" -and $snap.ssid -eq $InternetSsid -and (Test-RestoredInternetIp $snap)) { break }
    }
  }
  Invoke-Capture "post-netsh-interfaces" { netsh wlan show interfaces } | Out-Null
  try { Capture-CloudAfterReconnect } catch { Write-Log "cloud capture skipped: $($_.Exception.Message)" }
  $summary.finished_at = (Get-Date).ToString("o")
  Save-Summary
  Write-Log "capture complete summary=$summaryFile"
}
