[CmdletBinding()]
param([string] $Setup = 'C:\ZtermyAcceptanceInput\Ztermy-0.5.2-Setup.exe')
$ErrorActionPreference = 'Stop'
if ($env:USERNAME -ne 'WDAGUtilityAccount' -or !(Test-Path -LiteralPath 'C:\ZtermyAcceptanceInput\probe.ps1')) {
    throw 'This test may run only in the owned Windows Sandbox acceptance mapping.'
}
$results = 'C:\ZtermyAcceptanceResults'
$root = 'C:\ZtermyInstalledAcceptanceV3\Product'
$state = 'C:\ZtermyInstalledAcceptanceV3\State'
$startedUtc = [DateTime]::UtcNow.ToString('o')
$runtimeData = Join-Path $results ('installed-runtime-' + [Guid]::NewGuid().ToString('N'))
function Run-Setup([string[]] $Arguments, [bool] $ExpectSuccess = $true) {
    $process = Start-Process -FilePath $Setup -ArgumentList $Arguments -WindowStyle Hidden -PassThru -Wait
    if (($process.ExitCode -eq 0) -ne $ExpectSuccess) { throw "Unexpected installer exit $($process.ExitCode): $($Arguments[0])" }
    return $process.ExitCode
}
function Assert-Identity {
    $packages = @(Get-AppxPackage -Name ZSeries.Ztermy.Explorer)
    if ($packages.Count -ne 1 -or $packages[0].Publisher -ne 'CN=ZSeries Development') { throw 'Modern identity registration missing or ambiguous.' }
    $manager = [Windows.Management.Deployment.PackageManager,Windows.Management.Deployment,ContentType=WindowsRuntime]::new()
    $native = $manager.FindPackageForUser('', $packages[0].PackageFullName)
    if ([IO.Path]::GetFullPath($native.EffectiveExternalLocation.Path).TrimEnd('\') -ne $root) { throw 'Identity external directory differs from the installed product.' }
}
function Assert-ExplorerCommand {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
[ComImport, Guid("a08ce4d0-fa25-44ab-b57c-c7b1c323e0b9"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface ExplorerCommand {
    [PreserveSig] int GetTitle(IntPtr items, out IntPtr text);
    [PreserveSig] int GetIcon(IntPtr items, out IntPtr text);
    [PreserveSig] int GetToolTip(IntPtr items, out IntPtr text);
    [PreserveSig] int GetCanonicalName(out Guid name);
    [PreserveSig] int GetState(IntPtr items, [MarshalAs(UnmanagedType.Bool)] bool slow, out uint state);
    [PreserveSig] int Invoke(IntPtr items, IntPtr context);
    [PreserveSig] int GetFlags(out uint flags);
    [PreserveSig] int EnumSubCommands(out IntPtr commands);
}
public static class InstalledExplorerAcceptance {
    [DllImport("ole32.dll")] static extern int CoCreateInstance(ref Guid clsid, IntPtr outer, uint context, ref Guid iid, out IntPtr result);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)] static extern int SHCreateItemFromParsingName(string path, IntPtr context, ref Guid iid, out IntPtr item);
    [DllImport("shell32.dll")] static extern int SHCreateShellItemArrayFromShellItem(IntPtr item, ref Guid iid, out IntPtr items);
    public static void Inspect(string path) {
        Guid clsid = new Guid("9C0C02C2-441D-44E9-B880-68D6647B2B31"), iid = typeof(ExplorerCommand).GUID;
        IntPtr pointer = IntPtr.Zero, item = IntPtr.Zero, items = IntPtr.Zero, text = IntPtr.Zero;
        ExplorerCommand command = null;
        try {
            Marshal.ThrowExceptionForHR(CoCreateInstance(ref clsid, IntPtr.Zero, 4, ref iid, out pointer));
            command = (ExplorerCommand)Marshal.GetTypedObjectForIUnknown(pointer, typeof(ExplorerCommand));
            Marshal.ThrowExceptionForHR(command.GetTitle(IntPtr.Zero, out text));
            if (String.IsNullOrEmpty(Marshal.PtrToStringUni(text))) throw new Exception("Missing command title");
            Guid itemId = new Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe");
            Marshal.ThrowExceptionForHR(SHCreateItemFromParsingName(path, IntPtr.Zero, ref itemId, out item));
            Guid arrayId = new Guid("b63ea76d-1f85-456f-a19c-48159efa858b");
            Marshal.ThrowExceptionForHR(SHCreateShellItemArrayFromShellItem(item, ref arrayId, out items));
            uint state;
            Marshal.ThrowExceptionForHR(command.GetState(items, false, out state));
            if (state != 0) throw new Exception("Installed folder command is not enabled");
        } finally {
            if (text != IntPtr.Zero) Marshal.FreeCoTaskMem(text);
            if (items != IntPtr.Zero) Marshal.Release(items);
            if (item != IntPtr.Zero) Marshal.Release(item);
            if (command != null) Marshal.ReleaseComObject(command);
            if (pointer != IntPtr.Zero) Marshal.Release(pointer);
        }
    }
}
'@
    [InstalledExplorerAcceptance]::Inspect($root)
}
try {
    if (Test-Path -LiteralPath $root) { throw 'Use a fresh acceptance directory; do not overwrite a previous run.' }
    $arguments = @('install','--install-root',$root,'--state-root',$state,'--silent','--enable-shortcut','login-startup')
    $fresh = Run-Setup $arguments
    $ledger = Get-Content -LiteralPath (Join-Path $state 'ledger.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $executable = Join-Path $root 'ztermy.exe'
    if (!(Test-Path -LiteralPath $executable)) { throw 'Application was not installed.' }
    foreach ($file in $ledger.files) {
        $path = Join-Path $root $file.path
        if (!(Test-Path -LiteralPath $path)) { throw "Installed file missing: $($file.path)" }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw "Installed file hash differs: $($file.path)" }
    }
    $startup = Join-Path ([Environment]::GetFolderPath('Startup')) 'Ztermy.lnk'
    if (!(Test-Path -LiteralPath $startup)) { throw 'Selected login startup was not created.' }
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($startup)
    if ($shortcut.TargetPath -ne $executable -or $shortcut.Arguments -ne '--background') { throw 'Startup does not point to the installed application in background mode.' }
    $keys = @('Directory','Directory\Background','Drive') | ForEach-Object { "HKCU:\Software\Classes\$_\shell\ZInstaller.z-series.ztermy.open-here" }
    foreach ($key in $keys) {
        if (!(Test-Path -LiteralPath $key)) { throw "Traditional menu missing: $key" }
        $handler = (Get-Item -LiteralPath $key).GetValue('ExplorerCommandHandler')
        if ($handler -ne '{9C0C02C2-441D-44E9-B880-68D6647B2B31}' -or (Test-Path -LiteralPath ($key + '\command'))) {
            throw 'Traditional menu is not bound exclusively to the shared Explorer handler.'
        }
        $server = "HKCU:\Software\Classes\CLSID\$handler\InprocServer32"
        if (!(Test-Path -LiteralPath $server) -or (Get-Item -LiteralPath $server).GetValue('') -ne (Join-Path $root 'ztermy-explorer-command.dll')) {
            throw 'Explorer handler is not bound to the installed native DLL.'
        }
    }
    # Deployment on a clean system must reject an untrusted identity and restore
    # the existing installation, without importing a certificate as a side effect.
    $before = (Get-FileHash -LiteralPath (Join-Path $state 'ledger.json')).Hash
    $refusal = Run-Setup ($arguments + @('--enable-identity-package','explorer-modern')) $false
    $failureLog = Get-Content -LiteralPath (Join-Path $state 'install.log') -Raw -Encoding UTF8
    if ($failureLog -notmatch '0x800B0109') { throw 'Failure was not the expected Windows untrusted-certificate rejection.' }
    $failureLog | Set-Content -LiteralPath (Join-Path $results 'installed-untrusted-identity.log') -Encoding UTF8
    if ((Get-FileHash -LiteralPath (Join-Path $state 'ledger.json')).Hash -ne $before) { throw 'Failed identity install did not restore the prior ledger.' }
    if (@(Get-AppxPackage -Name ZSeries.Ztermy.Explorer).Count -ne 0) { throw 'Untrusted identity was registered.' }
    $cer = [Security.Cryptography.X509Certificates.X509Certificate2]::new((Join-Path $root 'integration\publisher.cer'))
    foreach ($store in @('Cert:\CurrentUser\TrustedPeople','Cert:\LocalMachine\TrustedPeople','Cert:\CurrentUser\Root','Cert:\LocalMachine\Root')) {
        if (Test-Path -LiteralPath ($store + '\' + $cer.Thumbprint)) { throw 'Installer unexpectedly changed certificate trust.' }
    }
    $fingerprint = (Get-FileHash -LiteralPath (Join-Path $root 'integration\publisher.cer')).Hash.ToLowerInvariant()
    $authorized = Run-Setup ($arguments + @('--enable-identity-package','explorer-modern','--trust-certificate',$fingerprint))
    Assert-Identity
    if (!(Test-Path -LiteralPath ('Cert:\LocalMachine\TrustedPeople\' + $cer.Thumbprint))) { throw 'Explicit consent did not trust the public certificate in the specified store.' }
    foreach ($store in @('Cert:\CurrentUser\Root','Cert:\LocalMachine\Root')) {
        if (Test-Path -LiteralPath ($store + '\' + $cer.Thumbprint)) { throw 'Installer changed a root store.' }
    }
    # Once trusted, replacement must not require another trust flag. Integration
    # memory is preserved, but certificate consent must never enter the ledger.
    $reinstall = Run-Setup $arguments
    Assert-Identity
    $ledgerPath = Join-Path $state 'ledger.json'
    $ledger = Get-Content -LiteralPath $ledgerPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (($ledger.integration_choices | ConvertTo-Json) -match $fingerprint) { throw 'Certificate consent was persisted as a remembered integration choice.' }
    # Exercise a true version-changing backend upgrade, without producing or
    # publishing a fake release. Payload is copied from the verified installation.
    $fixture = 'C:\ZtermyInstalledAcceptanceV3\UpgradeFixture'
    $payload = Join-Path $fixture 'payload'
    foreach ($file in $ledger.files) {
        $target = Join-Path $payload $file.path
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
        [IO.File]::Copy((Join-Path $root $file.path), $target)
    }
    $source = [IO.File]::ReadAllText('C:\ZtermyAcceptanceInput\package.toml')
    $upgradeManifest = Join-Path $fixture 'upgrade.toml'
    [IO.File]::WriteAllText($upgradeManifest, $source.Replace('version = "0.5.2"', 'version = "0.5.3"'))
    $upgradeArguments = @('install','--install-root',$root,'--state-root',$state,'--silent','--manifest',$upgradeManifest,'--payload',$payload)
    $upgrade = Run-Setup $upgradeArguments
    Assert-Identity
    $ledger = Get-Content -LiteralPath $ledgerPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($ledger.version -ne '0.5.3') { throw 'Version-changing upgrade did not commit the new ledger.' }
    # A publisher mismatch fails only after prior registration removal and file
    # commits. Verify actual rollback, not just the planner's operation ordering.
    $before = (Get-FileHash -LiteralPath $ledgerPath).Hash
    $iconPath = '.zinstaller\product-icon.svg'
    $iconHash = (Get-FileHash -LiteralPath (Join-Path $root $iconPath)).Hash
    $faultSource = [IO.File]::ReadAllText($upgradeManifest).Replace('version = "0.5.3"','version = "0.5.4"').Replace('publisher = "CN=ZSeries Development"','publisher = "CN=Wrong Acceptance Publisher"')
    $icon = Join-Path $payload $iconPath
    $oldIconBytes = [IO.File]::ReadAllBytes($icon)
    [IO.File]::AppendAllText($icon, "`n<!-- transaction rollback acceptance -->`n")
    $faultSource = $faultSource.Replace('size = ' + $oldIconBytes.Length, 'size = ' + ([IO.FileInfo]$icon).Length).Replace($iconHash.ToLowerInvariant(), (Get-FileHash -LiteralPath $icon).Hash.ToLowerInvariant())
    $faultManifest = Join-Path $fixture 'fault.toml'
    [IO.File]::WriteAllText($faultManifest, $faultSource)
    $rollback = Run-Setup @('install','--install-root',$root,'--state-root',$state,'--silent','--manifest',$faultManifest,'--payload',$payload) $false
    $rollbackLog = Get-Content -LiteralPath (Join-Path $state 'install.log') -Raw -Encoding UTF8
    if ($rollbackLog -notmatch 'Certificate identity mismatch') { throw 'Injected upgrade did not reach the intended identity-registration failure.' }
    if ((Get-FileHash -LiteralPath $ledgerPath).Hash -ne $before -or (Get-FileHash -LiteralPath (Join-Path $root $iconPath)).Hash -ne $iconHash) { throw 'Failed upgrade did not restore the prior ledger and modified file.' }
    Assert-Identity
    $unknown = Join-Path $root 'acceptance-user-owned.txt'
    [IO.File]::WriteAllText($unknown, 'user-owned acceptance content')
    # Every run needs a clean settings/workspace store. Reusing a prior capture
    # directory restores its maximized window and invalidates the smoke's normal
    # initial-window precondition; captures are evidence, not an input fixture.
    $smoke = Start-Process -FilePath $executable -ArgumentList @('--system-integration-smoke','--data-dir',$runtimeData) -WindowStyle Hidden -PassThru -Wait
    if ($smoke.ExitCode -ne 0) { throw "Installed native/UI integration smoke failed: $($smoke.ExitCode)" }
    Assert-ExplorerCommand
    $uninstall = Run-Setup @('uninstall','--install-root',$root,'--state-root',$state,'--silent')
    if (Test-Path -LiteralPath $executable) { throw 'Uninstall left the managed executable.' }
    if (Test-Path -LiteralPath $startup) { throw 'Uninstall left the login startup shortcut.' }
    if ([IO.File]::ReadAllText($unknown) -ne 'user-owned acceptance content') { throw 'Uninstall removed or changed an unowned file.' }
    foreach ($key in $keys) { if (Test-Path -LiteralPath $key) { throw 'Uninstall left an owned menu.' } }
    if (Get-AppxPackage -Name ZSeries.Ztermy.Explorer) { throw 'Uninstall left the registered identity.' }
    if (!(Test-Path -LiteralPath ('Cert:\LocalMachine\TrustedPeople\' + $cer.Thumbprint))) { throw 'Uninstall removed shared publisher trust.' }
    [ordered]@{ passed = $true; startedUtc = $startedUtc; setupSha256 = (Get-FileHash -LiteralPath $Setup).Hash.ToLowerInvariant(); freshExit = $fresh; untrustedIdentityExit = $refusal; authorizedIdentityExit = $authorized; reinstallExit = $reinstall; upgradeExit = $upgrade; failedUpgradeExit = $rollback; previousRegistrationRestored = $true; runtimeExit = $smoke.ExitCode; runtimeData = $runtimeData; uninstallExit = $uninstall; sharedTrustRetained = $true; checkedFiles = $ledger.files.Count; sandboxOnly = $true } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results 'installed-integration-trust-result.json') -Encoding UTF8
} catch {
    $_.ToString() | Set-Content -LiteralPath (Join-Path $results 'installed-integration-trust-error.txt') -Encoding UTF8
    throw
}
