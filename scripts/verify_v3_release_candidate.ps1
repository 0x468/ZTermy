param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [string]$ReleaseDirectory = "",
    [string]$AiSoakReportPath = "",
    [string]$TerminalSoakReportPath = "",
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$ExpectedVersion = "0.3.0",
    [ValidateRange(10, 86400)]
    [int]$MinimumAiSoakSeconds = 7200,
    [ValidateRange(10, 28800)]
    [int]$MinimumTerminalSoakSeconds = 28800
)

$ErrorActionPreference = "Stop"
$resolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
$releaseStem = "ztermy-$ExpectedVersion-windows-x64"

if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $resolvedBuild "package\release\$releaseStem"
}
if ([string]::IsNullOrWhiteSpace($AiSoakReportPath)) {
    $AiSoakReportPath = Join-Path $resolvedBuild "ai-concurrency-soak-2h.json"
}
if ([string]::IsNullOrWhiteSpace($TerminalSoakReportPath)) {
    $TerminalSoakReportPath = Join-Path $resolvedBuild "terminal-stability-soak-8h.json"
}

function Read-JsonObject {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description does not exist: $Path"
    }
    try {
        return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    } catch {
        throw "$Description is not valid JSON: $Path ($($_.Exception.Message))"
    }
}

function Assert-SoakReport {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Report,
        [Parameter(Mandatory = $true)]
        [string]$Description,
        [Parameter(Mandatory = $true)]
        [int]$MinimumDurationSeconds
    )

    if ($Report.schemaVersion -ne 1) {
        throw "$Description has unsupported schemaVersion '$($Report.schemaVersion)'."
    }
    if ($Report.failures -ne 0) {
        throw "$Description records $($Report.failures) failure(s)."
    }
    if ($Report.requestedDurationSeconds -lt $MinimumDurationSeconds) {
        throw "$Description requested only $($Report.requestedDurationSeconds) second(s); at least $MinimumDurationSeconds required."
    }
    $requiredMilliseconds = [double]$Report.requestedDurationSeconds * 1000.0
    if ([double]$Report.actualDurationMilliseconds -lt $requiredMilliseconds) {
        throw "$Description ended before its requested duration."
    }
    if ([string]::IsNullOrWhiteSpace([string]$Report.startedAtUtc) -or
        [string]::IsNullOrWhiteSpace([string]$Report.completedAtUtc)) {
        throw "$Description is missing its UTC time bounds."
    }
    try {
        $startedAt = [DateTimeOffset]::Parse([string]$Report.startedAtUtc)
        $completedAt = [DateTimeOffset]::Parse([string]$Report.completedAtUtc)
    } catch {
        throw "$Description has invalid UTC time bounds."
    }
    if ($completedAt -lt $startedAt -or
        ($completedAt - $startedAt).TotalSeconds -lt $Report.requestedDurationSeconds) {
        throw "$Description time bounds do not cover its requested duration."
    }
}

$resolvedRelease = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$entries = @(Get-ChildItem -LiteralPath $resolvedRelease -Force)
if ($entries.Count -ne 4 -or @($entries | Where-Object { -not $_.PSIsContainer }).Count -ne 4) {
    throw "Release directory must contain exactly four files and no directories: $resolvedRelease"
}

$manifestPath = Join-Path $resolvedRelease "release-manifest.json"
$checksumsPath = Join-Path $resolvedRelease "SHA256SUMS.txt"
$manifest = Read-JsonObject -Path $manifestPath -Description "Release manifest"

if ($manifest.schemaVersion -ne 1 -or
    $manifest.product -ne "ztermy" -or
    $manifest.version -ne $ExpectedVersion -or
    $manifest.platform -ne "windows" -or
    $manifest.architecture -ne "x64" -or
    $manifest.checksumAlgorithm -ne "SHA-256") {
    throw "Release manifest identity does not match ztermy $ExpectedVersion for Windows x64."
}

