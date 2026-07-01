# FollowBox control center cloud deployment helpers and actions.

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
    mode              = "ssh-primary"
    remoteLoopback    = if ($remoteHttpVerify.exitCode -eq 0) { ($remoteHttpVerify.stdout | Out-String).Trim() } else { ($remoteHttpVerify.stderr | Out-String).Trim() }
    publicApp         = ""
    publicDeployStamp = ""
    note              = "Deployment success is decided by SSH-side verification so public IP allow-lists do not block validation."
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
