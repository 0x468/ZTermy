param(
    [ValidateSet("msvc-static-release")]
    [string]$Preset = "msvc-static-release",
    [string]$OutputDirectory = "",
    [switch]$IncludeCompositionMatrix,
    [switch]$CollectOnly
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$buildDirectory = Join-Path $repositoryRoot "build\$Preset"
$capturedAt = [DateTimeOffset]::UtcNow
$captureId = $capturedAt.ToString("yyyyMMdd-HHmmss")
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $buildDirectory "test-data\performance-acceptance\$captureId"
}
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    Write-Host $Description
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Copy-EvidenceDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $source = Join-Path $buildDirectory "test-data\$Name"
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Expected performance evidence directory does not exist: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $resolvedOutput $Name) -Recurse
}

function OptionalRegistryValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    try {
        return (Get-ItemPropertyValue -LiteralPath $Path -Name $Name -ErrorAction Stop)
    } catch {
        return $null
    }
}

$operatingSystem = Get-CimInstance -ClassName Win32_OperatingSystem
$computerSystem = Get-CimInstance -ClassName Win32_ComputerSystem
$processors = @(Get-CimInstance -ClassName Win32_Processor | ForEach-Object {
        [ordered]@{
            name = $_.Name
            cores = $_.NumberOfCores
            logicalProcessors = $_.NumberOfLogicalProcessors
            maximumClockMHz = $_.MaxClockSpeed
        }
    })
$graphicsAdapters = @(Get-CimInstance -ClassName Win32_VideoController | ForEach-Object {
        [ordered]@{
            name = $_.Name
            driverVersion = $_.DriverVersion
            driverDate = if ($null -eq $_.DriverDate) { $null } else { $_.DriverDate.ToUniversalTime().ToString("o") }
            adapterRamBytes = $_.AdapterRAM
            horizontalPixels = $_.CurrentHorizontalResolution
            verticalPixels = $_.CurrentVerticalResolution
            refreshRateHz = $_.CurrentRefreshRate
            videoMode = $_.VideoModeDescription
        }
    })
$sourceCommit = (& git.exe -C $repositoryRoot rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw "Unable to identify the source commit for the performance evidence."
}
$sourceStatus = (& git.exe -C $repositoryRoot status --porcelain=v1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect the source worktree for the performance evidence."
}
if (-not [string]::IsNullOrWhiteSpace($sourceStatus)) {
    Write-Warning "The source worktree is dirty; the bundle records this and is not final release evidence."
}
$powerScheme = (& powercfg.exe /getactivescheme | Out-String).Trim()
$environmentEvidence = [ordered]@{
    schemaVersion = 1
    capturedAtUtc = $capturedAt.ToString("o")
    machine = [ordered]@{
        manufacturer = $computerSystem.Manufacturer
        model = $computerSystem.Model
        totalPhysicalMemoryBytes = $computerSystem.TotalPhysicalMemory
    }
    windows = [ordered]@{
        caption = $operatingSystem.Caption
        version = $operatingSystem.Version
        buildNumber = $operatingSystem.BuildNumber
        architecture = $operatingSystem.OSArchitecture
    }
    processors = $processors
    graphicsAdapters = $graphicsAdapters
    activePowerScheme = $powerScheme
    windowsTransparencyEnabled = OptionalRegistryValue `
        -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize" `
        -Name "EnableTransparency"
    benchmarkPreset = $Preset
    source = [ordered]@{
        commit = $sourceCommit
        dirty = -not [string]::IsNullOrWhiteSpace($sourceStatus)
    }
    compositionMatrixRequested = [bool]$IncludeCompositionMatrix
    collectOnly = [bool]$CollectOnly
}
$environmentPath = Join-Path $resolvedOutput "environment.json"
$environmentEvidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $environmentPath -Encoding utf8NoBOM

