# FollowBox control center Git helpers and actions.

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
  $unpushed = Get-GitUnpushedCommitCount -Config $Config -Branch $branch.branch
  if (-not $hasCandidates -and $unpushed.ok -and $unpushed.count -eq 0) {
    $verify = Get-GitRemoteHead -Config $Config
    $status = Get-GitStatusData -Config $Config
    $steps = @()
    $steps += $identity.steps
    $steps += $branch.steps
    $steps += $candidateFiles
    $steps += $unpushed.steps
    if ($verify.steps) {
      $steps += $verify.steps
    }
    $reason = if ($verify.ok) { "" } else { "No local changes or unpushed commits were found, but remote verification failed. Pull or check the remote before retrying." }
    return [ordered]@{
      ok            = [bool]$verify.ok
      step          = if ($verify.ok) { "git-noop" } else { "git-verify" }
      reason        = $reason
      summary       = if ($verify.ok) { "No local changes or unpushed commits; repository is already up to date." } else { $reason }
      branch        = $branch.branch
      commitMessage = $commitMessage
      noop          = $true
      steps         = $steps
      verify        = $verify
      git           = $status
    }
  }

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
