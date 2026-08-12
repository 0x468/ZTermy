param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [ValidateRange(10, 86400)]
    [int]$DurationSeconds = 120,
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"
$resolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $resolvedBuild "ai-concurrency-soak.json"
}

$tests = @(
    "ai-turn-runner",
    "ai-agent-scenario",
    "ai-action-tool-dispatcher",
    "ai-agent-guard",
    "ai-tool-dispatch-ledger",
    "ai-read-tool-dispatcher",
    "ai-terminal-frame-tracker",
    "provider-http-client",
    "mcp-stdio-client",
    "mcp-runtime-manager",
    "ai-mcp-lifecycle-stress"
)
$expression = "^(" + (($tests | ForEach-Object { [Regex]::Escape($_) }) -join "|") + ")$"
$started = [DateTimeOffset]::UtcNow
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$iterations = 0
$failures = 0
$lastOutput = @()

while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSeconds -or $iterations -eq 0) {
    $lastOutput = @(& ctest --test-dir $resolvedBuild -R $expression --output-on-failure -j 2 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $failures++
        break
    }
    $iterations++
}
$stopwatch.Stop()

$report = [ordered]@{
    schemaVersion = 2
    startedAtUtc = $started.ToString("O")
    completedAtUtc = [DateTimeOffset]::UtcNow.ToString("O")
    requestedDurationSeconds = $DurationSeconds
    actualDurationMilliseconds = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds)
    iterations = $iterations
    failures = $failures
    tests = $tests
    buildDirectory = $resolvedBuild
}
$parent = Split-Path -Parent $ReportPath
if (-not [string]::IsNullOrWhiteSpace($parent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReportPath -Encoding utf8NoBOM

if ($failures -ne 0) {
    $lastOutput | ForEach-Object { Write-Error $_ }
    throw "AI concurrency soak failed. See $ReportPath"
}
Write-Host "AI concurrency soak passed: $iterations iteration(s), $([Math]::Round($stopwatch.Elapsed.TotalSeconds, 1)) second(s)."
Write-Host "Report: $ReportPath"
