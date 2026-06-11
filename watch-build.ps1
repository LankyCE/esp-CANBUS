param(
    [string]$IdfPath = "",
    [string]$IdfToolsPath = "",
    [int]$InactivitySeconds = 180,
    [int]$PollSeconds = 5,
    [switch]$VerboseIdf,
    [switch]$DisableCcache = $true,
    [int]$ParallelLevel = 1
)

$ErrorActionPreference = "Stop"

function Get-ChildProcessIds {
    param([int]$RootPid)

    $all = Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId
    $childrenByParent = @{}
    foreach ($p in $all) {
        if (-not $childrenByParent.ContainsKey($p.ParentProcessId)) {
            $childrenByParent[$p.ParentProcessId] = @()
        }
        $childrenByParent[$p.ParentProcessId] += [int]$p.ProcessId
    }

    $result = New-Object System.Collections.Generic.HashSet[int]
    $queue = New-Object System.Collections.Generic.Queue[int]
    $queue.Enqueue($RootPid)
    [void]$result.Add($RootPid)

    while ($queue.Count -gt 0) {
        $pid = $queue.Dequeue()
        if ($childrenByParent.ContainsKey($pid)) {
            foreach ($child in $childrenByParent[$pid]) {
                if ($result.Add($child)) {
                    $queue.Enqueue($child)
                }
            }
        }
    }

    return @($result)
}

function Get-ProcessSnapshot {
    param([int[]]$Pids)

    $snap = @()
    foreach ($pid in $Pids) {
        try {
            $gp = Get-Process -Id $pid -ErrorAction Stop
            $wmi = Get-CimInstance Win32_Process -Filter "ProcessId = $pid" -ErrorAction SilentlyContinue
            $snap += [pscustomobject]@{
                Id          = $gp.Id
                Name        = $gp.ProcessName
                CPU         = [double]($(if ($null -ne $gp.CPU) { $gp.CPU } else { 0 }))
                WS_MB       = [math]::Round($gp.WorkingSet64 / 1MB, 1)
                StartTime   = $gp.StartTime
                CommandLine = if ($wmi) { $wmi.CommandLine } else { "" }
            }
        } catch {
        }
    }
    return $snap | Sort-Object Name, Id
}

function Stop-ProcessTree {
    param([int]$RootPid)

    $pids = Get-ChildProcessIds -RootPid $RootPid | Sort-Object -Descending
    foreach ($pid in $pids) {
        try {
            Stop-Process -Id $pid -Force -ErrorAction Stop
        } catch {
        }
    }
    return $pids
}

$ProjectDir = $PSScriptRoot
$WorkspaceDir = Split-Path -Parent $ProjectDir

if ([string]::IsNullOrWhiteSpace($IdfToolsPath)) {
    $IdfToolsPath = Join-Path $WorkspaceDir ".espressif"
}

if ([string]::IsNullOrWhiteSpace($IdfPath)) {
    $IdfCandidates = @(
        Get-ChildItem -Path $IdfToolsPath -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "esp-idf" } |
            Where-Object { Test-Path $_ }
    )
    if ($IdfCandidates) { $IdfPath = $IdfCandidates[0] }
}

$ProjectDir = (Resolve-Path $ProjectDir).Path
if (Test-Path $IdfToolsPath) { $IdfToolsPath = (Resolve-Path $IdfToolsPath).Path }
if ($IdfPath -and (Test-Path $IdfPath)) { $IdfPath = (Resolve-Path $IdfPath).Path }

$ExportScript = Join-Path $IdfPath "export.ps1"
if (-not (Test-Path $ExportScript)) {
    throw "ESP-IDF export script not found: $ExportScript"
}

$LogDir = Join-Path $ProjectDir "build\watch"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$StdoutLog = Join-Path $LogDir "build_$Stamp.stdout.log"
$StderrLog = Join-Path $LogDir "build_$Stamp.stderr.log"
$MetaLog = Join-Path $LogDir "build_$Stamp.meta.log"
$SnapshotLog = Join-Path $LogDir "build_$Stamp.hang-snapshot.log"

$idfCmd = if ($VerboseIdf) { "idf.py -v build" } else { "idf.py build" }

