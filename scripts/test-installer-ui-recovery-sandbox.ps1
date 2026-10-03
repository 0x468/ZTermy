[CmdletBinding()]
param([ValidateSet('Prepare','Finish','Hold')][string] $Phase = 'Prepare')
$ErrorActionPreference = 'Stop'
if ($env:USERNAME -ne 'WDAGUtilityAccount' -or !(Test-Path 'C:\ZtermyAcceptanceInput\probe.ps1')) {
    throw 'Only the owned Windows Sandbox acceptance mapping may run this test.'
}
$inputRoot = 'C:\ZtermyAcceptanceInput'
$results = 'C:\ZtermyAcceptanceResults'
$setup = Join-Path $inputRoot 'Ztermy-0.5.2-Setup.exe'
$oldSetup = Join-Path $inputRoot 'Old-Setup.exe'
$install = Join-Path $env:LOCALAPPDATA 'Ztermy'
$state = Join-Path $env:LOCALAPPDATA 'Z Series\Installer\z-series.ztermy'
$dll = Join-Path $install 'ztermy-explorer-command.dll'
if ($Phase -eq 'Hold') {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MappedAcceptanceImage {
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32", SetLastError=true)] public static extern bool FreeLibrary(IntPtr module);
}
'@
    $module = [MappedAcceptanceImage]::LoadLibrary($dll)
    if ($module -eq [IntPtr]::Zero) { throw 'Cannot map acceptance DLL.' }
    try {
        [IO.File]::WriteAllText((Join-Path $results 'mapped-image-ready.txt'), 'loaded actual product shell extension')
        while (!(Test-Path (Join-Path $results 'release-mapped-image.txt'))) { Start-Sleep -Milliseconds 100 }
    } finally { [MappedAcceptanceImage]::FreeLibrary($module) | Out-Null }
    return
}
function Run-Setup([string] $Executable, [string[]] $Arguments, [int] $Expected) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    $null = $process.Handle
    $process.WaitForExit()
    if ($null -eq $process.ExitCode -or $process.ExitCode -ne $Expected) {
        throw "Installer exit $($process.ExitCode), expected $Expected ($($Arguments[0]))."
    }
    return $process.ExitCode
}
function Journal-Hashes {
    return @(Get-ChildItem (Join-Path $state 'journal') -File | Sort-Object Name | ForEach-Object {
        $_.Name + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash
    })
}
try {
    if ($Phase -eq 'Prepare') {
        if (Test-Path $install) { throw 'Use a fresh Sandbox.' }
        $fresh = Run-Setup $oldSetup @('install','--silent','--disable-identity-package','explorer-modern') 0
        # Test-owned valid PE overlay makes the mapped old image differ from the
        # new payload, so a hash-equal shortcut cannot conceal the update failure.
        $stream = [IO.File]::Open($dll, [IO.FileMode]::Append)
        try { $overlay = [Text.Encoding]::ASCII.GetBytes('mapped-image-old-test'); $stream.Write($overlay,0,$overlay.Length) }
        finally { $stream.Dispose() }
        $holder = Start-Process powershell.exe -ArgumentList @('-NoProfile','-File',$PSCommandPath,'-Phase','Hold') -WindowStyle Hidden -PassThru
        $null = $holder.Handle
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while (!(Test-Path (Join-Path $results 'mapped-image-ready.txt'))) {
            if ($holder.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Image holder did not start.' }
            Start-Sleep -Milliseconds 100
        }
        $failure = Run-Setup $oldSetup @('install','--silent','--disable-identity-package','explorer-modern') 30
        $journal = (Get-ChildItem (Join-Path $state 'journal') -File | ForEach-Object {
            Get-Content $_.FullName -Raw | ConvertFrom-Json
        } | Sort-Object generation -Descending | Select-Object -First 1).journal
        if ($journal.state -ne 'recovery-required' -or !($journal.steps | Where-Object {
            $_.state -eq 'rollback-failed' -and $_.operation.destination -eq 'ztermy-explorer-command.dll'
        })) { throw 'Did not reproduce the exact mapped-extension recovery failure.' }
        $before = Journal-Hashes
        $startup = Join-Path $env:TEMP 'zinstaller-renderer-startup.log'
        $startupBefore = if (Test-Path $startup) { [IO.File]::ReadAllText($startup).Length } else { 0 }
        $gui = Start-Process $setup -WindowStyle Normal -PassThru
        $null = $gui.Handle
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            Start-Sleep -Milliseconds 200
            # Setup is a light bootstrapper -> extracted supervisor -> UI child.
            # Its PID is not the supervisor PID written in the UTF-8 startup log.
            # Keep the PS 5.1 source pattern ASCII-only (UTF-8 without BOM).
            $ready = (Test-Path $startup) -and ([IO.File]::ReadAllText($startup).Substring($startupBefore) -match 'renderer=\w+ .*ready')
            if ($gui.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Final bundled installer UI did not send ready.' }
        } until ($ready)
        if (@(Compare-Object $before (Journal-Hashes)).Count) { throw 'Opening options mutated the failed installation journal.' }
        [ordered]@{ ready=$true; reproducedOldExit=$failure; baselineExit=$fresh; uiParentPid=$gui.Id; holderPid=$holder.Id; journalUnchanged=$true; setupSha256=(Get-FileHash $setup).Hash.ToLowerInvariant() } |
            ConvertTo-Json | Set-Content (Join-Path $results 'installer-ui-ready.json') -Encoding UTF8
    } else {
        $ready = Get-Content (Join-Path $results 'installer-ui-ready.json') -Raw | ConvertFrom-Json
        # Close only this acceptance's owned frontend processes; no Explorer or
        # product windows are closed, and no host-machine process is touched.
        $allProcesses = @(Get-CimInstance Win32_Process)
        $ownedIds = @([int]$ready.uiParentPid)
        do {
            $next = @($allProcesses | Where-Object { $_.ParentProcessId -in $ownedIds -and $_.ProcessId -notin $ownedIds })
            $ownedIds += @($next | ForEach-Object { [int]$_.ProcessId })
        } while ($next.Count)
        $frontends = @($allProcesses | Where-Object { $_.ProcessId -in $ownedIds })
        foreach ($frontend in $frontends) {
            $owned = Get-Process -Id $frontend.ProcessId -ErrorAction SilentlyContinue
            if ($owned -and $owned.MainWindowHandle -ne 0) { $null = $owned.CloseMainWindow() }
        }
        Start-Sleep -Milliseconds 500
        $recovered = Run-Setup $setup @('install','--silent','--disable-identity-package','explorer-modern') 0
        $ledger = Get-Content (Join-Path $state 'ledger.json') -Raw | ConvertFrom-Json
        foreach ($file in $ledger.files) {
            if ((Get-FileHash (Join-Path $install $file.path)).Hash.ToLowerInvariant() -ne $file.sha256) { throw "Wrong installed hash: $($file.path)" }
        }
        $retired = Join-Path $state 'transaction\retired-images'
        if (!(Test-Path $retired) -or @(Get-ChildItem $retired -File).Count -lt 1) { throw 'Loaded old image was not safely retained.' }
        [IO.File]::WriteAllText((Join-Path $results 'release-mapped-image.txt'), 'release acceptance-owned LoadLibrary reference')
        $holder = Get-Process -Id $ready.holderPid -ErrorAction SilentlyContinue
        if ($holder) { $holder.WaitForExit(15000) | Out-Null; if (!$holder.HasExited) { throw 'Holder did not release image.' } }
        $reinstall = Run-Setup $setup @('install','--silent','--disable-identity-package','explorer-modern') 0
        if (Test-Path $retired) { throw 'Retired images were not cleaned after release.' }
        $uninstall = Run-Setup $setup @('uninstall','--silent') 0
        if (Test-Path (Join-Path $install 'ztermy.exe')) { throw 'Uninstall left the product executable.' }
        [ordered]@{passed=$true; actualUiReady=$ready.ready; uiDidNotMutateJournal=$ready.journalUnchanged; reproducedOldExit=$ready.reproducedOldExit; recoveredInstallExit=$recovered; checkedFiles=$ledger.files.Count; reinstallExit=$reinstall; retiredCleaned=$true; uninstallExit=$uninstall; sandboxOnly=$true; setupSha256=$ready.setupSha256} |
            ConvertTo-Json | Set-Content (Join-Path $results 'installer-ui-recovery-result.json') -Encoding UTF8
    }
} catch {
    $_.ToString() | Set-Content (Join-Path $results 'installer-ui-recovery-error.txt') -Encoding UTF8
    throw
}
