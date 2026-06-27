[CmdletBinding()]
param(
  [int]$Port = 8787,
  [switch]$NoBrowser
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:RepoRoot = Split-Path -Parent $PSScriptRoot
$script:ConfigPath = Join-Path $PSScriptRoot "followbox-control-center.config.json"
$script:HtmlPath = Join-Path $PSScriptRoot "followbox-control-center.html"
$script:Listener = $null
$script:LastConfigNormalization = @()

function ConvertTo-BoolSafe {
  param(
    [object]$Value,
    [bool]$Default = $false
  )

  if ($null -eq $Value) { return $Default }
  if ($Value -is [bool]) { return [bool]$Value }
  $text = ([string]$Value).Trim().ToLowerInvariant()
  if ($text -in @("true", "1", "yes", "on")) { return $true }
  if ($text -in @("false", "0", "no", "off", "")) { return $false }
  return $Default
}

function ConvertTo-Hashtable {
  param([object]$InputObject)

  if ($null -eq $InputObject) { return @{} }
  if ($InputObject -is [hashtable]) { return $InputObject }
  if ($InputObject -is [string] -or $InputObject -is [ValueType]) { return $InputObject }
  if ($InputObject -is [System.Collections.IDictionary]) {
    $copy = @{}
    foreach ($key in $InputObject.Keys) {
      $copy[$key] = ConvertTo-Hashtable -InputObject $InputObject[$key]
    }
    return $copy
  }
  if ($InputObject -is [System.Collections.IEnumerable] -and -not ($InputObject -is [string])) {
    $items = @()
    foreach ($item in $InputObject) {
      $items += ,(ConvertTo-Hashtable -InputObject $item)
    }
    return $items
  }
  if ($InputObject.PSObject -and @($InputObject.PSObject.Properties).Count -gt 0) {
    $hash = @{}
    foreach ($prop in $InputObject.PSObject.Properties) {
      $hash[$prop.Name] = ConvertTo-Hashtable -InputObject $prop.Value
    }
    return $hash
  }
  return $InputObject
}

function Find-PlatformIoCommand {
  $candidates = @(
    "pio",
    (Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"),
    (Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"),
    "C:\Users\chenb\.platformio\penv\Scripts\pio.exe"
  )

  foreach ($candidate in $candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
    try {
      if ($candidate -eq "pio") {
        $null = & where.exe pio 2>$null
        if ($LASTEXITCODE -eq 0) { return "pio" }
      } elseif (Test-Path -LiteralPath $candidate) {
        return $candidate
      }
    } catch {
    }
  }

  return "pio"
}

function Get-FirmwareSourceVersion {
  param([string]$FirmwarePath)

  if ([string]::IsNullOrWhiteSpace($FirmwarePath)) { return "" }
  $header = Join-Path $FirmwarePath "include\config\ota_config.h"
  if (-not (Test-Path -LiteralPath $header)) { return "" }

  try {
    $text = Get-Content -LiteralPath $header -Raw -Encoding UTF8
    $match = [regex]::Match(
      $text,
      '^\s*#\s*define\s+FOLLOWBOX_FIRMWARE_VERSION\s+"([^"]+)"',
      [System.Text.RegularExpressions.RegexOptions]::Multiline
    )
    if ($match.Success) { return $match.Groups[1].Value.Trim() }
  } catch {
  }
  return ""
}

function Get-CloudDeviceToken {
  param([string]$FirmwarePath)

  if ([string]::IsNullOrWhiteSpace($FirmwarePath)) { return "" }
  $header = Join-Path $FirmwarePath "include\config\cloud_config.h"
  if (-not (Test-Path -LiteralPath $header)) { return "" }

  try {
    $text = Get-Content -LiteralPath $header -Raw -Encoding UTF8
    $match = [regex]::Match(
      $text,
      '^\s*#\s*define\s+FOLLOWBOX_CLOUD_DEVICE_TOKEN\s+"([^"]+)"',
      [System.Text.RegularExpressions.RegexOptions]::Multiline
    )
    if ($match.Success) { return $match.Groups[1].Value.Trim() }
  } catch {
  }
  return ""
}

function Write-Utf8NoBomFile {
  param(
    [string]$Path,
    [string]$Value
  )

  $encoding = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Get-FullPathSafe {
  param([string]$Path)

  if ([string]::IsNullOrWhiteSpace($Path)) { return "" }
  try {
    return ([System.IO.Path]::GetFullPath($Path)).TrimEnd([char[]]@("\", "/"))
  } catch {
    return ([string]$Path).Trim()
  }
}

function Test-SamePath {
  param(
    [string]$Left,
    [string]$Right
  )

  $leftFull = Get-FullPathSafe -Path $Left
  $rightFull = Get-FullPathSafe -Path $Right
  return [System.StringComparer]::OrdinalIgnoreCase.Equals($leftFull, $rightFull)
}

function Normalize-Config {
  param([hashtable]$Config)

  $changes = @()
  $requiredPaths = [ordered]@{
    repoPath          = $script:RepoRoot
    cloudPath         = (Join-Path $script:RepoRoot "cloud")
    firmwarePath      = (Join-Path $script:RepoRoot "firmware")
    cloudFirmwarePath = (Join-Path $script:RepoRoot "cloud\firmware")
  }

  foreach ($key in $requiredPaths.Keys) {
    $expected = Get-FullPathSafe -Path ([string]$requiredPaths[$key])
    $old = if ($Config.ContainsKey($key)) { [string]$Config[$key] } else { "" }
    if (-not (Test-SamePath -Left $old -Right $expected)) {
      $changes += [ordered]@{ key = $key; old = $old; new = $expected }
      $Config[$key] = $expected
    }
  }

  if (-not $Config.ContainsKey("gitRemote") -or [string]::IsNullOrWhiteSpace([string]$Config.gitRemote)) {
    $changes += [ordered]@{ key = "gitRemote"; old = [string]$Config.gitRemote; new = "origin" }
    $Config.gitRemote = "origin"
  }
  if (-not $Config.ContainsKey("gitAddPathspec") -or [string]::IsNullOrWhiteSpace([string]$Config.gitAddPathspec)) {
    $changes += [ordered]@{ key = "gitAddPathspec"; old = [string]$Config.gitAddPathspec; new = "." }
    $Config.gitAddPathspec = "."
  }
  if (-not $Config.ContainsKey("pioCommand") -or [string]::IsNullOrWhiteSpace([string]$Config.pioCommand)) {
    $pio = Find-PlatformIoCommand
    $changes += [ordered]@{ key = "pioCommand"; old = [string]$Config.pioCommand; new = $pio }
    $Config.pioCommand = $pio
  }

  $sourceToken = Get-CloudDeviceToken -FirmwarePath ([string]$Config.firmwarePath)
  if (-not [string]::IsNullOrWhiteSpace($sourceToken) -and ([string]::IsNullOrWhiteSpace([string]$Config.cloudDeviceToken))) {
    $changes += [ordered]@{ key = "cloudDeviceToken"; old = ""; new = "from firmware/include/config/cloud_config.h" }
    $Config.cloudDeviceToken = $sourceToken
  }

  $sourceVersion = Get-FirmwareSourceVersion -FirmwarePath ([string]$Config.firmwarePath)
  if (-not [string]::IsNullOrWhiteSpace($sourceVersion) -and [string]$Config.otaVersion -ne $sourceVersion) {
    $changes += [ordered]@{ key = "otaVersion"; old = [string]$Config.otaVersion; new = $sourceVersion }
    $Config.otaVersion = $sourceVersion
  }

  $script:LastConfigNormalization = $changes
  return $Config
}

function Get-DefaultConfig {
  $remoteUrl = ""
  try {
    $remoteUrl = (git -C $script:RepoRoot remote get-url origin 2>$null)
    if ($LASTEXITCODE -ne 0) { $remoteUrl = "" }
  } catch {
    $remoteUrl = ""
  }

  return [ordered]@{
    repoPath              = $script:RepoRoot
    gitRemote             = "origin"
    gitUserName           = ""
    gitUserEmail          = ""
    cloneUrl              = ($remoteUrl | Out-String).Trim()
    cloneTargetPath       = (Join-Path (Split-Path $script:RepoRoot -Parent) "follow-the-box-clone")
    gitAddPathspec        = "."
    cloudPath             = (Join-Path $script:RepoRoot "cloud")
    cloudHost             = "82.156.85.60"
    cloudPort             = 51400
    cloudUser             = "root"
    cloudPemPath          = "C:\Users\chenb\AppData\Local\Temp\vmware-chenb\VMwareDnD\29853150\codex.pem"
    cloudRemoteDir        = "/www/wwwroot/followbox-cloud"
    cloudVerifyUrl        = "https://www.boonai.cn/fb/"
    cloudVerifyDeviceId   = "followbox-001"
    cloudDeviceToken      = (Get-CloudDeviceToken -FirmwarePath (Join-Path $script:RepoRoot "firmware"))
    firmwarePath          = (Join-Path $script:RepoRoot "firmware")
    cloudFirmwarePath     = (Join-Path $script:RepoRoot "cloud\firmware")
    pioCommand            = (Find-PlatformIoCommand)
    firmwareEnv           = "esp32-s3-devkitc-1"
    otaEnv                = "ota"
    serialPort            = "COM18"
    otaUploadPort         = "192.168.4.1"
    otaVersion            = (Get-FirmwareSourceVersion -FirmwarePath (Join-Path $script:RepoRoot "firmware"))
    otaForce              = $false
    openBrowserOnStart    = $true
    defaultCommitMessage  = ("chore: deploy followbox " + (Get-Date -Format "yyyy-MM-dd HH:mm"))
  }
}

function Load-Config {
  $defaults = Get-DefaultConfig
  if (-not (Test-Path -LiteralPath $script:ConfigPath)) {
    $normalizedDefaults = Normalize-Config -Config $defaults
    Save-Config -Config $normalizedDefaults | Out-Null
    return $normalizedDefaults
  }

  $raw = Get-Content -LiteralPath $script:ConfigPath -Raw -Encoding UTF8
  if ([string]::IsNullOrWhiteSpace($raw)) {
    $normalizedDefaults = Normalize-Config -Config $defaults
    Save-Config -Config $normalizedDefaults | Out-Null
    return $normalizedDefaults
  }

  $loaded = ConvertTo-Hashtable -InputObject ($raw | ConvertFrom-Json)
  foreach ($key in $defaults.Keys) {
    if (-not $loaded.ContainsKey($key)) {
      $loaded[$key] = $defaults[$key]
    }
  }
  $normalized = Normalize-Config -Config $loaded
  if ($script:LastConfigNormalization.Count -gt 0) {
    Save-Config -Config $normalized | Out-Null
  }
  return $normalized
}

function Save-Config {
  param([hashtable]$Config)

  $json = $Config | ConvertTo-Json -Depth 6
  Write-Utf8NoBomFile -Path $script:ConfigPath -Value ($json + "`n")
  return $Config
}

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

function Get-OtaTargetBaseUrl {
  param([string]$Target)

  $trimmed = ([string]$Target).Trim().TrimEnd("/")
  if ([string]::IsNullOrWhiteSpace($trimmed)) { return "" }
  if ($trimmed -match "^https?://") { return $trimmed }
  return "http://$trimmed"
}

function Get-OtaEndpointUrl {
  param([string]$Target)

  $baseUrl = Get-OtaTargetBaseUrl -Target $Target
  if ([string]::IsNullOrWhiteSpace($baseUrl)) { return "" }
  return (Join-UrlPath -BaseUrl $baseUrl -RelativePath "api/ota/local-upload")
}

function Test-OtaHttpEndpoint {
  param([string]$Target)

  $baseUrl = Get-OtaTargetBaseUrl -Target $Target
  if ([string]::IsNullOrWhiteSpace($baseUrl)) {
    return [ordered]@{ ok = $false; url = ""; statusCode = 0; reason = "OTA target is empty." }
  }
  if (-not (Test-CommandExists -Name "curl.exe")) {
    return [ordered]@{ ok = $false; url = ""; statusCode = 0; reason = "curl.exe was not found in PATH." }
  }

  $statusUrl = Join-UrlPath -BaseUrl $baseUrl -RelativePath "api/ota/status"
  $check = Invoke-ExternalCommand -FilePath "curl.exe" -Arguments @(
    "--silent",
    "--show-error",
    "--max-time", "5",
    "--connect-timeout", "3",
    "--noproxy", "*",
    "--write-out", "`n%{http_code}",
    $statusUrl
  ) -WorkingDirectory $script:RepoRoot -TimeoutMs 10000
  $stdout = [string]$check.stdout
  $lines = @($stdout -split "`r?`n")
  $statusCode = 0
  if ($lines.Count -gt 0) {
    [void][int]::TryParse($lines[$lines.Count - 1].Trim(), [ref]$statusCode)
  }
  $body = if ($lines.Count -gt 1) { (($lines[0..($lines.Count - 2)]) -join "`n").Trim() } else { "" }
  return [ordered]@{
    ok         = ($check.exitCode -eq 0 -and $statusCode -ge 200 -and $statusCode -lt 300)
    url        = $statusUrl
    uploadUrl  = (Get-OtaEndpointUrl -Target $Target)
    statusCode = $statusCode
    reason     = if ($check.exitCode -eq 0) { "" } else { $check.stderr.Trim() }
    body       = $body
    result     = $check
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

function Get-GitCandidateFiles {
  param([hashtable]$Config)

  $pathspec = if ([string]::IsNullOrWhiteSpace([string]$Config.gitAddPathspec)) { "." } else { [string]$Config.gitAddPathspec }
  $status = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "status", "--short", "--untracked-files=all", "--", $pathspec) -WorkingDirectory $Config.repoPath
  $files = @()
  foreach ($line in ($status.stdout -split "`r?`n")) {
    $text = $line.TrimEnd()
    if ([string]::IsNullOrWhiteSpace($text)) { continue }
    $file = if ($text.Length -gt 3) { $text.Substring(3).Trim() } else { $text.Trim() }
    $files += [ordered]@{
      status = if ($text.Length -ge 2) { $text.Substring(0, 2) } else { $text }
      path   = $file
    }
  }

  return [ordered]@{
    ok       = ($status.exitCode -eq 0)
    pathspec = $pathspec
    files    = $files
    raw      = $status.stdout.Trim()
    command  = $status.command
    stderr   = $status.stderr.Trim()
  }
}

function Get-GitIdentity {
  param([hashtable]$Config)

  $repoPath = [string]$Config.repoPath
  $configuredName = [string](Get-HashtableValue -Table $Config -Key "gitUserName" -Default "")
  $configuredEmail = [string](Get-HashtableValue -Table $Config -Key "gitUserEmail" -Default "")

  $localName = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $repoPath, "config", "--get", "user.name") -WorkingDirectory $repoPath
  $localEmail = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $repoPath, "config", "--get", "user.email") -WorkingDirectory $repoPath
  $globalName = $null
  $globalEmail = $null
  $checks = @($localName, $localEmail)

  $effectiveName = ""
  $effectiveEmail = ""
  $source = ""

  if (-not [string]::IsNullOrWhiteSpace($configuredName) -and -not [string]::IsNullOrWhiteSpace($configuredEmail)) {
    $effectiveName = $configuredName.Trim()
    $effectiveEmail = $configuredEmail.Trim()
    $source = "config"
  } elseif (-not [string]::IsNullOrWhiteSpace($localName.stdout) -and -not [string]::IsNullOrWhiteSpace($localEmail.stdout)) {
    $effectiveName = $localName.stdout.Trim()
    $effectiveEmail = $localEmail.stdout.Trim()
    $source = "repo"
  } else {
    $globalName = Invoke-ExternalCommand -FilePath "git" -Arguments @("config", "--global", "--get", "user.name") -WorkingDirectory $repoPath
    $globalEmail = Invoke-ExternalCommand -FilePath "git" -Arguments @("config", "--global", "--get", "user.email") -WorkingDirectory $repoPath
    $checks += @($globalName, $globalEmail)
    if (-not [string]::IsNullOrWhiteSpace($globalName.stdout) -and -not [string]::IsNullOrWhiteSpace($globalEmail.stdout)) {
      $effectiveName = $globalName.stdout.Trim()
      $effectiveEmail = $globalEmail.stdout.Trim()
      $source = "global"
    }
  }

  $reason = ""
  $ok = $true
  if ([string]::IsNullOrWhiteSpace($effectiveName) -or [string]::IsNullOrWhiteSpace($effectiveEmail)) {
    $ok = $false
    $reason = "Git user.name or user.email is missing. Fill them in before committing."
  }

  return [ordered]@{
    ok              = $ok
    source          = $source
    effectiveName   = $effectiveName
    effectiveEmail  = $effectiveEmail
    configuredName  = $configuredName
    configuredEmail = $configuredEmail
    localName       = $localName.stdout.Trim()
    localEmail      = $localEmail.stdout.Trim()
    globalName      = if ($null -ne $globalName) { $globalName.stdout.Trim() } else { "" }
    globalEmail     = if ($null -ne $globalEmail) { $globalEmail.stdout.Trim() } else { "" }
    reason          = $reason
    checks          = $checks
  }
}

function Ensure-GitIdentity {
  param([hashtable]$Config)

  $identity = Get-GitIdentity -Config $Config
  if (-not $identity.ok) {
    return [ordered]@{
      ok       = $false
      step     = "git-identity"
      reason   = $identity.reason
      identity = $identity
      steps    = @($identity.checks)
    }
  }

  $steps = @($identity.checks)
  if ($identity.source -eq "config") {
    $setName = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "config", "user.name", $identity.effectiveName) -WorkingDirectory $Config.repoPath
    $setEmail = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "config", "user.email", $identity.effectiveEmail) -WorkingDirectory $Config.repoPath
    $steps += @($setName, $setEmail)
    if ($setName.exitCode -ne 0 -or $setEmail.exitCode -ne 0) {
      return [ordered]@{
        ok       = $false
        step     = "git-identity"
        reason   = "Failed to write repository-local git identity."
        identity = $identity
        steps    = $steps
      }
    }
  }

  $verified = Get-GitIdentity -Config $Config
  $steps += @($verified.checks)
  if (-not $verified.ok) {
    return [ordered]@{
      ok       = $false
      step     = "git-identity"
      reason   = $verified.reason
      identity = $verified
      steps    = $steps
    }
  }

  return [ordered]@{
    ok       = $true
    step     = "git-identity"
    identity = $verified
    steps    = $steps
  }
}

function Get-GitBranchInfo {
  param([hashtable]$Config)

  $branch = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "branch", "--show-current") -WorkingDirectory $Config.repoPath
  if ($branch.exitCode -ne 0 -or [string]::IsNullOrWhiteSpace($branch.stdout)) {
    return [ordered]@{
      ok     = $false
      step   = "git-branch"
      reason = "Cannot determine the current git branch."
      branch = ""
      steps  = @($branch)
    }
  }

  return [ordered]@{
    ok     = $true
    step   = "git-branch"
    reason = ""
    branch = $branch.stdout.Trim()
    steps  = @($branch)
  }
}

function Get-GitWorkingTreeChanges {
  param([hashtable]$Config)

  $status = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "status", "--porcelain", "--untracked-files=all") -WorkingDirectory $Config.repoPath
  $files = @()
  foreach ($line in ($status.stdout -split "`r?`n")) {
    $text = $line.TrimEnd()
    if ([string]::IsNullOrWhiteSpace($text)) { continue }
    $file = if ($text.Length -gt 3) { $text.Substring(3).Trim() } else { $text.Trim() }
    $files += [ordered]@{
      status = if ($text.Length -ge 2) { $text.Substring(0, 2) } else { $text }
      path   = $file
    }
  }

  return [ordered]@{
    ok      = ($status.exitCode -eq 0)
    clean   = ($status.exitCode -eq 0 -and $files.Count -eq 0)
    files   = $files
    raw     = $status.stdout.Trim()
    command = $status.command
    stderr  = $status.stderr.Trim()
  }
}

function Get-GitUnpushedCommitCount {
  param(
    [hashtable]$Config,
    [string]$Branch
  )

  $upstream = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}") -WorkingDirectory $Config.repoPath
  if ($upstream.exitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($upstream.stdout)) {
    $range = "$($upstream.stdout.Trim())..HEAD"
  } else {
    $range = "$($Config.gitRemote)/$Branch..HEAD"
  }

  $count = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "rev-list", "--count", $range) -WorkingDirectory $Config.repoPath
  $value = 0
  $validCount = ($count.exitCode -eq 0 -and [int]::TryParse($count.stdout.Trim(), [ref]$value))
  return [ordered]@{
    ok    = $validCount
    count = if ($validCount) { $value } else { 0 }
    range = $range
    steps = @($upstream, $count)
  }
}

