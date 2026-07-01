# FollowBox control center common HTTP, path, and process helpers.

function Assert-PathExists {
  param(
    [string]$Path,
    [string]$Label
  )

  if ([string]::IsNullOrWhiteSpace($Path)) {
    throw "$Label is not configured."
  }
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "$Label not found: $Path"
  }
}

function Test-ExistingPath {
  param([string]$Path)

  if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
  return (Test-Path -LiteralPath $Path)
}

function Test-CommandExists {
  param([string]$Name)

  try {
    return ($null -ne (Get-Command -Name $Name -ErrorAction SilentlyContinue))
  } catch {
    return $false
  }
}

function Join-UrlPath {
  param(
    [string]$BaseUrl,
    [string]$RelativePath
  )

  if ([string]::IsNullOrWhiteSpace($BaseUrl)) { return $RelativePath }
  $base = [string]$BaseUrl
  if (-not $base.EndsWith("/")) {
    $base += "/"
  }
  return ($base + $RelativePath.TrimStart("/"))
}

function ConvertTo-UrlComponent {
  param([string]$Value)

  return [System.Uri]::EscapeDataString([string]$Value)
}

function Get-WebResponseText {
  param([object]$Response)

  if ($null -eq $Response -or $null -eq $Response.Content) { return "" }
  if ($Response.Content -is [byte[]]) {
    return ([System.Text.Encoding]::UTF8.GetString($Response.Content)).Trim()
  }
  return (($Response.Content | Out-String).Trim())
}

function Test-HostReachable {
  param([string]$Target)

  if ([string]::IsNullOrWhiteSpace($Target)) { return $false }

  try {
    return [bool](Test-Connection -ComputerName $Target -Count 1 -Quiet -ErrorAction Stop)
  } catch {
    return $false
  }
}

function Get-HashtableValue {
  param(
    [hashtable]$Table,
    [string]$Key,
    [object]$Default = $null
  )

  if ($null -ne $Table -and $Table.ContainsKey($Key) -and $null -ne $Table[$Key]) {
    return $Table[$Key]
  }
  return $Default
}

function Resolve-PlatformIoCommand {
  param([string]$Command)

  $candidate = if ([string]::IsNullOrWhiteSpace($Command)) { Find-PlatformIoCommand } else { $Command }
  if ([string]::IsNullOrWhiteSpace($candidate)) { $candidate = "pio" }

  try {
    if ($candidate -eq "pio") {
      $found = Get-Command -Name pio -ErrorAction SilentlyContinue | Select-Object -First 1
      if ($null -ne $found -and -not [string]::IsNullOrWhiteSpace([string]$found.Source)) {
        return [ordered]@{ ok = $true; command = "pio"; resolvedPath = ([string]$found.Source).Trim(); reason = "" }
      }
      return [ordered]@{ ok = $false; command = $candidate; resolvedPath = ""; reason = "pio was not found in PATH." }
    }

    if (Test-Path -LiteralPath $candidate) {
      return [ordered]@{ ok = $true; command = $candidate; resolvedPath = $candidate; reason = "" }
    }

    return [ordered]@{ ok = $false; command = $candidate; resolvedPath = ""; reason = "Configured PlatformIO command was not found: $candidate" }
  } catch {
    return [ordered]@{ ok = $false; command = $candidate; resolvedPath = ""; reason = $_.Exception.Message }
  }
}

function Get-SerialPortDetails {
  param([string]$PortName)

  $items = @()
  try {
    $query = Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
      Where-Object { $_.Name -match '\(COM\d+\)' } |
      Select-Object Name, DeviceID, PNPClass, Manufacturer

    foreach ($entry in $query) {
      if ($entry.Name -match '\((COM\d+)\)') {
        $items += [ordered]@{
          port         = $Matches[1]
          name         = [string]$entry.Name
          manufacturer = [string]$entry.Manufacturer
          deviceId     = [string]$entry.DeviceID
          pnpClass     = [string]$entry.PNPClass
        }
      }
    }
  } catch {
    return [ordered]@{
      ok        = $false
      requested = $PortName
      detected  = @()
      error     = $_.Exception.Message
    }
  }

  $requested = $null
  if (-not [string]::IsNullOrWhiteSpace($PortName)) {
    $requested = $items | Where-Object { $_.port -eq $PortName } | Select-Object -First 1
  }

  return [ordered]@{
    ok        = $true
    requested = $PortName
    matched   = $requested
    detected  = $items
  }
}

