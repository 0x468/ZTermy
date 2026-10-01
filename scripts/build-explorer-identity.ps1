[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Publisher,
    [Parameter(Mandatory)][string] $Version,
    [Parameter(Mandatory)][string] $Logo,
    [Parameter(Mandatory)][string] $OutputDirectory,
    [string] $SigningThumbprint,
    [string] $SignTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\SignTool.exe',
    [string] $MakeAppx = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\MakeAppx.exe'
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Publisher) -or $Publisher.IndexOfAny([char[]]"`r`n") -ge 0) {
    throw 'Publisher must be the signing certificate Subject, on one line.'
}
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'MSIX requires a four-part version.' }
$parsedVersion = [Version]$Version
foreach ($component in @($parsedVersion.Major, $parsedVersion.Minor, $parsedVersion.Build, $parsedVersion.Revision)) {
    if ($component -gt 65535) { throw 'Each MSIX version component must fit in 16 bits.' }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw "Refusing existing identity-package output: $output" }
$logoPath = (Resolve-Path -LiteralPath $Logo).Path
if ([IO.Path]::GetExtension($logoPath) -ne '.png') { throw 'Use a product-owned PNG logo.' }
$tool = (Resolve-Path -LiteralPath $MakeAppx).Path
$repository = Split-Path -Parent $PSScriptRoot
$template = Join-Path $repository 'installer/explorer/AppxManifest.xml.in'
$content = [IO.File]::ReadAllText($template).Replace('@PUBLISHER@', [Security.SecurityElement]::Escape($Publisher)).Replace('@VERSION@', $Version)
# No signing key, certificate store or OS registration is touched by this build.
$packageRoot = Join-Path $output 'package'
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'Assets') | Out-Null
Copy-Item -LiteralPath $logoPath -Destination (Join-Path $packageRoot 'Assets/logo.png')
[IO.File]::WriteAllText((Join-Path $packageRoot 'AppxManifest.xml'), $content, [Text.UTF8Encoding]::new($false))
$package = Join-Path $output 'ztermy-explorer.msix'
# Referenced app and command DLL intentionally remain in the external install directory.
& $tool pack /d $packageRoot /nv /p $package
if ($LASTEXITCODE -ne 0) { throw 'Identity-package build failed.' }
if ($SigningThumbprint) {
    if ($SigningThumbprint -notmatch '^[a-fA-F0-9]{40}$') { throw 'SigningThumbprint must be a full certificate thumbprint.' }
    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$SigningThumbprint"
    if (-not $certificate.HasPrivateKey -or $certificate.Subject -ne $Publisher) { throw 'Signing identity does not match Publisher.' }
    $signingTool = (Resolve-Path -LiteralPath $SignTool).Path
    & $signingTool sign /fd SHA256 /s My /sha1 $SigningThumbprint $package
    if ($LASTEXITCODE -ne 0) { throw 'Identity-package signing failed.' }
    # Export only the public DER; the PFX never enters staging.
    Export-Certificate -Cert $certificate -FilePath (Join-Path $output 'publisher.cer') | Out-Null
}
[pscustomobject]@{ Package = $package; Publisher = $Publisher; Version = $Version; Signed = [bool]$SigningThumbprint }
