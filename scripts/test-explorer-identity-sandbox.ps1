[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$account = [Security.Principal.WindowsIdentity]::GetCurrent().Name.Split('\')[-1]
if ($account -ne 'WDAGUtilityAccount' -or !(Test-Path -LiteralPath 'C:\ZtermyAcceptanceInput\probe.ps1')) {
    throw 'Sandbox-only acceptance. Refusing to change host certificates or registrations.'
}
$results = 'C:\ZtermyAcceptanceResults'
$root = Join-Path $env:TEMP ('ztermy-identity-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$external = Join-Path $root 'external'
New-Item -ItemType Directory -Path $external | Out-Null
$package = Join-Path $root 'identity.msix'
Copy-Item -LiteralPath 'C:\ZtermyAcceptanceInput\identity-v1.msix' -Destination $package
Copy-Item -LiteralPath 'C:\ZtermyAcceptanceInput\ztermy-explorer-command.dll' -Destination $external
try {
    $previous = Get-AppxPackage -Name ZSeries.Ztermy.Explorer
    if ($previous) {
        if ($previous.Publisher -ne 'CN=Ztermy Sandbox Acceptance') { throw 'Refusing an unexpected pre-existing identity.' }
        Remove-AppxPackage -Package $previous.PackageFullName -ErrorAction Stop
    }
    # This key exists only inside the disposable VM. No PFX is exported.
    $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=Ztermy Sandbox Acceptance' `
        -CertStoreLocation 'Cert:\CurrentUser\My' -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 2048 `
        -HashAlgorithm SHA256 -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
    & 'C:\ZtermyAcceptanceInput\SignTool.exe' sign /fd SHA256 /sha1 $cert.Thumbprint /s My $package *> (Join-Path $results 'identity-sign.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Sandbox-only signing failed.' }
    # A native protocol sink, not the Qt product, isolates the extension's argv contract.
    Add-Type -TypeDefinition @'
using System;
using System.IO;
public class DirectorySink {
    public static void Main(string[] args) {
        File.WriteAllLines(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "received-argv.txt"), args);
    }
}
'@ -OutputAssembly (Join-Path $external 'ztermy.exe') -OutputType ConsoleApplication
    $rejected = $false
    try { Add-AppxPackage -Path $package -ExternalLocation $external -ErrorAction Stop } catch {
        $rejected = $true
        $_.ToString() | Set-Content -LiteralPath (Join-Path $results 'identity-untrusted-rejection.txt') -Encoding UTF8
    }
    if (!$rejected -or (Get-AppxPackage -Name ZSeries.Ztermy.Explorer)) { throw 'Untrusted package was unexpectedly accepted.' }
    $cer = Join-Path $root 'acceptance-public.cer'
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
    # Explicit test authorization applies only inside this disposable Sandbox.
    Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null
    $trustStore = 'CurrentUser/TrustedPeople'
    try {
        Add-AppxPackage -Path $package -ExternalLocation $external -ErrorAction Stop
    } catch {
        $_.ToString() | Set-Content -LiteralPath (Join-Path $results 'identity-user-trust-rejection.txt') -Encoding UTF8
        if ($_.ToString() -notmatch '0x800B0109') { throw }
        # Deployment-service trust differs by Windows build. Test a second,
        # explicitly authorized store, never Trusted Root and never on the host.
        Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null
        $trustStore = 'LocalMachine/TrustedPeople'
        Add-AppxPackage -Path $package -ExternalLocation $external -ErrorAction Stop
    }
    $installed = Get-AppxPackage -Name ZSeries.Ztermy.Explorer
    if (!$installed -or $installed.Publisher -ne $cert.Subject) { throw 'Signed identity was not registered for this user.' }
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
public static class NativeCommandAcceptance {
    [DllImport("ole32.dll")] static extern int CoCreateInstance(ref Guid clsid, IntPtr outer, uint context, ref Guid iid, out IntPtr result);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)] static extern int SHCreateItemFromParsingName(string path, IntPtr context, ref Guid iid, out IntPtr item);
    [DllImport("shell32.dll")] static extern int SHCreateShellItemArrayFromShellItem(IntPtr item, ref Guid iid, out IntPtr items);
    public static string InvokeDirectory(string path) {
        Guid clsid = new Guid("9C0C02C2-441D-44E9-B880-68D6647B2B31"), iid = typeof(ExplorerCommand).GUID;
        IntPtr commandPointer = IntPtr.Zero, item = IntPtr.Zero, items = IntPtr.Zero, title = IntPtr.Zero;
        ExplorerCommand command = null;
        try {
            Marshal.ThrowExceptionForHR(CoCreateInstance(ref clsid, IntPtr.Zero, 4, ref iid, out commandPointer));
            command = (ExplorerCommand)Marshal.GetTypedObjectForIUnknown(commandPointer, typeof(ExplorerCommand));
            Marshal.ThrowExceptionForHR(command.GetTitle(IntPtr.Zero, out title));
            string label = Marshal.PtrToStringUni(title);
            Guid itemId = new Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe");
            Marshal.ThrowExceptionForHR(SHCreateItemFromParsingName(path, IntPtr.Zero, ref itemId, out item));
            Guid arrayId = new Guid("b63ea76d-1f85-456f-a19c-48159efa858b");
            Marshal.ThrowExceptionForHR(SHCreateShellItemArrayFromShellItem(item, ref arrayId, out items));
            uint state;
            Marshal.ThrowExceptionForHR(command.GetState(items, false, out state));
            if (state != 0) throw new Exception("Folder command is not enabled");
            Marshal.ThrowExceptionForHR(command.Invoke(items, IntPtr.Zero));
            return label;
        } finally {
            if (title != IntPtr.Zero) Marshal.FreeCoTaskMem(title);
            if (items != IntPtr.Zero) Marshal.Release(items);
            if (item != IntPtr.Zero) Marshal.Release(item);
            if (command != null) Marshal.ReleaseComObject(command);
            if (commandPointer != IntPtr.Zero) Marshal.Release(commandPointer);
        }
    }
}
'@
    # Windows PowerShell 5.1 parses BOM-less scripts as ANSI. Keep this source ASCII.
    $directory = Join-Path $root ([string][char]0x4E2D + [char]0x6587 + ' space & % !')
    New-Item -ItemType Directory -Path $directory | Out-Null
    $label = [NativeCommandAcceptance]::InvokeDirectory($directory)
    $argvFile = Join-Path $external 'received-argv.txt'
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (!(Test-Path -LiteralPath $argvFile) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
    $arguments = @([IO.File]::ReadAllLines($argvFile, [Text.Encoding]::UTF8))
    $arguments | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results 'identity-argv.json') -Encoding UTF8
    if ($arguments.Count -ne 2 -or $arguments[0] -ne '--open-directory' -or [IO.Path]::GetFullPath($arguments[1]) -ne $directory) {
        throw 'Native command did not preserve the directory as one literal argv value.'
    }
    Remove-AppxPackage -Package $installed.PackageFullName -ErrorAction Stop
    if (Get-AppxPackage -Name ZSeries.Ztermy.Explorer) { throw 'Identity uninstall left a registered package.' }
    $actualTrustPath = 'Cert:\' + $trustStore.Replace('/', '\') + '\' + $cert.Thumbprint
    if (!(Test-Path -LiteralPath $actualTrustPath)) { throw 'Uninstall removed shared certificate trust.' }
    [ordered]@{ phase = 'signed-identity'; passed = $true; untrustedRejected = $rejected; trustStore = $trustStore; nativeComActivation = $true; literalDirectory = $true; title = $label; uninstall = $true; sharedTrustRetained = $true } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results 'identity-result.json') -Encoding UTF8
} catch {
    $_.ToString() | Set-Content -LiteralPath (Join-Path $results 'identity-error.txt') -Encoding UTF8
    exit 1
}
