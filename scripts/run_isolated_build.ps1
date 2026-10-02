[CmdletBinding()]
param(
    [ValidateSet('msvc-dynamic-debug', 'msvc-dynamic-release', 'msvc-static-release')]
    [string] $Preset = 'msvc-dynamic-debug',
    [string[]] $Targets = @(),
    [ValidateRange(1, 32)][int] $Jobs = 12
)
$ErrorActionPreference = 'Stop'
$Targets = @($Targets | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $repository ('build\' + $Preset)
$oldClink = $env:CLINK_NOAUTORUN
Push-Location $repository
try {
    # Process-local only; never edit the user's registry, profile or global env.
    $env:CLINK_NOAUTORUN = '1'
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    $normalized = 0
    foreach ($file in Get-ChildItem -LiteralPath $buildRoot -Filter '*.ninja' -File -Recurse) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected linked Ninja file.' }
        $text = [IO.File]::ReadAllText($file.FullName)
        # Mechanical normalization of generated artifacts, not source or user config.
        # CMake's Windows generator otherwise emits cmd.exe /C (AutoRun enabled).
        $clean = [regex]::Replace($text, '(?i)(cmd\.exe) /C ', '$1 /D /C ')
        if ($clean -ne $text) {
            [IO.File]::WriteAllText($file.FullName, $clean, [Text.UTF8Encoding]::new($false))
            $normalized++
        }
    }
    Write-Output "Isolated generated CMD wrappers in $normalized Ninja file(s)."
    $arguments = @('--build', '--preset', $Preset, '--parallel', "$Jobs")
    if ($Targets.Count -gt 0) { $arguments += @('--target') + $Targets }
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Isolated CMake build failed.' }
} finally {
    Pop-Location
    if ($null -eq $oldClink) {
        Remove-Item Env:CLINK_NOAUTORUN -ErrorAction SilentlyContinue
    } else {
        $env:CLINK_NOAUTORUN = $oldClink
    }
}
