# FollowBox control center OTA helpers and actions.

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

function Get-OtaTargetHost {
  param([string]$Target)

  $trimmed = ([string]$Target).Trim().TrimEnd("/")
  if ([string]::IsNullOrWhiteSpace($trimmed)) { return "" }
  if ($trimmed -match "^https?://") {
    try { return ([uri]$trimmed).Host } catch { return $trimmed }
  }
  $withoutPath = ($trimmed -split "/")[0]
  return ($withoutPath -split ":")[0]
}

function Convert-Ipv4ToUInt32 {
  param([string]$Address)

  $bytes = [System.Net.IPAddress]::Parse($Address).GetAddressBytes()
  [Array]::Reverse($bytes)
  return [BitConverter]::ToUInt32($bytes, 0)
}

function Get-Ipv4MaskUInt32 {
  param([int]$PrefixLength)

  if ($PrefixLength -le 0) { return [uint32]0 }
  $mask = [uint32]0
  for ($i = 0; $i -lt $PrefixLength; $i++) {
    $mask = $mask -bor ([uint32]1 -shl (31 - $i))
  }
  return $mask
}

function Get-LocalIpv4Networks {
  $networks = @()
  try {
    foreach ($nic in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
      if ($nic.OperationalStatus -ne [System.Net.NetworkInformation.OperationalStatus]::Up) { continue }
      $props = $nic.GetIPProperties()
      foreach ($addr in $props.UnicastAddresses) {
        if ($addr.Address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) { continue }
        $ip = $addr.Address.ToString()
        if ($ip.StartsWith("127.")) { continue }
        $prefix = [int]$addr.PrefixLength
        $networks += [ordered]@{
          interface = $nic.Name
          address   = $ip
          prefix    = $prefix
          cidr      = "$ip/$prefix"
          network   = ((Convert-Ipv4ToUInt32 -Address $ip) -band (Get-Ipv4MaskUInt32 -PrefixLength $prefix))
          mask      = (Get-Ipv4MaskUInt32 -PrefixLength $prefix)
        }
      }
    }
  } catch {
  }
  return @($networks)
}

function Get-OtaNetworkDiagnostic {
  param([string]$Target)

  $hostName = Get-OtaTargetHost -Target $Target
  $targetInt = $null
  $isIp = $false
  try {
    $parsed = [System.Net.IPAddress]::Parse($hostName)
    if ($parsed.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork) {
      $targetInt = Convert-Ipv4ToUInt32 -Address $parsed.ToString()
      $isIp = $true
    }
  } catch {
  }

  $networks = Get-LocalIpv4Networks
  $matched = $null
  if ($isIp) {
    foreach ($network in $networks) {
      if (($targetInt -band $network.mask) -eq $network.network) {
        $matched = $network
        break
      }
    }
  }

  return [ordered]@{
    targetHost       = $hostName
    targetIsIpv4     = $isIp
    vmIpv4Networks   = @($networks | ForEach-Object { $_.cidr })
    sameVisibleLan   = (-not $isIp -or $null -ne $matched)
    matchedNetwork   = if ($null -ne $matched) { $matched.cidr } else { "" }
    note             = if ($isIp -and $null -eq $matched) { "Target IPv4 is not in any VM-visible local subnet." } else { "" }
  }
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
  $networkDiagnostic = Get-OtaNetworkDiagnostic -Target ([string]$Config.otaUploadPort)
  if (-not $endpoint.ok) {
    return [ordered]@{
      ok       = $false
      step     = "ota-http-endpoint"
      reason   = "The board HTTP OTA endpoint is not reachable. Install a firmware build that exposes /api/ota/local-upload, or put the VM and board on the same reachable LAN."
      endpoint = $endpoint
      vmNetwork = $networkDiagnostic
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
      vmNetwork = $networkDiagnostic
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
    vmNetwork    = $networkDiagnostic
    firmwareBin  = $binPath
    uploadMethod = "http-local-upload"
    build        = $build
    result       = $upload
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
