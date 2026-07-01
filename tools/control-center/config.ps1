# FollowBox control center config and shared serialization helpers.

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
    cloudPemPath          = (Join-Path $env:USERPROFILE "Downloads\codex.pem")
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