$childScript = @"
`$ErrorActionPreference = 'Stop'
Set-Location '$ProjectDir'
`$env:IDF_PATH = '$IdfPath'
`$env:IDF_TOOLS_PATH = '$IdfToolsPath'
if ($ParallelLevel -gt 0) { `$env:CMAKE_BUILD_PARALLEL_LEVEL = '$ParallelLevel' }
. '$ExportScript'
if ('$DisableCcache' -eq 'True') { `$env:IDF_CCACHE_ENABLE = '0' }
$idfCmd
"@

Set-Content -Path $MetaLog -Value @(
    "Started: $(Get-Date -Format o)"
    "ProjectDir: $ProjectDir"
    "IDF_PATH: $IdfPath"
    "IDF_TOOLS_PATH: $IdfToolsPath"
    "DisableCcache: $DisableCcache"
    "ParallelLevel: $ParallelLevel"
    "InactivitySeconds: $InactivitySeconds"
    "PollSeconds: $PollSeconds"
    "Command: $idfCmd"
    ""
) -Encoding UTF8

Write-Host "Project: $ProjectDir"
Write-Host "IDF_PATH: $IdfPath"
Write-Host "IDF_TOOLS_PATH: $IdfToolsPath"
Write-Host "stdout log: $StdoutLog"
Write-Host "stderr log: $StderrLog"
Write-Host "meta log:   $MetaLog"
Write-Host "Hang timeout: $InactivitySeconds sec (poll $PollSeconds sec)"
Write-Host "Launching build..."

$proc = Start-Process powershell.exe `
    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", $childScript) `
    -WorkingDirectory $ProjectDir `
    -RedirectStandardOutput $StdoutLog `
    -RedirectStandardError $StderrLog `
    -PassThru

$lastOutPos = 0L
$lastErrPos = 0L
$lastOutputActivity = Get-Date
$lastCpuActivity = Get-Date
$prevCpuTotal = 0.0

Write-Host "Watcher PID(root): $($proc.Id)"

while (-not $proc.HasExited) {
    Start-Sleep -Seconds $PollSeconds
    $proc.Refresh()

    $outLen = if (Test-Path $StdoutLog) { (Get-Item $StdoutLog).Length } else { 0L }
    $errLen = if (Test-Path $StderrLog) { (Get-Item $StderrLog).Length } else { 0L }

    $outChanged = $outLen -gt $lastOutPos
    $errChanged = $errLen -gt $lastErrPos

    if ($outChanged -or $errChanged) {
        $lastOutputActivity = Get-Date
    }

    if ($outChanged) {
        $lines = Get-Content $StdoutLog -Tail 20
        if ($lines) {
            Write-Host "`n--- stdout tail ($(Get-Date -Format T)) ---"
            $lines | ForEach-Object { Write-Host $_ }
        }
        $lastOutPos = $outLen
    }

    if ($errChanged) {
        $lines = Get-Content $StderrLog -Tail 20
        if ($lines) {
            Write-Host "`n--- stderr tail ($(Get-Date -Format T)) ---"
            $lines | ForEach-Object { Write-Host $_ }
        }
        $lastErrPos = $errLen
    }

    $treePids = Get-ChildProcessIds -RootPid $proc.Id
    $snap = Get-ProcessSnapshot -Pids $treePids
    $cpuTotal = ($snap | Measure-Object -Property CPU -Sum).Sum
    if ($null -eq $cpuTotal) { $cpuTotal = 0.0 }
    $cpuDelta = [double]$cpuTotal - [double]$prevCpuTotal
    if ($cpuDelta -gt 0.05) {
        $lastCpuActivity = Get-Date
    }
    $prevCpuTotal = [double]$cpuTotal

    $idleOutputSec = [int]((Get-Date) - $lastOutputActivity).TotalSeconds
    $idleCpuSec = [int]((Get-Date) - $lastCpuActivity).TotalSeconds
    $top = $snap | Sort-Object CPU -Descending | Select-Object -First 6 Id, Name, CPU, WS_MB

    Write-Host ("[watch] idle(output)={0}s idle(cpu)={1}s cpuDelta={2:n2}s procs={3}" -f $idleOutputSec, $idleCpuSec, $cpuDelta, $snap.Count)
    if ($top) {
        $top | Format-Table -AutoSize | Out-String | Write-Host
    }

    if ($idleOutputSec -ge $InactivitySeconds -and $idleCpuSec -ge $InactivitySeconds) {
        $hangMsg = "Potential hang detected at $(Get-Date -Format o): no output and no CPU progress for $InactivitySeconds seconds."
        Write-Warning $hangMsg
        Add-Content -Path $MetaLog -Value $hangMsg

        $snapshotText = @()
        $snapshotText += "Timestamp: $(Get-Date -Format o)"
        $snapshotText += "Root PID: $($proc.Id)"
        $snapshotText += "IdleOutputSec: $idleOutputSec"
        $snapshotText += "IdleCpuSec: $idleCpuSec"
        $snapshotText += ""
        $snapshotText += "Process snapshot:"
        $snapshotText += ($snap | Format-List * | Out-String)
        $snapshotText += ""
        $snapshotText += "Last stdout lines:"
        $snapshotText += (Get-Content $StdoutLog -Tail 100 | Out-String)
        $snapshotText += ""
        $snapshotText += "Last stderr lines:"
        $snapshotText += (Get-Content $StderrLog -Tail 100 | Out-String)
        Set-Content -Path $SnapshotLog -Value $snapshotText -Encoding UTF8

        $killed = Stop-ProcessTree -RootPid $proc.Id
        Add-Content -Path $MetaLog -Value ("Killed PIDs: " + ($killed -join ", "))
        Write-Host "Killed process tree: $($killed -join ', ')"
        break
    }
}

$proc.Refresh()
$end = Get-Date
Add-Content -Path $MetaLog -Value @(
    "Ended: $($end.ToString('o'))"
    "ExitCode: $($proc.ExitCode)"
)

Write-Host ""
Write-Host "Build watcher finished. ExitCode=$($proc.ExitCode)"
Write-Host "stdout: $StdoutLog"
Write-Host "stderr: $StderrLog"
if (Test-Path $SnapshotLog) {
    Write-Host "hang snapshot: $SnapshotLog"
}

exit $proc.ExitCode
