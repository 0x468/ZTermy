[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) '.signing'),
    [switch] $ExportPrivateKeyWithoutPassword
)
$ErrorActionPreference = 'Stop'
$subject = 'CN=ZSeries Development'
$store = 'Cert:\CurrentUser\My'
$output = [IO.Path]::GetFullPath($OutputDirectory)
$repository = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\', '/')
if ($output.Equals($repository, [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($repository + '\', [StringComparison]::OrdinalIgnoreCase)) {
    $signingDirectory = Join-Path $repository '.signing'
    if (!$output.Equals($signingDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'In this repository, signing material must be kept only in the ignored .signing directory.'
    }
    & git -c 'core.excludesFile=' -C $repository check-ignore --quiet -- '.signing/zseries-development.pfx'
    if ($LASTEXITCODE -ne 0) { throw 'The signing directory must be Git-ignored before generating any key.' }
    $tracked = @(& git -c 'core.excludesFile=' -C $repository ls-files -- '.signing')
    if ($LASTEXITCODE -ne 0 -or $tracked.Count -gt 0) { throw 'Signing files must not be tracked by Git.' }
}
$matches = @(Get-ChildItem -Path $store | Where-Object { $_.Subject -eq $subject })
if ($matches.Count -gt 1) { throw 'Multiple matching certificates exist; select an identity explicitly instead of generating another.' }
if ($matches.Count -eq 1) {
    $certificate = $matches[0]
    $usage = $certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' }
    if (!$certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or
        !(@($usage.EnhancedKeyUsages.Value) -contains '1.3.6.1.5.5.7.3.3')) {
        throw 'The existing development identity is not usable for signing; do not replace it silently.'
    }
}
if (!$PSCmdlet.ShouldProcess($subject, 'Create or reuse a signing identity and export the requested certificate files (no trust installation)')) {
    return
}
if (!(Test-Path -LiteralPath $output)) {
    New-Item -ItemType Directory -Path $output | Out-Null
    # Limit access to the owner, SYSTEM and Administrators; Git-ignore is not access control.
    $acl = Get-Acl -LiteralPath $output
    $acl.SetAccessRuleProtection($true, $false)
    $owner = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl.SetOwner($owner)
    foreach ($identity in @($owner, [Security.Principal.SecurityIdentifier]::new('S-1-5-18'),
                            [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $identity, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $output -AclObject $acl
}
$publicFile = Join-Path $output 'zseries-development.cer'
$privateFile = Join-Path $output 'zseries-development.pfx'
$identityFile = Join-Path $output 'identity.json'
if (Test-Path -LiteralPath $identityFile) {
    $previous = Get-Content -LiteralPath $identityFile -Raw | ConvertFrom-Json
    if (!$certificate -or $previous.thumbprint -ne $certificate.Thumbprint) {
        throw 'The recorded identity differs from the certificate store; preserve the existing files.'
    }
}
if (Test-Path -LiteralPath $publicFile) {
    $previousPublic = [Security.Cryptography.X509Certificates.X509Certificate2]::new($publicFile)
    try {
        if (!$certificate -or $previousPublic.Thumbprint -ne $certificate.Thumbprint) {
            throw 'The public certificate file belongs to another identity; refusing to overwrite it.'
        }
    } finally { $previousPublic.Dispose() }
}
if (Test-Path -LiteralPath $privateFile) {
    $previousPrivate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $privateFile, [string]::Empty, [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
    try {
        if (!$certificate -or !$previousPrivate.HasPrivateKey -or $previousPrivate.Thumbprint -ne $certificate.Thumbprint) {
            throw 'The private-key file belongs to another identity; refusing to overwrite it.'
        }
    } finally { $previousPrivate.Dispose() }
}
if (!$certificate) {
    $certificate = New-SelfSignedCertificate -Type Custom -Subject $subject `
        -FriendlyName 'ZSeries Development - internal distribution' -CertStoreLocation $store `
        -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyUsage DigitalSignature `
        -KeyExportPolicy Exportable -Provider 'Microsoft Software Key Storage Provider' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={critical}{text}') `
        -NotBefore (Get-Date).AddMinutes(-5) -NotAfter (Get-Date).AddYears(3)
}
Export-Certificate -Cert $certificate -FilePath $publicFile -Type CERT | Out-Null
if ($ExportPrivateKeyWithoutPassword -and !(Test-Path -LiteralPath $privateFile)) {
    Write-Warning 'Exporting a passwordless PFX by explicit request. Anyone with this file can sign as this identity.'
    $privateBytes = $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx, [string]::Empty)
    try { [IO.File]::WriteAllBytes($privateFile, $privateBytes) }
    finally { [Array]::Clear($privateBytes, 0, $privateBytes.Length) }
}
$sha256 = [Security.Cryptography.SHA256]::Create()
try { $fingerprint = [BitConverter]::ToString($sha256.ComputeHash($certificate.RawData)).Replace('-', '') }
finally { $sha256.Dispose() }
$metadata = [ordered]@{
    subject = $certificate.Subject
    thumbprint = $certificate.Thumbprint
    sha256Fingerprint = $fingerprint
    certificateStore = $store
    publicCertificate = $publicFile
    notBeforeUtc = $certificate.NotBefore.ToUniversalTime().ToString('o')
    notAfterUtc = $certificate.NotAfter.ToUniversalTime().ToString('o')
    privateKeyExported = (Test-Path -LiteralPath $privateFile)
    privateKeyFile = $(if (Test-Path -LiteralPath $privateFile) { $privateFile } else { $null })
    privateKeyPasswordProtected = $false
    trustModified = $false
}
[IO.File]::WriteAllText($identityFile, ($metadata | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
# No TrustedPeople/TrustedPublisher/Root import occurs here. Never print private-key bytes.
[pscustomobject]$metadata
