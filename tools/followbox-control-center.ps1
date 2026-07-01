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

. (Join-Path $PSScriptRoot "control-center\config.ps1")
. (Join-Path $PSScriptRoot "control-center\http.ps1")
. (Join-Path $PSScriptRoot "control-center\git.ps1")
. (Join-Path $PSScriptRoot "control-center\ota.ps1")
. (Join-Path $PSScriptRoot "control-center\cloud.ps1")

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
      $hasPushWork = ($git.ok -and ($git.files.Count -gt 0 -or ($unpushed.ok -and $unpushed.count -gt 0)))
      $details.git = $git
      $details.identity = $identity
      $details.unpushed = $unpushed
      $details.noop = ($git.ok -and $unpushed.ok -and -not $hasPushWork)
      $summary = if ($hasPushWork) { "Will run git add / commit / push." } else { "No local changes or unpushed commits found; will verify the remote is already up to date." }
      $checks += [ordered]@{ label = "Repo path exists"; ok = (Test-ExistingPath -Path $Config.repoPath) }
      $checks += [ordered]@{ label = "Git status readable"; ok = [bool]$git.ok; value = "$($git.files.Count) files" }
      $checks += [ordered]@{ label = "Current branch detected"; ok = [bool]$branch.ok; value = if ($branch.ok) { $branch.branch } else { $branch.reason } }
      $checks += [ordered]@{ label = "Unpushed commit count readable"; ok = ($branch.ok -and [bool]$unpushed.ok); value = if ($unpushed.ok) { "$($unpushed.count) commits" } else { $unpushed.range } }
      $checks += [ordered]@{ label = "Git identity ready"; ok = [bool]$identity.ok; value = if ($identity.ok) { "$($identity.effectiveName) <$($identity.effectiveEmail)>" } else { $identity.reason } }
      if ($git.pathspec -eq "." -or $git.pathspec -eq "*") {
        $warnings += "Current gitAddPathspec covers the whole repo."
      }
      if ($git.files.Count -gt 20) {
        $warnings += "Many files are included. Check that unrelated changes are not being pushed."
      }
      if ($git.files.Count -eq 0 -and $unpushed.ok -and $unpushed.count -eq 0) {
        $warnings += "Nothing needs committing or pushing; this action will return success after remote verification."
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
      $networkDiagnostic = Get-OtaNetworkDiagnostic -Target ([string]$Config.otaUploadPort)
      $pio = Resolve-PlatformIoCommand -Command ([string]$Config.pioCommand)
      $details.network = [ordered]@{
        target       = $Config.otaUploadPort
        uploadMethod = "http-local-upload"
        statusUrl    = $endpoint.url
        uploadUrl    = $endpoint.uploadUrl
      }
      $details.endpoint = $endpoint
      $details.vmNetwork = $networkDiagnostic
      $details.pio = $pio
      $summary = "Will build firmware and upload it directly to the board HTTP OTA endpoint."
      $checks += [ordered]@{ label = "OTA target configured"; ok = (-not [string]::IsNullOrWhiteSpace([string]$Config.otaUploadPort)); value = [string]$Config.otaUploadPort }
      $checks += [ordered]@{ label = "Target is on a VM-visible LAN"; ok = [bool]$networkDiagnostic.sameVisibleLan; value = if ($networkDiagnostic.sameVisibleLan) { $networkDiagnostic.matchedNetwork } else { "$($networkDiagnostic.targetHost) not in $($networkDiagnostic.vmIpv4Networks -join ', ')" } }
      $checks += [ordered]@{ label = "Board HTTP OTA endpoint reachable"; ok = [bool]$endpoint.ok; value = if ($endpoint.ok) { $endpoint.url } else { $endpoint.reason } }
      $checks += [ordered]@{ label = "PlatformIO available"; ok = [bool]$pio.ok; value = if ($pio.ok) { $pio.resolvedPath } else { $pio.reason } }
      $checks += [ordered]@{ label = "curl.exe available"; ok = (Test-CommandExists -Name "curl.exe") }
      $warnings += "Confirm the target IP belongs to the intended board before OTA."
      if (-not $networkDiagnostic.sameVisibleLan) {
        $warnings += "The configured OTA target is outside the VM-visible LAN. Use the board's STA IP on the VM subnet, or switch the VM network adapter to bridged mode."
      }
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