function Get-GitRemoteHead {
  param([hashtable]$Config)

  $branch = Get-GitBranchInfo -Config $Config
  if (-not $branch.ok) {
    return [ordered]@{
      ok     = $false
      step   = "git-verify"
      reason = $branch.reason
      branch = $branch.branch
      steps  = $branch.steps
    }
  }

  $branchName = $branch.branch
  $localHead = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "rev-parse", "HEAD") -WorkingDirectory $Config.repoPath
  $remoteHead = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "ls-remote", "--heads", $Config.gitRemote, $branchName) -WorkingDirectory $Config.repoPath
  $steps = @()
  $steps += $branch.steps
  $steps += @($localHead, $remoteHead)

  if ($localHead.exitCode -ne 0 -or $remoteHead.exitCode -ne 0) {
    return [ordered]@{
      ok         = $false
      step       = "git-verify"
      reason     = "Failed to read local or remote HEAD."
      branch     = $branchName
      localHead  = $localHead.stdout.Trim()
      remoteHead = $remoteHead.stdout.Trim()
      steps      = $steps
    }
  }

  $localSha = $localHead.stdout.Trim()
  $remoteSha = ""
  foreach ($line in ($remoteHead.stdout -split "`r?`n")) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split "\s+"
    if ($parts.Count -ge 1) {
      $remoteSha = $parts[0].Trim()
      break
    }
  }

  if ([string]::IsNullOrWhiteSpace($remoteSha)) {
    return [ordered]@{
      ok         = $false
      step       = "git-verify"
      reason     = "Remote branch HEAD was not returned by ls-remote."
      branch     = $branchName
      localHead  = $localSha
      remoteHead = ""
      steps      = $steps
    }
  }

  $ok = ($localSha -eq $remoteSha)
  return [ordered]@{
    ok         = $ok
    step       = "git-verify"
    reason     = if ($ok) { "" } else { "Remote HEAD does not match local HEAD after push." }
    branch     = $branchName
    localHead  = $localSha
    remoteHead = $remoteSha
    steps      = $steps
  }
}
function Get-PreflightData {
  param(
    [hashtable]$Config,
    [hashtable]$Body
  )

  $action = if ($Body.ContainsKey("action")) { [string]$Body.action } else { "" }
  if ([string]::IsNullOrWhiteSpace($action)) {
    throw "action is required"
  }

  $warnings = @()
  $checks = @()
  $summary = ""
  $details = [ordered]@{}

  switch ($action) {
    "git-commit-push" {
      $git = Get-GitCandidateFiles -Config $Config
      $identity = Get-GitIdentity -Config $Config
      $branch = Get-GitBranchInfo -Config $Config
      $unpushed = if ($branch.ok) { Get-GitUnpushedCommitCount -Config $Config -Branch $branch.branch } else { [ordered]@{ ok = $false; count = 0; range = ""; steps = $branch.steps } }
      $details.git = $git
      $details.identity = $identity
      $details.unpushed = $unpushed
      $summary = "Will run git add / commit / push."
      $checks += [ordered]@{ label = "Repo path exists"; ok = (Test-ExistingPath -Path $Config.repoPath) }
      $checks += [ordered]@{ label = "Changes or unpushed commits found"; ok = ($git.ok -and ($git.files.Count -gt 0 -or ($unpushed.ok -and $unpushed.count -gt 0))); value = "$($git.files.Count) files, $($unpushed.count) commits" }
      $checks += [ordered]@{ label = "Git identity ready"; ok = [bool]$identity.ok; value = if ($identity.ok) { "$($identity.effectiveName) <$($identity.effectiveEmail)>" } else { $identity.reason } }
      if ($git.pathspec -eq "." -or $git.pathspec -eq "*") {
        $warnings += "Current gitAddPathspec covers the whole repo."
      }
      if ($git.files.Count -gt 20) {
        $warnings += "Many files are included. Check that unrelated changes are not being pushed."
      }
      if ($git.files.Count -eq 0 -and $unpushed.count -gt 0) {
        $warnings += "No new files need committing; existing unpushed commits will be pushed."
      }
    }
    "git-pull-local" {
      $branch = Get-GitBranchInfo -Config $Config
      $workingTree = Get-GitWorkingTreeChanges -Config $Config
      $details.branch = $branch.branch
      $details.git = $workingTree
      $summary = "Will fetch the current branch and pull with --ff-only into the local checkout."
      $checks += [ordered]@{ label = "Repo path exists"; ok = (Test-ExistingPath -Path $Config.repoPath) }
      $checks += [ordered]@{ label = "Current branch detected"; ok = [bool]$branch.ok; value = if ($branch.ok) { $branch.branch } else { $branch.reason } }
      $checks += [ordered]@{ label = "Working tree clean"; ok = [bool]$workingTree.clean; value = if ($workingTree.clean) { "clean" } else { [string]$workingTree.files.Count } }
      if (-not $workingTree.clean -and $workingTree.files.Count -gt 0) {
        $warnings += "Pull is blocked while local changes are present. Commit, stash, or discard them first."
      }
    }
    "cloud-deploy" {
      $verifyUrl = [string]$Config.cloudVerifyUrl
      $versionUrl = Join-UrlPath -BaseUrl $verifyUrl -RelativePath "deploy-version.txt"
      $details.cloud = [ordered]@{
        host       = $Config.cloudHost
        port       = $Config.cloudPort
        remoteDir  = $Config.cloudRemoteDir
        verifyUrl  = $verifyUrl
        versionUrl = $versionUrl
      }
      $summary = "Will upload cloud artifacts and restart PM2/nginx."
      $checks += [ordered]@{ label = "Cloud path exists"; ok = (Test-ExistingPath -Path $Config.cloudPath) }
      $checks += [ordered]@{ label = "SSH key exists"; ok = (Test-ExistingPath -Path $Config.cloudPemPath) }
      $checks += [ordered]@{ label = "ssh command available"; ok = (Test-CommandExists -Name "ssh") }
      $checks += [ordered]@{ label = "scp command available"; ok = (Test-CommandExists -Name "scp") }
      if ([string]::IsNullOrWhiteSpace([string]$Config.cloudVerifyUrl)) {
        $warnings += "No cloud verify URL is configured."
      }
    }
    "deploy-cloud-all" {
      $git = Get-GitCandidateFiles -Config $Config
      $identity = Get-GitIdentity -Config $Config
      $details.git = $git
      $details.identity = $identity
      $details.cloud = [ordered]@{
        host      = $Config.cloudHost
        port      = $Config.cloudPort
        remoteDir = $Config.cloudRemoteDir
        verifyUrl = $Config.cloudVerifyUrl
      }
      $summary = "Will commit/push first, then deploy cloud."
      $checks += [ordered]@{ label = "Candidate files found"; ok = ($git.ok -and $git.files.Count -gt 0); value = [string]$git.files.Count }
      $checks += [ordered]@{ label = "Git identity ready"; ok = [bool]$identity.ok; value = if ($identity.ok) { "$($identity.effectiveName) <$($identity.effectiveEmail)>" } else { $identity.reason } }
      $checks += [ordered]@{ label = "Cloud path exists"; ok = (Test-ExistingPath -Path $Config.cloudPath) }
      $checks += [ordered]@{ label = "SSH key exists"; ok = (Test-ExistingPath -Path $Config.cloudPemPath) }
      if ($git.pathspec -eq "." -or $git.pathspec -eq "*") {
        $warnings += "Deploy-all will commit the full pathspec scope first."
      }
    }
    "ota-publish-cloud" {
      $targetHost = try { ([uri]$Config.cloudVerifyUrl).Host } catch { "" }
      $otaVersion = [string](Get-HashtableValue -Table $Body -Key "otaVersion" -Default $Config.otaVersion)
      $sourceVersion = Get-FirmwareSourceVersion -FirmwarePath ([string]$Config.firmwarePath)
      $pio = Resolve-PlatformIoCommand -Command ([string]$Config.pioCommand)
      $details.ota = [ordered]@{
        firmwareEnv      = $Config.firmwareEnv
        otaVersion       = $otaVersion
        sourceVersion    = $sourceVersion
        cloudFirmwareDir = $Config.cloudFirmwarePath
        remoteFirmware   = "$($Config.cloudRemoteDir)/firmware"
        verifyHost       = $targetHost
        pioCommand       = $pio.command
        pioResolvedPath  = $pio.resolvedPath
        pioReason        = $pio.reason
      }
      $summary = "Will build OTA package locally and upload it to cloud/firmware."
      $checks += [ordered]@{ label = "Firmware path exists"; ok = (Test-ExistingPath -Path $Config.firmwarePath) }
      $checks += [ordered]@{ label = "cloud/firmware path exists"; ok = (Test-ExistingPath -Path $Config.cloudFirmwarePath) }
      $checks += [ordered]@{ label = "SSH key exists"; ok = (Test-ExistingPath -Path $Config.cloudPemPath) }
      $checks += [ordered]@{ label = "PlatformIO available"; ok = [bool]$pio.ok; value = if ($pio.ok) { $pio.resolvedPath } else { $pio.reason } }
      $checks += [ordered]@{ label = "Firmware source version detected"; ok = (-not [string]::IsNullOrWhiteSpace($sourceVersion)); value = $sourceVersion }
      $checks += [ordered]@{ label = "OTA version matches firmware source"; ok = (-not [string]::IsNullOrWhiteSpace($sourceVersion) -and $otaVersion -eq $sourceVersion); value = "$otaVersion / $sourceVersion" }
      if ([string]::IsNullOrWhiteSpace($otaVersion)) {
        $warnings += "No OTA version was provided."
      } elseif (-not [string]::IsNullOrWhiteSpace($sourceVersion) -and $otaVersion -ne $sourceVersion) {
        $warnings += "OTA version must match firmware/include/config/ota_config.h so H5 and firmware report the same release."
      }
    }
    "upload-serial" {
      $serial = Get-SerialPortDetails -PortName ([string]$Config.serialPort)
      $pio = Resolve-PlatformIoCommand -Command ([string]$Config.pioCommand)
      $details.serial = $serial
      $details.pio = $pio
      $summary = "Will flash the board over the serial port."
      $checks += [ordered]@{ label = "Serial port configured"; ok = (-not [string]::IsNullOrWhiteSpace([string]$Config.serialPort)); value = [string]$Config.serialPort }
      $checks += [ordered]@{ label = "PlatformIO available"; ok = [bool]$pio.ok; value = if ($pio.ok) { $pio.resolvedPath } else { $pio.reason } }
      $checks += [ordered]@{ label = "Target serial port detected"; ok = ($serial.ok -and $null -ne $serial.matched); value = if ($serial.matched) { $serial.matched.name } else { "" } }
      $warnings += "Confirm wheels are lifted and the drive chain is safe before serial flashing."
    }
    "upload-network" {
      $endpoint = Test-OtaHttpEndpoint -Target ([string]$Config.otaUploadPort)
      $pio = Resolve-PlatformIoCommand -Command ([string]$Config.pioCommand)
      $details.network = [ordered]@{
        target       = $Config.otaUploadPort
        uploadMethod = "http-local-upload"
        statusUrl    = $endpoint.url
        uploadUrl    = $endpoint.uploadUrl
      }
      $details.endpoint = $endpoint
      $details.pio = $pio
      $summary = "Will build firmware and upload it directly to the board HTTP OTA endpoint."
      $checks += [ordered]@{ label = "OTA target configured"; ok = (-not [string]::IsNullOrWhiteSpace([string]$Config.otaUploadPort)); value = [string]$Config.otaUploadPort }
      $checks += [ordered]@{ label = "Board HTTP OTA endpoint reachable"; ok = [bool]$endpoint.ok; value = if ($endpoint.ok) { $endpoint.url } else { $endpoint.reason } }
      $checks += [ordered]@{ label = "PlatformIO available"; ok = [bool]$pio.ok; value = if ($pio.ok) { $pio.resolvedPath } else { $pio.reason } }
      $checks += [ordered]@{ label = "curl.exe available"; ok = (Test-CommandExists -Name "curl.exe") }
      $warnings += "Confirm the target IP belongs to the intended board before OTA."
      $warnings += "This VM-safe path uses board HTTP upload; it requires firmware that exposes /api/ota/local-upload."
      $warnings += "This updates the app partition only; LittleFS web assets still require uploadfs when they change."
    }
    default {
      throw "Unsupported action: $action"
    }
  }

  $blocking = @($checks | Where-Object { -not $_.ok })
  return [ordered]@{
    ok       = ($blocking.Count -eq 0)
    action   = $action
    summary  = $summary
    checks   = $checks
    warnings = $warnings
    details  = $details
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

function Get-SshArgumentPrefix {
  param(
    [hashtable]$Config,
    [ValidateSet("ssh", "scp")]
    [string]$Tool = "ssh"
  )

  $portFlag = if ($Tool -eq "scp") { "-P" } else { "-p" }

  return @(
    "-o", "BatchMode=yes",
    "-o", "IdentitiesOnly=yes",
    "-o", "ConnectTimeout=10",
    "-o", "ServerAliveInterval=10",
    "-o", "ServerAliveCountMax=2",
    "-o", "PubkeyAcceptedAlgorithms=+ssh-rsa",
    "-o", "HostkeyAlgorithms=+ssh-rsa",
    "-i", $Config.cloudPemPath,
    $portFlag, [string]$Config.cloudPort
  )
}

function Get-GitStatusData {
  param([hashtable]$Config)

  Assert-PathExists -Path $Config.repoPath -Label "Repo path"
  $branch = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "branch", "--show-current") -WorkingDirectory $Config.repoPath
  $status = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "status", "--short", "--branch") -WorkingDirectory $Config.repoPath
  $remote = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "remote", "-v") -WorkingDirectory $Config.repoPath
  $identity = Get-GitIdentity -Config $Config
  return [ordered]@{
    branch   = $branch.stdout.Trim()
    status   = $status.stdout.Trim()
    remote   = $remote.stdout.Trim()
    identity = $identity
  }
}

