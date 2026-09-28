param(
    [string]$BuildDirectory = 'build/msvc-dynamic-debug',
    [string]$Python = 'python',
    [switch]$InstallHeob,
    [switch]$IncludeGui,
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = (Resolve-Path (Join-Path $repo $BuildDirectory)).Path
$toolDirectory = Join-Path $repo 'build/tools/heob-4.2'
$tool = Join-Path $toolDirectory 'heob64.exe'
if (!(Test-Path $tool)) {
    if (!$InstallHeob) { throw 'Heob not installed; rerun with -InstallHeob' }
    # CMake/libarchive cannot decode every BCJ2/ARM64 stream in this archive.
    $sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
    if (!(Test-Path $sevenZip)) { $sevenZip = (Get-Command 7z -ErrorAction Stop).Source }
    $archive = Join-Path $repo 'build/tools/heob-4.2.7z'
    New-Item -ItemType Directory -Force $toolDirectory | Out-Null
    Invoke-WebRequest 'https://github.com/ssbssa/heob/releases/download/4.2/heob-4.2.7z' -OutFile $archive
    $expected = 'B8DBDCCFA87B6745FEB325D301B4ED98D860E821B8FD8158837420936DE36225'
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $expected) { throw 'Heob checksum mismatch' }
    & $sevenZip x $archive "-o$toolDirectory" '-y'
    if ($LASTEXITCODE -or !(Test-Path $tool)) { throw 'Heob extraction failed' }
}
foreach ($name in @('ztermy_leak_detector_probe', 'ztermy_qt_heap_lifetime_probe')) {
    if (!(Test-Path (Join-Path $build "$name.exe")) -or !(Test-Path (Join-Path $build "$name.pdb"))) {
        throw "Build $name with Debug/RelWithDebInfo before running this script"
    }
}
$output = Join-Path $repo ('build/memory/heob-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
$qtConfig = Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern '^Qt6_DIR:PATH=(.+)$'
if (!$qtConfig) { throw 'Qt6_DIR missing from CMake cache' }
$qtBin = (Resolve-Path (Join-Path $qtConfig.Matches[0].Groups[1].Value '../../../bin')).Path
$qtPlatforms = Join-Path $qtBin '../plugins/platforms'
$runs = [System.Collections.Generic.List[object]]::new()
function Run-Checked([string]$Name, [string]$Executable, [string[]]$Arguments) {
    $xml = Join-Path $output "$Name.xml"
    $log = Join-Path $output "$Name.txt"
    $summary = Join-Path $output "$Name.json"
    $start = [System.Diagnostics.ProcessStartInfo]::new($tool)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WorkingDirectory = $repo
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Stable hashing makes Qt hash-table capacity comparisons reproducible.
    $start.Environment['QT_HASH_SEED'] = '0'
    # No page-per-allocation protection, memory contents or child-process injection.
    # QML exit reports have many reachable cache stacks. GUI runs classify lost
    # blocks without serializing reachable stacks; small probes keep both.
    $leakMode = if ($Name.StartsWith('gui-')) { '-l2' } else { '-l3' }
    foreach ($arg in (@('-p0', $leakMode, '-L0', '-F1', "-y$build;$qtBin;$qtPlatforms", "-o$log", "-x$xml",
                        (Join-Path $build "$Executable.exe")) + $Arguments)) {
        $start.ArgumentList.Add($arg)
    }
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true) # Only this runner's owned process tree.
            $process.WaitForExit()
            throw "$Name timed out; result is invalid, not clean"
        }
        $stdout.GetAwaiter().GetResult() | Set-Content (Join-Path $output "$Name.stdout.txt")
        $stderr.GetAwaiter().GetResult() | Set-Content (Join-Path $output "$Name.stderr.txt")
        if ($process.ExitCode) { throw "$Name failed: $($process.ExitCode); inspect $output" }
    } finally { $process.Dispose() }
    & $Python (Join-Path $PSScriptRoot 'summarize_heob.py') $xml --output $summary | Write-Host
    if ($LASTEXITCODE) { throw "Invalid/incomplete report: $xml" }
    $result = Get-Content $summary -Raw | ConvertFrom-Json
    $runs.Add([pscustomobject]@{name=$Name;assessment=$result.assessment;categories=$result.categories;report=$xml})
    return $result
}
$clean = Run-Checked 'calibration-clean' 'ztermy_leak_detector_probe' @('--clean')
if ($clean.assessment -ne 'no-lost-blocks-observed') { throw 'Clean calibration failed' }
$positive = Run-Checked 'calibration-leak' 'ztermy_leak_detector_probe' @('--leak')
if ($positive.categories.Leak_DefinitelyLost.bytes -ne 4096 -or
    $positive.categories.Leak_DefinitelyLost.blocks -ne 1 -or
    !($positive.records.frames.file -contains 'leak_detector_probe.cpp')) {
    throw 'Positive calibration failed (including source symbols); do not trust later results'
}
$null = Run-Checked 'baseline' 'ztermy_qt_heap_lifetime_probe' @('baseline', '1')
foreach ($cycles in 1, 30) {
    $null = Run-Checked "logging-$cycles" 'ztermy_qt_heap_lifetime_probe' @('logging', "$cycles")
}
foreach ($cycles in 1, 10, 30) {
    $null = Run-Checked "icons-$cycles" 'ztermy_qt_heap_lifetime_probe' @('icons', "$cycles")
}
$null = Run-Checked 'callbacks-100' 'ztermy_qt_heap_lifetime_probe' @('callbacks', '100')
$null = Run-Checked 'local-lifecycle' 'ztermy_local_terminal_session_tests' @(
    'returnsCursorPositionQueryToLocalChild', 'repeatedlyStopsWithoutBlockingCaller',
    'buildsAtMostOneSnapshotPerDelivery', '-o', (Join-Path $output 'local-tests.txt,txt'))
$null = Run-Checked 'ai-model-lifecycle' 'ztermy_ai_conversation_model_tests' @(
    'streamsAssistantMessageAndUsage', 'preservesOnlyCurrentTurnImagePayloads', 'boundsMessagesAndUtf8Text',
    'exposesCancellationAsRetryableNeutralState', '-o', (Join-Path $output 'ai-tests.txt,txt'))
if ($IncludeGui) {
    foreach ($scenario in 'profile-icons', 'toolbar-hover') {
        $null = Run-Checked "gui-$scenario" 'ztermy' @("--$scenario-smoke", '--data-dir', (Join-Path $output "$scenario-data"))
    }
    foreach ($cycles in 10, 30) {
        $null = Run-Checked "gui-profile-icons-$cycles" 'ztermy' @('--profile-icons-smoke', "--profile-icon-cycles=$cycles",
            '--data-dir', (Join-Path $output "profile-icons-$cycles-data"))
    }
}
$manifest = [pscustomobject]@{
    build=$build; toolVersion='4.2'; toolSha256=(Get-FileHash $tool -Algorithm SHA256).Hash
    gitRevision=(& git -C $repo rev-parse HEAD); qtHashSeed='0'; runs=$runs
    note='Completion is not a no-leak claim. Review each loss stack; reachable bytes are not lost bytes.'
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $output 'manifest.json') -Encoding utf8
Write-Host "Completed calibrated measurements: $output"
if (@($runs | Where-Object { $_.name -ne 'calibration-leak' -and $_.assessment -eq 'needs-review' }).Count) {
    Write-Warning 'Allocation diagnostics require review. No automatic suppression was applied.'
}
