param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [ValidateRange(10, 28800)]
    [int]$DurationSeconds = 1800,
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"
$resolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
$executable = Join-Path $resolvedBuild "ztermy_local_terminal_session_tests.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Local terminal test executable does not exist: $executable"
}

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $resolvedBuild "terminal-stability-soak.json"
}

# QtTest aborts a test function after five minutes unless this environment
# variable is raised. Keep a ten-minute grace period beyond the requested soak
# so final assertions and orderly ConPTY shutdown remain covered by a watchdog.
$functionTimeoutMilliseconds = ($DurationSeconds + 600) * 1000
$started = [DateTimeOffset]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$output = @(
    & cmake -E env `
        "ZTERMY_RUN_LOCAL_SOAK_GATE=1" `
        "ZTERMY_LOCAL_SOAK_SECONDS=$DurationSeconds" `
        "QTEST_FUNCTION_TIMEOUT=$functionTimeoutMilliseconds" `
        $executable `
        survivesSustainedInteractionWithoutLatencyGrowth 2>&1
)
$exitCode = $LASTEXITCODE
$stopwatch.Stop()

$report = [ordered]@{
    schemaVersion = 1
    startedAtUtc = $started.ToString("O")
    completedAtUtc = [DateTimeOffset]::UtcNow.ToString("O")
    requestedDurationSeconds = $DurationSeconds
    functionTimeoutMilliseconds = $functionTimeoutMilliseconds
    actualDurationMilliseconds = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds)
    failures = if ($exitCode -eq 0) { 0 } else { 1 }
    test = "survivesSustainedInteractionWithoutLatencyGrowth"
    buildDirectory = $resolvedBuild
}

$parent = Split-Path -Parent $ReportPath
if (-not [string]::IsNullOrWhiteSpace($parent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
$report | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $ReportPath -Encoding utf8NoBOM

if ($exitCode -ne 0) {
    $output | Select-Object -Last 80 | ForEach-Object { Write-Error $_ }
    throw "Terminal stability soak failed with exit code $exitCode. See $ReportPath"
}

Write-Host "Terminal stability soak passed: $([Math]::Round($stopwatch.Elapsed.TotalSeconds, 1)) second(s)."
Write-Host "Report: $ReportPath"
