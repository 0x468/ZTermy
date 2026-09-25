param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [switch]$Repair
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = (Resolve-Path -LiteralPath (Join-Path $workspaceRoot 'build')).Path
$targetBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (-not $targetBuild.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only a specific build directory inside this repository/build is supported.'
}
$cache = Get-Content -LiteralPath (Join-Path $targetBuild 'CMakeCache.txt')
$ninjaEntry = $cache | Where-Object { $_ -match '^CMAKE_MAKE_PROGRAM:FILEPATH=' } | Select-Object -First 1
if (-not $ninjaEntry) { throw 'CMake did not record its build tool.' }
$ninjaPath = $ninjaEntry.Substring($ninjaEntry.IndexOf('=') + 1)
if ([IO.Path]::GetFileName($ninjaPath) -notin @('ninja', 'ninja.exe')) { throw 'This check requires Ninja.' }
$dependencies = & $ninjaPath -C $targetBuild -t deps
if ($LASTEXITCODE -ne 0) { throw 'Unable to read the Ninja dependency database.' }
$candidates = @(
    foreach ($line in $dependencies) {
        if ($line -notmatch '^(.+): #deps 0,') { continue }
        $relative = $Matches[1].Replace('\', '/')
        # Limit repair to generated objects for our own source, not dependency
        # packages, resource blobs, archives, executables, or arbitrary paths.
        if ($relative -notmatch '^CMakeFiles/[^/]+\.dir/src/.+\.(cpp|cc|cxx)\.obj$') { continue }
        $candidate = Join-Path $targetBuild $relative
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $resolved = (Resolve-Path -LiteralPath $candidate).Path
        if (-not $resolved.StartsWith($targetBuild + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Object path escaped the selected build directory: $relative"
        }
        $resolved
    }
)
foreach ($candidate in $candidates) {
    if ($Repair) { Remove-Item -LiteralPath $candidate }
    Write-Output $candidate
}
if ($Repair) {
    Write-Output "Removed $($candidates.Count) generated objects with missing header dependencies; rebuild through the existing preset."
} else {
    Write-Output "Found $($candidates.Count) generated objects with missing header dependencies. No files changed. Use -Repair to invalidate them."
}
