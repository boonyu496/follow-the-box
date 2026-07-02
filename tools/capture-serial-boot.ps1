param(
  [string]$PortName = "COM18",
  [int]$Baud = 115200,
  [int]$Seconds = 9
)
$ErrorActionPreference = "Continue"
$port = New-Object System.IO.Ports.SerialPort($PortName, $Baud)
$port.ReadTimeout = 1200
try { $port.Open() } catch { Write-Output "OPEN_FAIL: $_"; return }
# Pulse EN/reset via DTR/RTS (esptool-style)
$port.DtrEnable = $false
$port.RtsEnable = $true
Start-Sleep -Milliseconds 120
$port.RtsEnable = $false
Start-Sleep -Milliseconds 60
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$sb = New-Object System.Text.StringBuilder
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
  try { $line = $port.ReadLine(); [void]$sb.AppendLine($line) } catch {}
}
$port.Close()
$out = $sb.ToString()
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $PSScriptRoot ("..\output\serial-boot-$stamp.log")
$out | Set-Content -LiteralPath $logPath -Encoding UTF8
Write-Output "SAVED: $logPath"
$out | Select-String -Pattern "wifi|softap|SoftAP| AP |Guru|panic|Backtrace|Brownout|brownout|rst:|boot:|abort|assert|version|scanless|channel|E \(|W \(" | Select-Object -First 80 | ForEach-Object { $_.Line }
