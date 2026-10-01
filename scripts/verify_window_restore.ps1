param([string]$BuildDirectory = 'build/msvc-dynamic-release', [switch]$RemovedScreen, [switch]$VisualStatus, [switch]$SnapCapture,
      [switch]$TrayExit, [string]$ExecutableName = 'ztermy.exe')

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = (Resolve-Path (Join-Path $repo $BuildDirectory)).Path
$exe = Join-Path $build $ExecutableName
$data = Join-Path $build ('test-data/window-restore-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $data | Out-Null

# No pointer injection or existing user data. The in-process check observes
# native windows before shutdown; this script checks persisted topology and
# normal bounds independently, then starts another process on the saved state.

$settings = Get-Content (Join-Path $repo 'tests/fixtures/settings/schema-39.json') -Raw | ConvertFrom-Json
$settings.reopenLocalSessions = $false
if ($TrayExit) { $settings.reopenLocalSessions = $true }
$settings.reconnectRemoteSessions = $false
$settings.closeToTray = $false
$settings.language = 'en'
$settings | ConvertTo-Json -Depth 50 | Set-Content (Join-Path $data 'settings.json') -Encoding utf8
$profiles = Get-Content (Join-Path $repo 'tests/fixtures/ssh/schema-8.json') -Raw | ConvertFrom-Json
$profiles.version = 9
foreach ($profile in $profiles.profiles) {
    $profile | Add-Member -NotePropertyName iconName -NotePropertyValue 'security'
}
$profiles | ConvertTo-Json -Depth 50 | Set-Content (Join-Path $data 'profiles.json') -Encoding utf8
function Tab($id, $owner) {
    return @{
        id=$id; title=$id; manualTitle=$id; windowId=$owner; returnWorkspaceId=''
        rootNodeId="$id-pane"; activePaneId="$id-pane"
        nodes=@(@{id="$id-pane"; kind='leaf'; restoreIntentId="$id-intent"; firstChildId=''; secondChildId=''; orientation='horizontal'; ratio=0.5})
        restoreIntents=@(@{id="$id-intent"; kind='local'; profileId='commandPrompt'; title=$id; manualTitle=$id})
    }
}
$workspace = @{
    schemaVersion=10; profiles=@(); collapsedHostSections=@(); quarantinedRestoreIntentIds=@(); restoreAttemptIntentId=''
    activeTerminalWorkspaceId='main-check'
    terminalWorkspaces=@((Tab 'main-check' 'main'), (Tab 'detached-first' 'restore-window'), (Tab 'detached-selected' 'restore-window'))
    terminalWindows=@(
        @{id='main'; selectedWorkspaceId='main-check'; screenName=''; x=50; y=60; width=840; height=540; maximized=$false},
        @{id='restore-window'; selectedWorkspaceId='detached-selected'; screenName=''; x=100; y=110; width=800; height=520; maximized=$true}
    )
}
$statePath = Join-Path $data 'workspace_state.json'
foreach ($tab in $workspace.terminalWorkspaces | Where-Object id -NE 'detached-first') {
    if ($TrayExit) { continue }
    $tab.restoreIntents[0].kind = 'ssh-profile'
    $tab.restoreIntents[0].profileId = 'target'
}
if ($RemovedScreen) {
    foreach ($placement in $workspace.terminalWindows) {
        $placement.screenName = 'removed-test-monitor'
        $placement.x = -90000
        $placement.y = -90000
    }
}
$workspace | ConvertTo-Json -Depth 50 | Set-Content $statePath -Encoding utf8
$oldBackend = $env:QT_QUICK_BACKEND
$env:QT_QUICK_BACKEND = 'software'
$owned = $null
try {
    foreach ($pass in 1..2) {
        $arguments = @('--window-runtime-smoke', '--saved-window-startup', '--data-dir', ('"' + $data + '"'))
        if ($RemovedScreen) { $arguments += '--removed-screen-startup' }
        if ($VisualStatus) { $arguments += '--status-visual-smoke' }
        if ($SnapCapture) { $arguments += '--snap-layout-capture' }
        if ($TrayExit) {
            $arguments += '--tray-exit-smoke'
            if ($pass -eq 2) { $arguments += '--tray-exit-hidden' }
        }
        $owned = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if (!$owned.WaitForExit(20000)) { throw 'Runtime check did not complete' }
        if ($owned.ExitCode -ne 0) { throw "Runtime check failed: $($owned.ExitCode); see isolated logs" }
        $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($owned.Id)")
        if ($children.Count) { throw 'Terminal child processes remain after application exit' }
        if ($TrayExit) {
            $log = Get-Content (Join-Path $data 'logs/ztermy.log') -Raw
            if ($log -notmatch 'Tray exit: event loop stopped without timeout and full Tab topology preserved: true' -or
                $log -notmatch 'Local terminal session started' -or $log -notmatch 'Local terminal session stopped') {
                throw 'Tray exit did not exercise live local-session cleanup'
            }
        }
        $saved = Get-Content $statePath -Raw | ConvertFrom-Json
        $placement = @($saved.terminalWindows | Where-Object id -EQ 'restore-window')
        $detached = @($saved.terminalWorkspaces | Where-Object windowId -NE 'main')
        $owners = @($detached.windowId | Select-Object -Unique)
        if ($saved.terminalWorkspaces.Count -ne 3 -or $placement.Count -ne 1 -or
            $detached.Count -ne 2 -or $owners.Count -ne 2 -or $saved.terminalWindows.Count -ne 3 -or
            !$placement[0].maximized -or $placement[0].selectedWorkspaceId -ne 'detached-selected' -or
            $placement[0].width -ne 800 -or $placement[0].height -ne 520) {
            throw 'Shutdown overwrote selected Tab, normal bounds, maximization or topology'
        }
        Write-Output "PASS restart $pass : native state, selected workspace, normal bounds, legacy tabs split without loss, no children"
        $owned.Dispose()
        $owned = $null
    }
} finally {
    if ($owned) {
        $owned.Refresh()
        if (!$owned.HasExited) { Stop-Process -Id $owned.Id -Force }
        $owned.Dispose()
    }
    $env:QT_QUICK_BACKEND = $oldBackend
    Write-Output "Evidence: $data"
}