function Read-JsonBody {
  param([System.Net.HttpListenerRequest]$Request)

  $reader = New-Object System.IO.StreamReader($Request.InputStream, $Request.ContentEncoding)
  $payload = $reader.ReadToEnd()
  $reader.Dispose()
  if ([string]::IsNullOrWhiteSpace($payload)) { return @{} }
  return ConvertTo-Hashtable -InputObject ($payload | ConvertFrom-Json)
}

function New-JsonResponse {
  param(
    [System.Net.HttpListenerResponse]$Response,
    [int]$StatusCode,
    [object]$Body
  )

  $json = $Body | ConvertTo-Json -Depth 12
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
  $Response.StatusCode = $StatusCode
  $Response.ContentType = "application/json; charset=utf-8"
  $Response.ContentEncoding = [System.Text.Encoding]::UTF8
  $Response.AddHeader("Cache-Control", "no-store")
  $Response.OutputStream.Write($bytes, 0, $bytes.Length)
  $Response.Close()
}

function New-HtmlResponse {
  param(
    [System.Net.HttpListenerResponse]$Response,
    [string]$Html
  )

  $bytes = [System.Text.Encoding]::UTF8.GetBytes($Html)
  $Response.StatusCode = 200
  $Response.ContentType = "text/html; charset=utf-8"
  $Response.ContentEncoding = [System.Text.Encoding]::UTF8
  $Response.AddHeader("Cache-Control", "no-store")
  $Response.OutputStream.Write($bytes, 0, $bytes.Length)
  $Response.Close()
}

function Invoke-ExternalCommand {
  param(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$WorkingDirectory,
    [int]$TimeoutMs = 180000
  )

  $startedAt = Get-Date
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $FilePath
  $psi.WorkingDirectory = $WorkingDirectory
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $quotedArguments = $Arguments | ForEach-Object {
    $text = [string]$_
    if ($text -match '[\s"]') {
      '"' + ($text -replace '"', '\"') + '"'
    } else {
      $text
    }
  }
  $psi.Arguments = ($quotedArguments -join " ")

  $commandText = ($FilePath + " " + (($Arguments | ForEach-Object {
        if ($_ -match "\s") { '"' + $_ + '"' } else { $_ }
      }) -join " "))

  try {
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $completed = $process.WaitForExit($TimeoutMs)
    if (-not $completed) {
      try {
        $process.Kill()
      } catch {
      }
      try {
        $process.WaitForExit(5000) | Out-Null
      } catch {
      }
      $finishedAt = Get-Date
      return [ordered]@{
        ok               = $false
        command          = $commandText
        workingDirectory = $WorkingDirectory
        exitCode         = -2
        stdout           = ""
        stderr           = "Command timed out after $TimeoutMs ms."
        startedAt        = $startedAt.ToString("s")
        finishedAt       = $finishedAt.ToString("s")
        durationMs       = [int][Math]::Round(($finishedAt - $startedAt).TotalMilliseconds)
        timedOut         = $true
      }
    }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $finishedAt = Get-Date

    return [ordered]@{
      ok               = ($process.ExitCode -eq 0)
      command          = $commandText
      workingDirectory = $WorkingDirectory
      exitCode         = $process.ExitCode
      stdout           = $stdout
      stderr           = $stderr
      startedAt        = $startedAt.ToString("s")
      finishedAt       = $finishedAt.ToString("s")
      durationMs       = [int][Math]::Round(($finishedAt - $startedAt).TotalMilliseconds)
    }
  } catch {
    $finishedAt = Get-Date
    return [ordered]@{
      ok               = $false
      command          = $commandText
      workingDirectory = $WorkingDirectory
      exitCode         = -1
      stdout           = ""
      stderr           = $_.Exception.Message
      startedAt        = $startedAt.ToString("s")
      finishedAt       = $finishedAt.ToString("s")
      durationMs       = [int][Math]::Round(($finishedAt - $startedAt).TotalMilliseconds)
    }
  }
}

function Get-AppHtml {
  Assert-PathExists -Path $script:HtmlPath -Label "HTML file"
  return [System.IO.File]::ReadAllText($script:HtmlPath, [System.Text.Encoding]::UTF8)
}
