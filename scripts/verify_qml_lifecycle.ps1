param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [string]$QtBinPath = '',
    [string]$TestDataRoot = (Join-Path $PSScriptRoot '../build/test-data'),
    [ValidateRange(1, 10)]
    [int]$Repeat = 3
)

$ErrorActionPreference = 'Stop'
$executable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$originalPath = $env:PATH
try {
    if ($QtBinPath) {
        $env:PATH = (Resolve-Path -LiteralPath $QtBinPath).Path + ';' + $env:PATH
    }
    for ($iteration = 1; $iteration -le $Repeat; ++$iteration) {
        $directory = Join-Path $TestDataRoot ('v5-qml-lifecycle-' + [guid]::NewGuid().ToString('N'))
        $directory = (New-Item -ItemType Directory -Path $directory -Force).FullName
        $process = Start-Process -FilePath $executable -ArgumentList @(
            '--history-scroll-smoke', '--data-dir', ('"' + $directory + '"')
        ) -WindowStyle Hidden -PassThru
        $log = Join-Path $directory 'logs/ztermy.log'
        if (!$process.WaitForExit(60000)) {
            throw "QML runtime timed out; preserved process $($process.Id), log=$log"
        }
        Write-Output "Lifecycle run $iteration exit=$($process.ExitCode) log=$log"
        if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log)) {
            throw 'QML lifecycle runtime did not complete successfully.'
        }
        $failures = Select-String -LiteralPath $log -Pattern @(
            'items in the process of being created',
            'Component destroyed while completion pending',
            'Cannot create new component instance before completing',
            '\bFATAL\b'
        )
        if ($failures) {
            $failures | ForEach-Object { Write-Output $_.Line }
            throw 'Unfinished QML construction or a fatal diagnostic was recorded.'
        }
    }
} finally {
    $env:PATH = $originalPath
}