function Get-HealthData {
  param([hashtable]$Config)

  $pio = Resolve-PlatformIoCommand -Command ([string]$Config.pioCommand)
  $identity = Get-GitIdentity -Config $Config

  return [ordered]@{
    repoExists      = (Test-ExistingPath -Path $Config.repoPath)
    cloudExists     = (Test-ExistingPath -Path $Config.cloudPath)
    firmwareExists  = (Test-ExistingPath -Path $Config.firmwarePath)
    pemExists       = (Test-ExistingPath -Path $Config.cloudPemPath)
    pioExists       = [bool]$pio.ok
    pioCommand      = $pio.command
    pioResolvedPath = $pio.resolvedPath
    pioReason       = $pio.reason
    gitUserName     = $identity.effectiveName
    gitUserEmail    = $identity.effectiveEmail
    serialPort      = $Config.serialPort
    otaUploadPort   = $Config.otaUploadPort
  }
}

function Publish-OtaFilesLocal {
  param(
    [hashtable]$Config,
    [hashtable]$Body
  )

  Assert-PathExists -Path $Config.firmwarePath -Label "Firmware path"
  Assert-PathExists -Path $Config.cloudFirmwarePath -Label "Cloud firmware path"

  $envName = if ($Body.ContainsKey("firmwareEnv") -and $Body.firmwareEnv) { [string]$Body.firmwareEnv } else { [string]$Config.firmwareEnv }
  $version = if ($Body.ContainsKey("otaVersion") -and $Body.otaVersion) { [string]$Body.otaVersion } else { [string]$Config.otaVersion }
  $force = if ($Body.ContainsKey("otaForce")) { ConvertTo-BoolSafe -Value $Body.otaForce -Default $false } else { ConvertTo-BoolSafe -Value $Config.otaForce -Default $false }
  $pioCommand = if ($Body.ContainsKey("pioCommand") -and $Body.pioCommand) { [string]$Body.pioCommand } else { [string]$Config.pioCommand }
  $sourceVersion = Get-FirmwareSourceVersion -FirmwarePath ([string]$Config.firmwarePath)
  if ([string]::IsNullOrWhiteSpace($sourceVersion)) {
    return [ordered]@{
      ok     = $false
      step   = "ota-version"
      reason = "FOLLOWBOX_FIRMWARE_VERSION was not found in firmware/include/config/ota_config.h."
    }
  }
  if ([string]::IsNullOrWhiteSpace($version)) {
    $version = $sourceVersion
  }
  if ($version -ne $sourceVersion) {
    return [ordered]@{
      ok            = $false
      step          = "ota-version"
      reason        = "otaVersion must match FOLLOWBOX_FIRMWARE_VERSION before publishing."
      otaVersion    = $version
      sourceVersion = $sourceVersion
    }
  }
  $pio = Resolve-PlatformIoCommand -Command $pioCommand
  if (-not $pio.ok) {
    return [ordered]@{
      ok     = $false
      step   = "platformio"
      reason = $pio.reason
      pio    = $pio
    }
  }

  $build = Invoke-ExternalCommand -FilePath $pioCommand -Arguments @("run", "-d", $Config.firmwarePath, "-e", $envName) -WorkingDirectory $Config.firmwarePath
  if ($build.exitCode -ne 0) {
    return [ordered]@{ ok = $false; step = "build"; result = $build }
  }

  $binPath = Join-Path $Config.firmwarePath ".pio\build\$envName\firmware.bin"
  Assert-PathExists -Path $binPath -Label "Built firmware"

  $targetBin = Join-Path $Config.cloudFirmwarePath "firmware.bin"
  Copy-Item -LiteralPath $binPath -Destination $targetBin -Force

  $hash = Get-FileHash -LiteralPath $targetBin -Algorithm MD5
  $item = Get-Item -LiteralPath $targetBin
  $manifestPath = Join-Path $Config.cloudFirmwarePath "manifest.json"
  $existingNotes = ""
  if (Test-Path -LiteralPath $manifestPath) {
    try {
      $existingManifest = ConvertTo-Hashtable -InputObject ((Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8) | ConvertFrom-Json)
      $existingNotes = [string](Get-HashtableValue -Table $existingManifest -Key "notes" -Default "")
    } catch {
      $existingNotes = ""
    }
  }
  $notes = if ($Body.ContainsKey("otaNotes") -and -not [string]::IsNullOrWhiteSpace([string]$Body.otaNotes)) {
    [string]$Body.otaNotes
  } else {
    $existingNotes
  }
  $manifest = [ordered]@{
    version = $version
    file    = "firmware.bin"
    md5     = $hash.Hash.ToLowerInvariant()
    size    = [int64]$item.Length
    force   = $force
  }
  if (-not [string]::IsNullOrWhiteSpace($notes)) {
    $manifest.notes = $notes
  }
  Write-Utf8NoBomFile -Path $manifestPath -Value (($manifest | ConvertTo-Json -Depth 4) + "`n")

  return [ordered]@{
    ok            = $true
    step          = "publish-local"
    manifest      = $manifest
    sourceVersion = $sourceVersion
    build         = $build
    binPath       = $targetBin
  }
}