$artifacts = @($manifest.artifacts)
if ($artifacts.Count -ne 2) {
    throw "Release manifest must describe exactly two artifacts."
}
$expectedArtifactNames = @(
    "$releaseStem-portable.zip",
    "$releaseStem.msi"
)
$expectedKinds = @("portable", "installer")
$checksumLines = [System.Collections.Generic.List[string]]::new()

for ($index = 0; $index -lt $artifacts.Count; $index++) {
    $artifact = $artifacts[$index]
    if ($artifact.kind -ne $expectedKinds[$index] -or
        $artifact.file -ne $expectedArtifactNames[$index] -or
        [string]$artifact.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw "Release artifact $index has an unexpected kind, name, or SHA-256 value."
    }
    $artifactPath = Join-Path $resolvedRelease $artifact.file
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "Release artifact does not exist: $artifactPath"
    }
    $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $artifact.sha256) {
        throw "Release artifact digest mismatch: $($artifact.file)"
    }
    $checksumLines.Add("$actualHash  $($artifact.file)")
}

if (-not (Test-Path -LiteralPath $checksumsPath -PathType Leaf)) {
    throw "Checksum manifest does not exist: $checksumsPath"
}
$writtenChecksumLines = @(Get-Content -LiteralPath $checksumsPath)
if (($writtenChecksumLines -join "`n") -ne ($checksumLines -join "`n")) {
    throw "SHA256SUMS.txt does not exactly match the verified release artifacts."
}

$aiReport = Read-JsonObject -Path $AiSoakReportPath -Description "AI concurrency soak report"
Assert-SoakReport -Report $aiReport -Description "AI concurrency soak report" -MinimumDurationSeconds $MinimumAiSoakSeconds
if ([System.IO.Path]::GetFullPath([string]$aiReport.buildDirectory) -ne $resolvedBuild) {
    throw "AI concurrency soak report belongs to another build directory."
}
if ($aiReport.iterations -lt 1) {
    throw "AI concurrency soak report contains no completed iteration."
}
$expectedAiTests = @(
    "ai-turn-runner",
    "ai-agent-guard",
    "ai-tool-dispatch-ledger",
    "ai-read-tool-dispatcher",
    "ai-terminal-frame-tracker",
    "provider-http-client",
    "mcp-stdio-client",
    "mcp-runtime-manager",
    "ai-mcp-lifecycle-stress"
)
if ((@($aiReport.tests) -join "`n") -ne ($expectedAiTests -join "`n")) {
    throw "AI concurrency soak report does not cover the approved test set."
}

$terminalReport = Read-JsonObject -Path $TerminalSoakReportPath -Description "Terminal stability soak report"
Assert-SoakReport -Report $terminalReport -Description "Terminal stability soak report" -MinimumDurationSeconds $MinimumTerminalSoakSeconds
if ([System.IO.Path]::GetFullPath([string]$terminalReport.buildDirectory) -ne $resolvedBuild) {
    throw "Terminal stability soak report belongs to another build directory."
}
if ($terminalReport.test -ne "survivesSustainedInteractionWithoutLatencyGrowth") {
    throw "Terminal stability soak report names an unexpected test."
}
$minimumFunctionTimeout = ([int64]$terminalReport.requestedDurationSeconds + 600) * 1000
if ([int64]$terminalReport.functionTimeoutMilliseconds -lt $minimumFunctionTimeout) {
    throw "Terminal stability soak watchdog does not include the required ten-minute grace period."
}

Write-Host "V3 release-candidate evidence verified for ztermy $ExpectedVersion."
Write-Host "Release: $resolvedRelease"
Write-Host "AI soak: $($aiReport.iterations) iteration(s), $([Math]::Round($aiReport.actualDurationMilliseconds / 1000.0, 1)) second(s)"
Write-Host "Terminal soak: $([Math]::Round($terminalReport.actualDurationMilliseconds / 1000.0, 1)) second(s)"