if (-not $CollectOnly) {
    Write-Host "Keep the Windows desktop unlocked and do not cover or minimize the benchmark window."
    Push-Location $repositoryRoot
    try {
        if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory "CMakeCache.txt") -PathType Leaf)) {
            Invoke-CheckedCommand -Executable "cmake" -Arguments @("--preset", $Preset) `
                -Description "Configuring the static Release build"
        }
        Invoke-CheckedCommand -Executable "cmake" `
            -Arguments @("--build", "--preset", $Preset, "--target", "ztermy_performance_baseline") `
            -Description "Running the terminal performance baseline"
        Invoke-CheckedCommand -Executable "cmake" `
            -Arguments @("--build", "--preset", $Preset, "--target", "ztermy_ui_performance_baseline") `
            -Description "Running the UI performance baseline"
        Invoke-CheckedCommand -Executable "cmake" `
            -Arguments @("--build", "--preset", $Preset, "--target", "ztermy_performance_report") `
            -Description "Building the strict performance report validator"

        $terminalReport = Join-Path $buildDirectory "test-data\performance-baseline\terminal-performance.json"
        $validator = Join-Path $buildDirectory "ztermy_performance_report.exe"
        Invoke-CheckedCommand -Executable $validator -Arguments @("--validate", $terminalReport) `
            -Description "Validating the terminal performance report"

        if ($IncludeCompositionMatrix) {
            Invoke-CheckedCommand -Executable "cmake" `
                -Arguments @("--build", "--preset", $Preset, "--target", "ztermy_composition_performance_matrix") `
                -Description "Running the five-material composition matrix"
        }
    } finally {
        Pop-Location
    }

    Copy-EvidenceDirectory -Name "performance-baseline"
    Copy-EvidenceDirectory -Name "performance-ui-baseline"
    if ($IncludeCompositionMatrix) {
        $matrixPath = Join-Path $buildDirectory "test-data\composition-performance-matrix.md"
        if (-not (Test-Path -LiteralPath $matrixPath -PathType Leaf)) {
            throw "Composition matrix report does not exist: $matrixPath"
        }
        Copy-Item -LiteralPath $matrixPath -Destination $resolvedOutput
        foreach ($name in @(
                "performance-acrylic",
                "performance-mica",
                "performance-micaAlt",
                "performance-transparent",
                "performance-opaque",
                "performance-ui-acrylic",
                "performance-ui-mica",
                "performance-ui-micaAlt",
                "performance-ui-transparent",
                "performance-ui-opaque"
            )) {
            Copy-EvidenceDirectory -Name $name
        }
    }
}

$observationTemplate = @"
# ztermy low-end performance observation

- Machine and owner label:
- Test date:
- Static Release commit: $sourceCommit
- Display DPI/scaling:
- Windows power mode:
- Windows transparency/battery saver:

## Terminal burst and resize

- Pass/fail:
- Visible stall or black/stale resize region:
- Input responsiveness while output is active:
- Final prompt and completion marker present:

## AI Markdown stream

- Pass/fail:
- Pointer/text selection stability:
- Manual scroll position retained away from bottom:
- Terminal remains responsive while streaming:

## Acrylic versus Mica

- Preferred material on this machine:
- Material-specific stall, GPU, or power observation:

## Overall result

- Pass means correctness is preserved and there is no sustained interaction stall above roughly 100 ms.
- Result:
- Notes or reproduction steps:
"@
$observationTemplate | Set-Content -LiteralPath (Join-Path $resolvedOutput "manual-observations.md") -Encoding utf8NoBOM

$files = @(Get-ChildItem -LiteralPath $resolvedOutput -File -Recurse | Sort-Object FullName)
$manifest = [ordered]@{
    schemaVersion = 1
    capturedAtUtc = $capturedAt.ToString("o")
    files = @($files | ForEach-Object {
            [ordered]@{
                path = [System.IO.Path]::GetRelativePath($resolvedOutput, $_.FullName).Replace('\', '/')
                bytes = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        })
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resolvedOutput "manifest.json") -Encoding utf8NoBOM

Write-Host "Performance acceptance evidence created: $resolvedOutput"
