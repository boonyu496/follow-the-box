# Time-bounded ESP32-S3 serial capture for hotspot-drop diagnosis.
#
# SAFE FOR THE AGENT/PC: this reads the board over USB (COM18) only. The PC
# keeps its Ethernet internet the whole time -- it never joins the FollowBox
# hotspot, so there is zero risk of the machine going offline.
#
# What it captures:
#   * The boot banner (firmware version + WiFi mode) after an optional reset.
#   * The periodic "wifi:" / "wifi_ap:" logs.
#   * Any brownout / WDT / panic / reboot that fires while a phone opens
#     http://192.168.4.1 (the reported drop trigger).
#
# It ALWAYS stops after -Seconds (default 90) and closes the port, so it can
# never hang the terminal.
param(
  [string]$Port = "COM18",
  [int]$Baud = 115200,
  [int]$Seconds = 90,
  [switch]$Reset,          # pulse EN via DTR/RTS to capture the boot banner
  [string]$Label = "capture"
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "serial-$Label-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$logFile = Join-Path $runDir "serial.log"

Write-Host "[capture] port=$Port baud=$Baud seconds=$Seconds reset=$($Reset.IsPresent)"
Write-Host "[capture] writing -> $logFile"

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$sp.ReadTimeout = 500
$sp.NewLine = "`n"
# Do NOT let opening the port trigger a spurious reset unless asked.
$sp.DtrEnable = $false
$sp.RtsEnable = $false

try {
  $sp.Open()
} catch {
  Write-Host "[capture] FAILED to open $Port : $($_.Exception.Message)"
  exit 1
}

if ($Reset) {
  # CP210x auto-reset circuit: RTS->EN, DTR->IO0. Pulse EN low to reboot into
  # run mode (IO0 stays high). This lets us capture the boot banner.
  Write-Host "[capture] pulsing reset to capture boot banner..."
  $sp.DtrEnable = $false   # IO0 high -> normal boot
  $sp.RtsEnable = $true    # EN low  -> hold in reset
  Start-Sleep -Milliseconds 150
  $sp.RtsEnable = $false   # EN high -> boot
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$deadline = (Get-Date).AddSeconds($Seconds)
$writer = [System.IO.StreamWriter]::new($logFile, $true)
$writer.AutoFlush = $true

$flagPattern = 'brownout|Brownout|rst:0x|boot:0x|panic|Panic|Guru Meditation|watchdog|WDT|wdt|abort\(\)|assert|StoreProhibited|LoadProhibited|CORRUPT HEAP|Backtrace|version=|net-mode|hotspot|link\.|wifi:|wifi_ap:|wifi_sta:'
$flagged = New-Object System.Collections.ArrayList

while ((Get-Date) -lt $deadline) {
  try {
    $line = $sp.ReadLine()
  } catch [TimeoutException] {
    continue
  } catch {
    $writer.WriteLine("[read-error] $($_.Exception.Message)")
    Start-Sleep -Milliseconds 200
    continue
  }
  if ($null -eq $line) { continue }
  $stamp = "[{0:0.0}s] {1}" -f $sw.Elapsed.TotalSeconds, $line.TrimEnd()
  $writer.WriteLine($stamp)
  if ($line -match $flagPattern) {
    Write-Host $stamp
    [void]$flagged.Add($stamp)
  }
}

$writer.Flush()
$writer.Close()
$sp.Close()

Write-Host "`n[capture] DONE after $Seconds s. Full log: $logFile"
Write-Host "[capture] flagged lines: $($flagged.Count)"
if ($flagged.Count -gt 0) {
  Write-Host "----- flagged (version/wifi/crash) -----"
  $flagged | ForEach-Object { Write-Host $_ }
}