function Invoke-LanHttpOtaUpload {
  param(
    [hashtable]$Config,
    [hashtable]$Body
  )

  if ([string]::IsNullOrWhiteSpace([string]$Config.otaUploadPort)) {
    return [ordered]@{
      ok     = $false
      step   = "ota-target"
      reason = "otaUploadPort is required."
    }
  }

  $pio = Resolve-PlatformIoCommand -Command ([string]$Config.pioCommand)
  if (-not $pio.ok) {
    return [ordered]@{
      ok     = $false
      step   = "platformio"
      reason = $pio.reason
      pio    = $pio
    }
  }

  if (-not (Test-CommandExists -Name "curl.exe")) {
    return [ordered]@{
      ok     = $false
      step   = "curl"
      reason = "curl.exe was not found in PATH."
    }
  }

  Assert-PathExists -Path $Config.firmwarePath -Label "Firmware path"
  $envName = if ($Body.ContainsKey("otaEnv") -and $Body.otaEnv) {
    [string]$Body.otaEnv
  } else {
    [string]$Config.otaEnv
  }
  if ([string]::IsNullOrWhiteSpace($envName)) {
    return [ordered]@{
      ok     = $false
      step   = "ota-env"
      reason = "otaEnv is required."
    }
  }

  $endpoint = Test-OtaHttpEndpoint -Target ([string]$Config.otaUploadPort)
  if (-not $endpoint.ok) {
    return [ordered]@{
      ok       = $false
      step     = "ota-http-endpoint"
      reason   = "The board HTTP OTA endpoint is not reachable. Install a firmware build that exposes /api/ota/local-upload, or put the VM and board on the same reachable LAN."
      endpoint = $endpoint
    }
  }

  $build = Invoke-ExternalCommand -FilePath $pio.command -Arguments @(
    "run", "-d", $Config.firmwarePath, "-e", $envName
  ) -WorkingDirectory $Config.firmwarePath -TimeoutMs 300000
  if ($build.exitCode -ne 0) {
    return [ordered]@{
      ok       = $false
      step     = "build"
      reason   = "PlatformIO build failed."
      endpoint = $endpoint
      result   = $build
    }
  }

  $binPath = Join-Path $Config.firmwarePath ".pio\build\$envName\firmware.bin"
  Assert-PathExists -Path $binPath -Label "Built firmware"

  $uploadUrl = Get-OtaEndpointUrl -Target ([string]$Config.otaUploadPort)
  $arguments = @(
    "--fail",
    "--show-error",
    "--silent",
    "--max-time", "240",
    "--connect-timeout", "10",
    "--noproxy", "*",
    "-H", "Expect:",
    "-F", "firmware=@$binPath;filename=firmware.bin;type=application/octet-stream"
  )
  $localApiKey = [string](Get-HashtableValue -Table $Config -Key "localApiKey" -Default "")
  if (-not [string]::IsNullOrWhiteSpace($localApiKey)) {
    $arguments += @("-H", "X-FollowBox-Key: $localApiKey")
  }
  $arguments += $uploadUrl

  $upload = Invoke-ExternalCommand -FilePath "curl.exe" -Arguments $arguments -WorkingDirectory $Config.firmwarePath -TimeoutMs 300000
  return [ordered]@{
    ok           = ($upload.exitCode -eq 0)
    step         = if ($upload.exitCode -eq 0) { "uploaded" } else { "upload" }
    reason       = if ($upload.exitCode -eq 0) { "Board accepted the HTTP OTA upload and should reboot." } else { "HTTP OTA upload failed." }
    target       = [string]$Config.otaUploadPort
    endpoint     = $endpoint
    firmwareBin  = $binPath
    uploadMethod = "http-local-upload"
    build        = $build
    result       = $upload
  }
}

