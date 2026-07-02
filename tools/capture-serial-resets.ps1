# Repeated-reset COM18 capture to catch the early-boot PANIC + backtrace.
#
# The observed panic fired ~1s after a clean POWERON boot. The ESP-IDF panic
# handler prints "Guru Meditation ... Backtrace: 0x..." to UART0 by default,
# and UART0 == CP210x == COM18 (which IS connected). So we pulse a hardware
# reset, capture a few seconds of COM18, and repeat: if the panic is
# intermittent this loop should eventually catch the full backtrace.
#
# ZERO internet risk: reads the board over USB only; the PC never touches WiFi,
# so Ethernet internet is untouched. Always stops after all cycles.
param(
  [string]$Port = "COM18",
  [int]$Baud = 115200,
  [int]$Cycles = 8,
  [int]$SecondsPerCycle = 7
)

$ErrorActionPreference = "Continue"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path (Join-Path $repoRoot "output") "serial-resets-$runId"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$logFile = Join-Path $runDir "serial.log"

Write-Host "[resets] port=$Port cycles=$Cycles secondsPerCycle=$SecondsPerCycle -> $logFile"

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$sp.ReadTimeout = 400
$sp.NewLine = "`n"
$sp.DtrEnable = $false
$sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Host "[resets] cannot open $Port : $($_.Exception.Message)"; exit 1 }

$writer = [System.IO.StreamWriter]::new($logFile, $true); $writer.AutoFlush = $true
$flagPattern = 'Guru Meditation|Backtrace|panic|Panic|abort\(\)|assert|StoreProhibited|LoadProhibited|InstrFetchProhibited|Cache disabled|CORRUPT HEAP|register dump|PC\s*:|EXCVADDR|rst:0x|boot:0x|brownout|Brownout|BROWNOUT|wdt|WDT|E \(|ELF file SHA'
$allFlagged = New-Object System.Collections.ArrayList
$panicCaught = $false

for ($c = 1; $c -le $Cycles; $c++) {
  # discard anything queued
  try { $sp.DiscardInBuffer() } catch {}
  Write-Host "[resets] cycle $c/$Cycles : pulsing reset"
  $writer.WriteLine("===== CYCLE $c RESET @ $(Get-Date -Format HH:mm:ss.fff) =====")
  # EN low (RTS) briefly, IO0 high (DTR false) -> clean run-mode reboot
  $sp.DtrEnable = $false
  $sp.RtsEnable = $true
  Start-Sleep -Milliseconds 150
  $sp.RtsEnable = $false

  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while ($sw.Elapsed.TotalSeconds -lt $SecondsPerCycle) {
    try { $line = $sp.ReadLine() }
    catch [TimeoutException] { continue }
    catch { Start-Sleep -Milliseconds 100; continue }
    if ($null -eq $line) { continue }
    $stamp = "[c$c +{0:0.0}s] {1}" -f $sw.Elapsed.TotalSeconds, $line.TrimEnd()
    $writer.WriteLine($stamp)
    if ($line -match $flagPattern) {
      Write-Host $stamp
      [void]$allFlagged.Add($stamp)
      if ($line -match 'Guru Meditation|Backtrace|StoreProhibited|LoadProhibited|abort\(\)|panic') { $panicCaught = $true }
    }
  }
}

$writer.Flush(); $writer.Close(); $sp.Close()
Write-Host "`n[resets] DONE. full log: $logFile"
Write-Host "[resets] panicCaught=$panicCaught flaggedLines=$($allFlagged.Count)"
if ($allFlagged.Count -gt 0) {
  Write-Host "----- flagged lines -----"
  $allFlagged | ForEach-Object { Write-Host $_ }
}
