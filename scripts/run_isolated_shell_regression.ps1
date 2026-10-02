[CmdletBinding()]
param(
    [ValidateSet('msvc-dynamic-debug', 'msvc-static-release')]
    [string] $Preset = 'msvc-dynamic-debug',
    [string] $TestRegex = '^(local-terminal-session|ai-agent-scenario|conpty-process)$'
)
$ErrorActionPreference = 'Stop'
$started = Get-Date
$oldIsolation = $env:ZTERMY_TEST_ISOLATED_SHELLS
$oldClink = $env:CLINK_NOAUTORUN
try {
    $env:ZTERMY_TEST_ISOLATED_SHELLS = '1'
    $env:CLINK_NOAUTORUN = '1'
    & ctest --preset $Preset --parallel 12 --output-on-failure -R $TestRegex
    $testExit = $LASTEXITCODE
    # Read only: never dismiss errors, change AutoRun/profile, or kill user shells.
    try {
        $events = @(Get-WinEvent -FilterHashtable @{
            LogName = 'System'; ProviderName = 'Application Popup'; Id = 26; StartTime = $started
        } -ErrorAction Stop)
    } catch {
        if ($_.FullyQualifiedErrorId -notlike 'NoMatchingEventsFound*') { throw }
        $events = @()
    }
    $shellFailures = @($events | Where-Object {
        $_.Message -match '(?i)(clink(?:_x64)?|cmd|pwsh|powershell)\.exe' -and
        $_.Message -match '(?i)0xc0000142'
    })
    if ($shellFailures.Count -gt 0) {
        # Event messages may contain paths or user data: report times only.
        $times = ($shellFailures | ForEach-Object { $_.TimeCreated.ToString('o') }) -join ', '
        throw "New Windows shell DLL-initialization error(s): $times. Acceptance failed."
    }
    if ($testExit -ne 0) { throw "CTest failed with exit code $testExit." }
    Write-Output 'Isolated shell regression passed; no new shell DLL-init popup events.'
} finally {
    if ($null -eq $oldClink) {
        Remove-Item Env:CLINK_NOAUTORUN -ErrorAction SilentlyContinue
    } else {
        $env:CLINK_NOAUTORUN = $oldClink
    }
    if ($null -eq $oldIsolation) {
        Remove-Item Env:ZTERMY_TEST_ISOLATED_SHELLS -ErrorAction SilentlyContinue
    } else {
        $env:ZTERMY_TEST_ISOLATED_SHELLS = $oldIsolation
    }
}
