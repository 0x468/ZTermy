# Requires PowerShell 7 (ProcessStartInfo.ArgumentList and Environment).
[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [ValidateSet('idle', 'ui', 'terminal')][string]$Scenario = 'ui',
    [ValidateRange(1, 20)][int]$Repetitions = 3,
    [ValidateRange(5, 600)][int]$IdleSeconds = 30,
    [ValidateRange(30, 3600)][int]$TimeoutSeconds = 180,
    [ValidateRange(0, 2000)][int]$Chunks = 240,
    [ValidateSet('acrylic', 'mica', 'micaAlt', 'transparent', 'opaque')][string]$Backdrop = 'acrylic',
    [switch]$MemoryStages,
    [switch]$DetachScrollBar,
    [switch]$NoAiDelegates,
    [switch]$Lifecycle,
    [ValidateRange(1, 5)][int]$LifecycleCycles = 1,
    [switch]$Interactions,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use PowerShell 7.' }
if ([IntPtr]::Size -ne 8) { throw 'The native memory probe requires 64-bit PowerShell.' }
if (($MemoryStages -or $DetachScrollBar -or $NoAiDelegates) -and $Scenario -ne 'ui') { throw 'Memory diagnostics require Scenario ui.' }
if (($DetachScrollBar -or $NoAiDelegates) -and -not $MemoryStages) { throw 'Diagnostic controls require MemoryStages.' }
if ($Lifecycle -and (-not $MemoryStages -or $Interactions -or $DetachScrollBar -or $NoAiDelegates)) { throw 'Lifecycle requires plain MemoryStages.' }
if (-not $Lifecycle -and $LifecycleCycles -ne 1) { throw 'LifecycleCycles requires Lifecycle.' }
if ($Interactions -and (-not $MemoryStages -or $NoAiDelegates -or $DetachScrollBar -or $Chunks -lt 3)) { throw 'Interactions require displayed MemoryStages with at least 3 chunks.' }
if ($Scenario -eq 'idle' -and $IdleSeconds -ge $TimeoutSeconds) { throw 'TimeoutSeconds must exceed IdleSeconds.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MemoryProbe {
    [StructLayout(LayoutKind.Sequential)] public struct Counters {
        public uint cb, faults;
        public ulong peakWs, ws, peakPaged, paged, peakNonpaged, nonpaged, pagefile, peakPagefile, privateBytes;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Region {
        public ulong address, allocationBase;
        public uint allocationProtect, partitionId;
        public ulong size;
        public uint state, protect, type;
    }
    [DllImport("psapi.dll", SetLastError=true)] static extern bool GetProcessMemoryInfo(IntPtr h, out Counters c, uint size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern ulong VirtualQueryEx(IntPtr h, ulong address, out Region r, ulong size);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    public static Counters Read(IntPtr h) {
        Counters c;
        if (!GetProcessMemoryInfo(h, out c, (uint)Marshal.SizeOf(typeof(Counters)))) throw new System.ComponentModel.Win32Exception();
        return c;
    }
    public static ulong[] Regions(IntPtr h) {
        ulong cursor=0, reserved=0, committed=0, images=0, mapped=0, privateCommit=0, count=0;
        Region r;
        while (VirtualQueryEx(h,cursor,out r,(ulong)Marshal.SizeOf(typeof(Region))) != 0) {
            if (r.state==0x2000) reserved+=r.size;
            if (r.state==0x1000) {
                committed+=r.size;
                if(r.type==0x1000000) images+=r.size;
                if(r.type==0x40000) mapped+=r.size;
                if(r.type==0x20000) privateCommit+=r.size;
            }
            count++;
            ulong next=r.address+r.size;
            if(next<=cursor) break;
            cursor=next;
        }
        if(count==0) throw new System.ComponentModel.Win32Exception();
        return new ulong[]{reserved,committed,images,mapped,privateCommit};
    }
}
'@
if ($SelfTest) {
    $self = [Diagnostics.Process]::GetCurrentProcess()
    $c = [MemoryProbe]::Read($self.Handle)
    $v = [MemoryProbe]::Regions($self.Handle)
    if ($c.privateBytes -le 0 -or $c.ws -le 0 -or $v[1] -lt $v[4] -or $v[0] -le 0) { throw 'Native counter invariants failed.' }
    'Native memory/virtual-region probe passed.'
    return
}
$exe = (Resolve-Path -LiteralPath $Executable).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'OutputDirectory must be new: measurements never overwrite evidence.' }
New-Item -ItemType Directory -Path $output | Out-Null
$before = @(Get-CimInstance Win32_Process | Where-Object Name -in @('ztermy.exe','conhost.exe') | Select-Object ProcessId,ParentProcessId,Name,CreationDate)
$environment = [ordered]@{
    capturedUtc = [DateTime]::UtcNow.ToString('o'); executable = $exe
    sha256 = (Get-FileHash -LiteralPath $exe).Hash; scenario = $Scenario; repetitions = $Repetitions
    chunks = $Chunks; backdrop = $Backdrop; memoryStages = [bool]$MemoryStages; requestedScaleFactor = 1; requestedRhi = 'd3d11'
    detachScrollBar = [bool]$DetachScrollBar
    noAiDelegates = [bool]$NoAiDelegates
    lifecycle = [bool]$Lifecycle
    lifecycleCycles = $LifecycleCycles
    interactions = [bool]$Interactions
    timeoutSeconds = $TimeoutSeconds
    coldWarm = 'First observed launch then warm OS caches; fresh data directory each run. No cache flushing.'
    os = (Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,TotalVisibleMemorySize)
    cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)
    gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution)
    tools = @(foreach ($tool in 'wpr.exe','wpa.exe','vmmap.exe','umdh.exe','gflags.exe') {
        $command = Get-Command $tool -ErrorAction SilentlyContinue
        if ($command) { [ordered]@{name=$tool;path=$command.Source;version=(Get-Item $command.Source).VersionInfo.FileVersion} }
    })
    preexistingProcesses = $before
    limitations = @('250ms polling misses short peaks; GPU and VA queried at about 2s.',
        'PrivateUsage is process commit/private bytes, not an extra additive category.',
        'VA committed includes image/mapped pages; reserved excludes committed pages.',
        'GUI resources are USER/GDI handles, not QML objects or D3D textures.',
        'GPU counters may be unavailable; null is not zero. Child memory is excluded from parent counters.')
}
$environment | ConvertTo-Json -Depth 7 | Set-Content "$output/environment.json" -Encoding utf8
# WMI's first GPU-provider activation can take seconds: pay it before launching.
try { $null = Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory } catch { }
$summaries = @()
for ($run = 1; $run -le $Repetitions; $run++) {
    $runDir = Join-Path $output "run-$run"
    $dataDir = Join-Path $runDir 'data'
    New-Item -ItemType Directory -Path $dataDir | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = Split-Path $exe
    $start.ArgumentList.Add('--data-dir'); $start.ArgumentList.Add($dataDir)
    if ($Scenario -eq 'ui') { $start.ArgumentList.Add('--ui-performance-benchmark') }
    if ($Scenario -eq 'terminal') { $start.ArgumentList.Add('--performance-benchmark') }
    $start.Environment['QT_SCALE_FACTOR'] = '1'
    $start.Environment['QSG_RHI_BACKEND'] = 'd3d11'
    $start.Environment['QSG_RHI_PREFER_SOFTWARE_RENDERER'] = '0'
    $start.Environment['ZTERMY_UI_BENCHMARK_PAGE'] = 'ai'
    $start.Environment['ZTERMY_PERFORMANCE_BACKDROP'] = $Backdrop
    $start.Environment['ZTERMY_UI_BENCHMARK_CHUNKS'] = "$Chunks"
    $start.Environment['ZTERMY_UI_BENCHMARK_CAPTURE'] = '0'
    $start.Environment['ZTERMY_MEMORY_STAGES'] = if ($MemoryStages) { '1' } else { '0' }
    $start.Environment['ZTERMY_MEMORY_DETACH_SCROLLBAR'] = if ($DetachScrollBar) { '1' } else { '0' }
    $start.Environment['ZTERMY_MEMORY_NO_AI_DELEGATES'] = if ($NoAiDelegates) { '1' } else { '0' }
    $start.Environment['ZTERMY_MEMORY_LIFECYCLE'] = if ($Lifecycle) { '1' } else { '0' }
    $start.Environment['ZTERMY_MEMORY_LIFECYCLE_CYCLES'] = "$LifecycleCycles"
    $start.Environment['ZTERMY_MEMORY_INTERACTIONS'] = if ($Interactions) { '1' } else { '0' }
    $process = [Diagnostics.Process]::Start($start)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $rows = [Collections.Generic.List[object]]::new()
    $owned = @{}
    $owned[$process.Id] = $process.StartTime.ToUniversalTime()
    $nextSlow = 0.0; $lastCpu = 0.0; $lastTime = 0.0; $windowSized = $false
    $forced = $false; $gpuError = $null; $probeError = $null; $exitCode = $null
    try {
        while (-not $process.HasExited) {
            $process.Refresh()
            $elapsed = $watch.Elapsed.TotalSeconds
            if ($elapsed -gt $TimeoutSeconds) { $forced = $true; break }
            if ($Scenario -eq 'idle' -and -not $windowSized -and $process.MainWindowHandle -ne 0) {
                $windowSized = [MemoryProbe]::SetWindowPos($process.MainWindowHandle,[IntPtr]::Zero,80,80,1120,800,0x0014)
            }
            if ($Scenario -eq 'idle' -and $elapsed -ge $IdleSeconds) {
                [void]$process.CloseMainWindow()
                if (-not $process.WaitForExit(5000)) { $forced = $true }
                break
            }
            try { $c = [MemoryProbe]::Read($process.Handle) } catch { if ($process.HasExited) { break }; throw }
            $cpu = $process.TotalProcessorTime.TotalSeconds
            $row = [ordered]@{
                utc = [DateTime]::UtcNow.ToString('o'); elapsedSeconds = $elapsed; pid = $process.Id
                workingSetBytes = $c.ws; peakWorkingSetBytes = $c.peakWs
                privateBytes = $c.privateBytes; commitBytes = $c.pagefile; peakCommitBytes = $c.peakPagefile
                virtualBytes = $process.VirtualMemorySize64; threads = $process.Threads.Count; handles = $process.HandleCount
                cpuSeconds = $cpu; cpuOneCorePercent = if($rows.Count -gt 0 -and $elapsed -gt $lastTime){100*($cpu-$lastCpu)/($elapsed-$lastTime)}else{$null}
                userHandles = [MemoryProbe]::GetGuiResources($process.Handle,1); gdiHandles = [MemoryProbe]::GetGuiResources($process.Handle,0)
                windowDpi = [MemoryProbe]::GetDpiForWindow($process.MainWindowHandle)
                vaReservedBytes = $null; vaCommittedBytes = $null; vaImageCommitBytes = $null; vaMappedCommitBytes = $null; vaPrivateCommitBytes = $null
                gpuDedicatedBytes = $null; gpuSharedBytes = $null; slowProbeMilliseconds = $null
            }
            if ($elapsed -ge $nextSlow) {
                $slow = [Diagnostics.Stopwatch]::StartNew()
                $va = [MemoryProbe]::Regions($process.Handle)
                $row.vaReservedBytes=$va[0]; $row.vaCommittedBytes=$va[1]; $row.vaImageCommitBytes=$va[2]; $row.vaMappedCommitBytes=$va[3]; $row.vaPrivateCommitBytes=$va[4]
                try {
                    $gpu = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -ErrorAction Stop | Where-Object Name -Like "pid_$($process.Id)_*")
                    if ($gpu.Count -gt 0) {
                        $row.gpuDedicatedBytes=($gpu | Measure-Object DedicatedUsage -Sum).Sum
                        $row.gpuSharedBytes=($gpu | Measure-Object SharedUsage -Sum).Sum
                    }
                } catch { $gpuError = $_.Exception.Message }
                $all = @(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,CreationDate)
                # Capture descendants to validate only the processes this experiment owns.
                do {
                    $added = $false
                    foreach ($child in $all) {
                        if ($owned.ContainsKey([int]$child.ParentProcessId) -and -not $owned.ContainsKey([int]$child.ProcessId)) {
                            $owned[[int]$child.ProcessId] = $child.CreationDate.ToUniversalTime(); $added = $true
                        }
                    }
                } while ($added)
                $row.slowProbeMilliseconds = $slow.Elapsed.TotalMilliseconds
                $nextSlow = $watch.Elapsed.TotalSeconds + 2
            }
            $rows.Add([pscustomobject]$row)
            $lastCpu=$cpu; $lastTime=$elapsed
            Start-Sleep -Milliseconds 250
        }
    } catch { $probeError=$_.Exception.Message; $forced=$true }
    finally {
        if (-not $process.HasExited) { $process.Kill($true); $process.WaitForExit(); $forced=$true }
        $exitCode=$process.ExitCode
        Start-Sleep -Milliseconds 1000
        $survivors = @(foreach ($id in $owned.Keys) {
            $child=Get-Process -Id $id -ErrorAction SilentlyContinue
            if ($child -and $child.StartTime.ToUniversalTime() -eq $owned[$id]) { $child | Select-Object Id,ProcessName }
        })
        $rows | Export-Csv "$runDir/samples.csv" -NoTypeInformation
        $summary = [ordered]@{run=$run;pid=$process.Id;exitCode=$exitCode;forcedTermination=$forced;probeError=$probeError;gpuError=$gpuError;survivors=$survivors;sampleCount=$rows.Count;durationSeconds=$watch.Elapsed.TotalSeconds}
        if ($rows.Count -gt 0) {
            $summary.peakPrivateBytes=($rows | Measure-Object privateBytes -Maximum).Maximum
            $summary.peakWorkingSetBytes=($rows | Measure-Object workingSetBytes -Maximum).Maximum
            $summary.kernelPeakCommitBytes=($rows | Measure-Object peakCommitBytes -Maximum).Maximum
            $summary.lastPrivateBytes=$rows[-1].privateBytes
        }
        $summary | ConvertTo-Json -Depth 5 | Set-Content "$runDir/result.json"
        $summaries += [pscustomobject]$summary
        $process.Dispose()
    }
    if ($survivors.Count -gt 0 -or $probeError) { throw "Measurement/cleanup incomplete; inspect $runDir/result.json" }
}
$summaries | ConvertTo-Json -Depth 5 | Set-Content "$output/results.json"
$summaries | Format-Table run,exitCode,forcedTermination,sampleCount,peakPrivateBytes,peakWorkingSetBytes
if (@($summaries | Where-Object { $_.exitCode -ne 0 -or $_.forcedTermination }).Count -gt 0) {
    throw "One or more experiments failed or timed out. Evidence preserved at $output."
}