function Invoke-CloudDeploy {
  param([hashtable]$Config)

  Assert-PathExists -Path $Config.cloudPath -Label "Cloud path"
  Assert-PathExists -Path $Config.cloudPemPath -Label "SSH key"

  $sshPrefix = Get-SshArgumentPrefix -Config $Config -Tool "ssh"
  $scpPrefix = Get-SshArgumentPrefix -Config $Config -Tool "scp"
  $remote = "$($Config.cloudUser)@$($Config.cloudHost)"
  $remoteDir = [string]$Config.cloudRemoteDir

  $mkdir = Invoke-ExternalCommand -FilePath "ssh" -Arguments ($sshPrefix + @($remote, "mkdir -p '$remoteDir/public' '$remoteDir/firmware'")) -WorkingDirectory $Config.cloudPath -TimeoutMs 30000
  $backend = Invoke-ExternalCommand -FilePath "scp" -Arguments ($scpPrefix + @(
        (Join-Path $Config.cloudPath "server.js"),
        (Join-Path $Config.cloudPath "package.json"),
        (Join-Path $Config.cloudPath "deploy-clean-cache.sh"),
        (Join-Path $Config.cloudPath "followbox-nginx.conf"),
        "$remote`:$remoteDir/"
      )) -WorkingDirectory $Config.cloudPath -TimeoutMs 180000
  $public = Invoke-ExternalCommand -FilePath "scp" -Arguments ($scpPrefix + @("-r", (Join-Path $Config.cloudPath "public"), "$remote`:$remoteDir/")) -WorkingDirectory $Config.cloudPath -TimeoutMs 180000
  $firmware = Invoke-ExternalCommand -FilePath "scp" -Arguments ($scpPrefix + @("-r", (Join-Path $Config.cloudPath "firmware"), "$remote`:$remoteDir/")) -WorkingDirectory $Config.cloudPath -TimeoutMs 180000
  $restart = Invoke-ExternalCommand -FilePath "ssh" -Arguments ($sshPrefix + @($remote, "cd '$remoteDir' && chmod +x deploy-clean-cache.sh && npm install && bash ./deploy-clean-cache.sh '$remoteDir'")) -WorkingDirectory $Config.cloudPath -TimeoutMs 300000
  $remoteVerify = Invoke-ExternalCommand -FilePath "ssh" -Arguments ($sshPrefix + @($remote, "cd '$remoteDir' && test -f server.js && test -f public/index.html && test -f public/deploy-version.txt && test -f firmware/manifest.json && (pm2 list 2>/dev/null | grep -i followbox-cloud || true)")) -WorkingDirectory $Config.cloudPath -TimeoutMs 60000
  $remoteHttpVerifyCommand = "cd '$remoteDir' && node -e `"require('http').get('http://127.0.0.1:8080/', (res) => { console.log('HTTP ' + res.statusCode); process.exit(res.statusCode === 200 ? 0 : 1); }).on('error', (err) => { console.error(err.message); process.exit(1); });`""
  $remoteHttpVerify = Invoke-ExternalCommand -FilePath "ssh" -Arguments ($sshPrefix + @($remote, $remoteHttpVerifyCommand)) -WorkingDirectory $Config.cloudPath -TimeoutMs 60000

  $verify = [ordered]@{
    mode             = "ssh-primary"
    remoteLoopback   = if ($remoteHttpVerify.exitCode -eq 0) { ($remoteHttpVerify.stdout | Out-String).Trim() } else { ($remoteHttpVerify.stderr | Out-String).Trim() }
    publicApp        = ""
    publicDeployStamp = ""
    note             = "Deployment success is decided by SSH-side verification so public IP allow-lists do not block validation."
  }
  try {
    $response = Invoke-WebRequest -Uri $Config.cloudVerifyUrl -UseBasicParsing -Method Get -TimeoutSec 15
    $verify.publicApp = "HTTP $($response.StatusCode)"
  } catch {
    $verify.publicApp = "Skipped or blocked by external access policy: $($_.Exception.Message)"
  }

  try {
    $versionResponse = Invoke-WebRequest -Uri (Join-UrlPath -BaseUrl $Config.cloudVerifyUrl -RelativePath "deploy-version.txt") -UseBasicParsing -Method Get -TimeoutSec 15
    $verify.publicDeployStamp = Get-WebResponseText -Response $versionResponse
  } catch {
    $verify.publicDeployStamp = "Skipped or blocked by external access policy: $($_.Exception.Message)"
  }

  return [ordered]@{
    ok     = ($mkdir.exitCode -eq 0 -and $backend.exitCode -eq 0 -and $public.exitCode -eq 0 -and $firmware.exitCode -eq 0 -and $restart.exitCode -eq 0 -and $remoteVerify.exitCode -eq 0 -and $remoteHttpVerify.exitCode -eq 0)
    verify = $verify
    steps  = @($mkdir, $backend, $public, $firmware, $restart, $remoteVerify, $remoteHttpVerify)
  }
}

function Invoke-OtaPublicVerify {
  param(
    [hashtable]$Config,
    [object]$Manifest
  )

  $manifestVersion = [string]$Manifest["version"]
  $manifestMd5 = [string]$Manifest["md5"]
  $manifestSize = [int64]$Manifest["size"]
  $baseUrl = [string]$Config.cloudVerifyUrl
  $deviceId = [string](Get-HashtableValue -Table $Config -Key "cloudVerifyDeviceId" -Default "followbox-001")
  $token = [string](Get-HashtableValue -Table $Config -Key "cloudDeviceToken" -Default "")
  if ([string]::IsNullOrWhiteSpace($baseUrl) -or [string]::IsNullOrWhiteSpace($deviceId) -or [string]::IsNullOrWhiteSpace($token)) {
    return [ordered]@{
      ok      = $true
      skipped = $true
      reason  = "cloudVerifyUrl, cloudVerifyDeviceId, or cloudDeviceToken is not configured."
      steps   = @()
    }
  }

  $encodedDevice = ConvertTo-UrlComponent -Value $deviceId
  $encodedToken = ConvertTo-UrlComponent -Value $token
  $encodedCurrent = ConvertTo-UrlComponent -Value "control-center-verify"
  $encodedVersion = ConvertTo-UrlComponent -Value $manifestVersion
  $versionUrl = (Join-UrlPath -BaseUrl $baseUrl -RelativePath "api/device/$encodedDevice/firmware/version") + "?token=$encodedToken&current=$encodedCurrent"
  $downloadUrl = (Join-UrlPath -BaseUrl $baseUrl -RelativePath "api/device/$encodedDevice/firmware/download") + "?token=$encodedToken&version=$encodedVersion"

  $steps = @()
  $version = Invoke-ExternalCommand -FilePath "curl.exe" -Arguments @("--fail", "--silent", "--show-error", "--max-time", "15", $versionUrl) -WorkingDirectory $Config.cloudPath
  $steps += $version
  $summary = $null
  $versionOk = $false
  if ($version.exitCode -eq 0) {
    try {
      $summary = ConvertTo-Hashtable -InputObject ($version.stdout | ConvertFrom-Json)
      $versionOk = (
        [string]$summary.available_version -eq $manifestVersion -and
        [string]$summary.md5 -eq $manifestMd5 -and
        [int64]$summary.size -eq $manifestSize
      )
    } catch {
      $versionOk = $false
    }
  }

  $tmp = Join-Path $env:TEMP ("followbox-ota-verify-" + [guid]::NewGuid().ToString("N") + ".bin")
  $download = Invoke-ExternalCommand -FilePath "curl.exe" -Arguments @("--fail", "--silent", "--show-error", "--location", "--max-time", "45", "-o", $tmp, $downloadUrl) -WorkingDirectory $Config.cloudPath
  $steps += $download
  $downloadOk = $false
  $downloadMd5 = ""
  $downloadSize = 0
  try {
    if ($download.exitCode -eq 0 -and (Test-Path -LiteralPath $tmp)) {
      $downloadMd5 = (Get-FileHash -LiteralPath $tmp -Algorithm MD5).Hash.ToLowerInvariant()
      $downloadSize = [int64](Get-Item -LiteralPath $tmp).Length
      $downloadOk = ($downloadMd5 -eq $manifestMd5 -and $downloadSize -eq $manifestSize)
    }
  } finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
  }

  return [ordered]@{
    ok           = ($versionOk -and $downloadOk)
    skipped      = $false
    versionUrl   = $versionUrl
    downloadUrl  = $downloadUrl
    versionOk    = $versionOk
    downloadOk   = $downloadOk
    summary      = $summary
    downloadMd5  = $downloadMd5
    downloadSize = $downloadSize
    steps        = $steps
  }
}

function Invoke-OtaPublishCloud {
  param(
    [hashtable]$Config,
    [hashtable]$Body
  )

  $local = Publish-OtaFilesLocal -Config $Config -Body $Body
  if (-not $local.ok) { return $local }

  Assert-PathExists -Path $Config.cloudPemPath -Label "SSH key"
  $sshPrefix = Get-SshArgumentPrefix -Config $Config -Tool "ssh"
  $scpPrefix = Get-SshArgumentPrefix -Config $Config -Tool "scp"
  $remote = "$($Config.cloudUser)@$($Config.cloudHost)"
  $remoteDir = "$($Config.cloudRemoteDir)/firmware"

  $mkdir = Invoke-ExternalCommand -FilePath "ssh" -Arguments ($sshPrefix + @($remote, "mkdir -p '$remoteDir'")) -WorkingDirectory $Config.cloudPath -TimeoutMs 30000
  $upload = Invoke-ExternalCommand -FilePath "scp" -Arguments ($scpPrefix + @(
        (Join-Path $Config.cloudFirmwarePath "firmware.bin"),
        (Join-Path $Config.cloudFirmwarePath "manifest.json"),
        "$remote`:$remoteDir/"
      )) -WorkingDirectory $Config.cloudPath -TimeoutMs 180000

  $manifest = $local.manifest
  $manifestVersion = [string]$manifest["version"]
  $manifestMd5 = [string]$manifest["md5"]
  $manifestSize = [int64]$manifest["size"]
  $remoteVerifyScript = "cd '$remoteDir' && node -e `"const fs=require('fs'),crypto=require('crypto');const expected={version:process.argv[1],md5:process.argv[2],size:Number(process.argv[3])};const m=JSON.parse(fs.readFileSync('manifest.json','utf8').replace(/^\uFEFF/,''));const file=m.file||'firmware.bin';const b=fs.readFileSync(file);const md5=crypto.createHash('md5').update(b).digest('hex');const result={version:String(m.version||''),declaredMd5:String(m.md5||'').toLowerCase(),declaredSize:Number(m.size||0),actualMd5:md5,actualSize:b.length,ok:String(m.version||'')===expected.version&&String(m.md5||'').toLowerCase()===expected.md5&&Number(m.size||0)===expected.size&&md5===expected.md5&&b.length===expected.size};console.log(JSON.stringify(result));process.exit(result.ok?0:1);`" '$manifestVersion' '$manifestMd5' '$manifestSize'"
  $remoteVerify = if ($mkdir.exitCode -eq 0 -and $upload.exitCode -eq 0) {
    Invoke-ExternalCommand -FilePath "ssh" -Arguments ($sshPrefix + @($remote, $remoteVerifyScript)) -WorkingDirectory $Config.cloudPath -TimeoutMs 60000
  } else {
    $null
  }
  $publicVerify = if ($null -ne $remoteVerify -and $remoteVerify.exitCode -eq 0) {
    Invoke-OtaPublicVerify -Config $Config -Manifest $manifest
  } else {
    [ordered]@{ ok = $false; skipped = $true; reason = "Remote verification did not pass."; steps = @() }
  }

  $steps = @($mkdir, $upload)
  if ($null -ne $remoteVerify) { $steps += $remoteVerify }
  if ($publicVerify.steps) { $steps += $publicVerify.steps }

  return [ordered]@{
    ok           = ($mkdir.exitCode -eq 0 -and $upload.exitCode -eq 0 -and $null -ne $remoteVerify -and $remoteVerify.exitCode -eq 0 -and $publicVerify.ok)
    local        = $local
    remoteVerify = $remoteVerify
    publicVerify = $publicVerify
    steps        = $steps
  }
}

function Invoke-GitCommitPush {
  param(
    [hashtable]$Config,
    [hashtable]$Body
  )

  $commitMessage = if ($Body.ContainsKey("commitMessage") -and -not [string]::IsNullOrWhiteSpace([string]$Body.commitMessage)) {
    [string]$Body.commitMessage
  } else {
    [string]$Config.defaultCommitMessage
  }

  $identity = Ensure-GitIdentity -Config $Config
  if (-not $identity.ok) {
    return [ordered]@{
      ok            = $false
      step          = $identity.step
      reason        = $identity.reason
      commitMessage = $commitMessage
      identity      = $identity.identity
      steps         = $identity.steps
      git           = Get-GitStatusData -Config $Config
    }
  }

  $branch = Get-GitBranchInfo -Config $Config
  if (-not $branch.ok) {
    $branchSteps = @()
    $branchSteps += $identity.steps
    $branchSteps += $branch.steps
    return [ordered]@{
      ok            = $false
      step          = $branch.step
      reason        = $branch.reason
      commitMessage = $commitMessage
      identity      = $identity.identity
      steps         = $branchSteps
      git           = Get-GitStatusData -Config $Config
    }
  }

  $candidateFiles = Get-GitCandidateFiles -Config $Config
  if (-not $candidateFiles.ok) {
    $candidateSteps = @()
    $candidateSteps += $identity.steps
    $candidateSteps += $branch.steps
    $candidateSteps += $candidateFiles
    return [ordered]@{
      ok            = $false
      step          = "git-commit"
      reason        = "Failed to read candidate files for commit."
      commitMessage = $commitMessage
      identity      = $identity.identity
      steps         = $candidateSteps
      git           = Get-GitStatusData -Config $Config
    }
  }

  $hasCandidates = ($candidateFiles.files.Count -gt 0)
  $add = $null
  $commit = $null
  if ($hasCandidates) {
    $add = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "add", $Config.gitAddPathspec) -WorkingDirectory $Config.repoPath
    if ($add.exitCode -eq 0) {
      $commit = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "commit", "-m", $commitMessage) -WorkingDirectory $Config.repoPath
    }
  }

  $commitReady = (-not $hasCandidates -or ($null -ne $add -and $add.exitCode -eq 0 -and $null -ne $commit -and $commit.exitCode -eq 0))
  $fetch = if ($commitReady) { Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "fetch", $Config.gitRemote, $branch.branch) -WorkingDirectory $Config.repoPath } else { $null }
  $remoteBranch = "$($Config.gitRemote)/$($branch.branch)"
  $rebase = if ($null -ne $fetch -and $fetch.exitCode -eq 0) {
    Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "rebase", $remoteBranch) -WorkingDirectory $Config.repoPath
  } else {
    $null
  }
  $abort = if ($null -ne $rebase -and $rebase.exitCode -ne 0) {
    Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "rebase", "--abort") -WorkingDirectory $Config.repoPath
  } else {
    $null
  }
  $syncReady = ($null -ne $fetch -and $fetch.exitCode -eq 0 -and $null -ne $rebase -and $rebase.exitCode -eq 0)
  $push = if ($syncReady) { Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "push", "--set-upstream", $Config.gitRemote, $branch.branch) -WorkingDirectory $Config.repoPath } else { $null }
  $verify = if ($null -ne $push -and $push.exitCode -eq 0) { Get-GitRemoteHead -Config $Config } else { $null }
  $status = Get-GitStatusData -Config $Config
  $steps = @()
  $steps += $identity.steps
  $steps += $branch.steps
  $steps += $candidateFiles
  if ($null -ne $add) { $steps += $add }
  if ($null -ne $commit) { $steps += $commit }
  if ($null -ne $fetch) { $steps += $fetch }
  if ($null -ne $rebase) { $steps += $rebase }
  if ($null -ne $abort) { $steps += $abort }
  if ($null -ne $push) { $steps += $push }
  if ($null -ne $verify) {
    $steps += $verify.steps
  }
  $reason = ""
  if ($null -ne $add -and $add.exitCode -ne 0) {
    $reason = "git add failed."
  } elseif ($hasCandidates -and ($null -eq $commit -or $commit.exitCode -ne 0)) {
    $reason = "git commit failed."
  } elseif ($null -eq $fetch -or $fetch.exitCode -ne 0) {
    $reason = "git fetch failed; push was not attempted."
  } elseif ($null -eq $rebase -or $rebase.exitCode -ne 0) {
    $reason = "Remote changes conflict with local commits. Rebase was aborted; resolve the divergence manually, then retry."
  } elseif ($null -eq $push -or $push.exitCode -ne 0) {
    $reason = "git push failed."
  } elseif ($null -ne $verify -and -not $verify.ok) {
    $reason = $verify.reason
  }

  return [ordered]@{
    ok            = ($syncReady -and $null -ne $push -and $push.exitCode -eq 0 -and $null -ne $verify -and $verify.ok)
    step          = if ([string]::IsNullOrWhiteSpace($reason)) { "" } elseif ($null -ne $add -and $add.exitCode -ne 0) { "git-add" } elseif ($hasCandidates -and ($null -eq $commit -or $commit.exitCode -ne 0)) { "git-commit" } elseif ($null -eq $fetch -or $fetch.exitCode -ne 0) { "git-fetch" } elseif ($null -eq $rebase -or $rebase.exitCode -ne 0) { "git-rebase" } elseif ($null -eq $push -or $push.exitCode -ne 0) { "git-push" } else { "git-verify" }
    reason        = $reason
    branch        = $branch.branch
    commitMessage = $commitMessage
    steps         = $steps
    verify        = $verify
    git           = $status
  }
}

function Invoke-GitPullLocal {
  param([hashtable]$Config)

  $branch = Get-GitBranchInfo -Config $Config
  if (-not $branch.ok) {
    return [ordered]@{
      ok     = $false
      step   = $branch.step
      reason = $branch.reason
      steps  = $branch.steps
      git    = Get-GitStatusData -Config $Config
    }
  }

  $workingTree = Get-GitWorkingTreeChanges -Config $Config
  if (-not $workingTree.ok) {
    $statusSteps = @()
    $statusSteps += $branch.steps
    $statusSteps += $workingTree
    return [ordered]@{
      ok     = $false
      step   = "git-status"
      reason = "Failed to inspect the local working tree before pull."
      steps  = $statusSteps
      git    = Get-GitStatusData -Config $Config
    }
  }
  if (-not $workingTree.clean) {
    $dirtySteps = @()
    $dirtySteps += $branch.steps
    $dirtySteps += $workingTree
    return [ordered]@{
      ok          = $false
      step        = "git-pull"
      reason      = "Working tree has local changes. Commit, stash, or discard them before pulling."
      branch      = $branch.branch
      workingTree = $workingTree
      steps       = $dirtySteps
      git         = Get-GitStatusData -Config $Config
    }
  }

  $fetch = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "fetch", $Config.gitRemote, $branch.branch) -WorkingDirectory $Config.repoPath
  $pull = if ($fetch.exitCode -eq 0) {
    Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $Config.repoPath, "pull", "--ff-only", $Config.gitRemote, $branch.branch) -WorkingDirectory $Config.repoPath
  } else {
    $null
  }
  $status = Get-GitStatusData -Config $Config
  $steps = @()
  $steps += $branch.steps
  $steps += @($workingTree, $fetch)
  if ($null -ne $pull) {
    $steps += $pull
  }

  return [ordered]@{
    ok          = ($fetch.exitCode -eq 0 -and $null -ne $pull -and $pull.exitCode -eq 0)
    step        = if ($fetch.exitCode -ne 0) { "git-fetch" } elseif ($null -eq $pull -or $pull.exitCode -ne 0) { "git-pull" } else { "" }
    reason      = if ($fetch.exitCode -ne 0) { "git fetch failed." } elseif ($null -eq $pull -or $pull.exitCode -ne 0) { "git pull --ff-only failed." } else { "" }
    branch      = $branch.branch
    workingTree = $workingTree
    steps       = $steps
    git         = $status
  }
}

function Get-AppHtml {
  Assert-PathExists -Path $script:HtmlPath -Label "HTML file"
  return [System.IO.File]::ReadAllText($script:HtmlPath, [System.Text.Encoding]::UTF8)
}

function Merge-Config {
  param(
    [hashtable]$Base,
    [hashtable]$Incoming
  )

  foreach ($entry in $Incoming.GetEnumerator()) {
    $Base[$entry.Key] = $entry.Value
  }
  return (Normalize-Config -Config $Base)
}

function Handle-ApiRequest {
  param([System.Net.HttpListenerContext]$Context)

  $request = $Context.Request
  $response = $Context.Response
  $path = $request.Url.AbsolutePath

  if ($path -eq "/" -or $path -eq "/index.html") {
    New-HtmlResponse -Response $response -Html (Get-AppHtml)
    return
  }

  $config = Load-Config

  try {
    switch ($path) {
      "/api/state" {
        $git = Get-GitStatusData -Config $config
        $health = Get-HealthData -Config $config
        New-JsonResponse -Response $response -StatusCode 200 -Body @{
          ok                    = $true
          toolRoot              = $PSScriptRoot
          repoRoot              = $script:RepoRoot
          config                = $config
          configNormalization   = $script:LastConfigNormalization
          git                   = $git
          health                = $health
        }
        return
      }
      "/api/config/save" {
        $body = Read-JsonBody -Request $request
        $updated = Merge-Config -Base $config -Incoming $body
        Save-Config -Config $updated | Out-Null
        New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = $true; config = $updated }
        return
      }
      "/api/git/status" {
        $body = Read-JsonBody -Request $request
        $updated = Merge-Config -Base $config -Incoming $body
        Save-Config -Config $updated | Out-Null
        $status = Get-GitStatusData -Config $updated
        New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = $true; git = $status }
        return
      }
      "/api/git/fetch" {
        $fetch = Invoke-ExternalCommand -FilePath "git" -Arguments @("-C", $config.repoPath, "fetch", $config.gitRemote) -WorkingDirectory $config.repoPath
        $status = Get-GitStatusData -Config $config
        New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = ($fetch.exitCode -eq 0); result = $fetch; git = $status }
        return
      }
      "/api/git/pull" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        $pull = Invoke-GitPullLocal -Config $runtimeConfig
        New-JsonResponse -Response $response -StatusCode 200 -Body $pull
        return
      }
      "/api/git/clone" {
        if ([string]::IsNullOrWhiteSpace([string]$config.cloneTargetPath)) { throw "cloneTargetPath is required" }
        $clone = Invoke-ExternalCommand -FilePath "git" -Arguments @("clone", $config.cloneUrl, $config.cloneTargetPath) -WorkingDirectory $script:RepoRoot
        New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = ($clone.exitCode -eq 0); result = $clone }
        return
      }
      "/api/git/commit-push" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        $result = Invoke-GitCommitPush -Config $runtimeConfig -Body $body
        New-JsonResponse -Response $response -StatusCode 200 -Body $result
        return
      }
      "/api/preflight" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        $result = Get-PreflightData -Config $runtimeConfig -Body $body
        New-JsonResponse -Response $response -StatusCode 200 -Body $result
        return
      }
      "/api/cloud/deploy" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        $result = Invoke-CloudDeploy -Config $runtimeConfig
        New-JsonResponse -Response $response -StatusCode 200 -Body $result
        return
      }
      "/api/cloud/deploy-all" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        $gitResult = Invoke-GitCommitPush -Config $runtimeConfig -Body $body
        if (-not $gitResult.ok) {
          New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = $false; stage = "git"; git = $gitResult }
          return
        }
        $cloudResult = Invoke-CloudDeploy -Config $runtimeConfig
        New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = $cloudResult.ok; stage = "cloud"; git = $gitResult; cloud = $cloudResult }
        return
      }
      "/api/ota/build" {
        $body = Read-JsonBody -Request $request
        $result = Publish-OtaFilesLocal -Config $config -Body $body
        New-JsonResponse -Response $response -StatusCode 200 -Body $result
        return
      }
      "/api/ota/publish-cloud" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        $result = Invoke-OtaPublishCloud -Config $runtimeConfig -Body $body
        New-JsonResponse -Response $response -StatusCode 200 -Body $result
        return
      }
      "/api/ota/upload-serial" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        if ([string]::IsNullOrWhiteSpace([string]$runtimeConfig.serialPort)) {
          New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = $false; step = "serial-port"; reason = "serialPort is required"; serialPort = $runtimeConfig.serialPort }
          return
        }
        $pio = Resolve-PlatformIoCommand -Command ([string]$runtimeConfig.pioCommand)
        if (-not $pio.ok) {
          New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = $false; step = "platformio"; reason = $pio.reason; pio = $pio }
          return
        }
        $upload = Invoke-ExternalCommand -FilePath $pio.command -Arguments @("run", "-d", $runtimeConfig.firmwarePath, "-e", $runtimeConfig.firmwareEnv, "-t", "upload", "--upload-port", $runtimeConfig.serialPort) -WorkingDirectory $runtimeConfig.firmwarePath
        New-JsonResponse -Response $response -StatusCode 200 -Body @{ ok = ($upload.exitCode -eq 0); result = $upload }
        return
      }
      "/api/ota/upload-network" {
        $body = Read-JsonBody -Request $request
        $runtimeConfig = Merge-Config -Base $config -Incoming $body
        $result = Invoke-LanHttpOtaUpload -Config $runtimeConfig -Body $body
        New-JsonResponse -Response $response -StatusCode 200 -Body $result
        return
      }
      default {
        New-JsonResponse -Response $response -StatusCode 404 -Body @{ ok = $false; error = "not found" }
        return
      }
    }
  } catch {
    New-JsonResponse -Response $response -StatusCode 500 -Body @{
      ok            = $false
      path          = $path
      error         = $_.Exception.Message
      exceptionType = $_.Exception.GetType().FullName
      line          = $_.InvocationInfo.ScriptLineNumber
      command       = $_.InvocationInfo.Line
    }
  }
}

$config = Load-Config
$script:Listener = New-Object System.Net.HttpListener
$prefix = "http://127.0.0.1:$Port/"
$script:Listener.Prefixes.Add($prefix)
$script:Listener.Start()

Write-Host "FollowBox Control Center running at $prefix"
Write-Host "Press Ctrl+C to stop."

if (-not $NoBrowser -and (ConvertTo-BoolSafe -Value $config.openBrowserOnStart -Default $true)) {
  Start-Process $prefix
}

try {
  while ($script:Listener.IsListening) {
    $context = $script:Listener.GetContext()
    Handle-ApiRequest -Context $context
  }
} finally {
  if ($script:Listener) {
    $script:Listener.Stop()
    $script:Listener.Close()
  }
}
