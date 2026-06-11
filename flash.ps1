param(
    [string]$Port = "",
    [int]$Baud = 460800,
    [switch]$NoBuild,
    [switch]$NoMonitor,
    [string]$IdfPath = "",
    [string]$IdfToolsPath = "",
    [switch]$SkipProcessCleanup
)

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WorkspaceDir = Split-Path -Parent $ProjectDir

function Test-SerialPortAvailable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PortName
    )

    try {
        $serialPort = [System.IO.Ports.SerialPort]::new($PortName, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $serialPort.Open()
        $serialPort.Close()
        $serialPort.Dispose()
        return $true
    } catch {
        return $false
    }
}

function Get-PreferredSerialPort {
    $ports = @(Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue)
    if (-not $ports) {
        return ""
    }

    $espressifPort = $ports |
        Where-Object { $_.PNPDeviceID -match 'VID_303A' } |
        Select-Object -First 1

    if ($espressifPort) {
        return $espressifPort.DeviceID
    }

    $usbSerialPort = $ports |
        Where-Object { $_.Name -match 'USB Serial' -or $_.Description -match 'USB Serial' } |
        Select-Object -First 1

    if ($usbSerialPort) {
        return $usbSerialPort.DeviceID
    }

    return ""
}

function Stop-StaleIdfProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$IdfRoot
    )

    $currentPid = $PID
    $candidateNames = @(
        'python.exe',
        'pythonw.exe',
        'powershell.exe',
        'pwsh.exe',
        'cmd.exe',
        'ninja.exe',
        'cmake.exe',
        'ccache.exe',
        'xtensa-esp32s3-elf-gcc.exe',
        'xtensa-esp32s3-elf-g++.exe',
        'xtensa-esp-elf-gcc.exe',
        'xtensa-esp-elf-g++.exe',
        'cc1.exe',
        'cc1plus.exe',
        'ld.exe',
        'collect2.exe'
    )
    $processes = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $candidateNames -contains $_.Name.ToLowerInvariant()
        })

    foreach ($process in $processes) {
        if ($process.ProcessId -eq $currentPid) {
            continue
        }

        $commandLine = $process.CommandLine
        if ([string]::IsNullOrWhiteSpace($commandLine)) {
            continue
        }

        $isProjectRelated = $commandLine -like "*$ProjectPath*" -or $commandLine -like "*$IdfRoot*"
        if (-not $isProjectRelated) {
            continue
        }

        $looksLikeIdfWorker =
            $commandLine -match 'idf\.py' -or
            $commandLine -match 'esptool' -or
            $commandLine -match 'serial\.tools\.miniterm' -or
            $commandLine -match '\bmonitor\b' -or
            $commandLine -match 'flash\.ps1' -or
            $commandLine -match '\bninja(\.exe)?\b' -or
            $commandLine -match '\bcmake(\.exe)?\b' -or
            $commandLine -match '\bccache(\.exe)?\b' -or
            $commandLine -match 'xtensa-esp'

        if (-not $looksLikeIdfWorker) {
            continue
        }

        try {
            Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
            Write-Host ("Stopped stale process: {0} (PID {1})" -f $process.Name, $process.ProcessId)
        } catch {
            Write-Warning ("Failed to stop PID {0}: {1}" -f $process.ProcessId, $_.Exception.Message)
        }
    }
}

function Reset-BuildMetadata {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    $buildDir = Join-Path $ProjectPath "build"
    $targets = @(
        (Join-Path $buildDir ".ninja_deps"),
        (Join-Path $buildDir ".ninja_log"),
        (Join-Path $buildDir "build.ninja"),
        (Join-Path $buildDir "rules.ninja"),
        (Join-Path $buildDir "CMakeCache.txt")
    )

    foreach ($target in $targets) {
        if (Test-Path $target) {
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        }
    }

    $cmakeFilesDir = Join-Path $buildDir "CMakeFiles"
    if (Test-Path $cmakeFilesDir) {
        Remove-Item -LiteralPath $cmakeFilesDir -Force -Recurse -ErrorAction SilentlyContinue
    }
}

function Invoke-IdfCommandWithRecovery {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$IdfRoot,

        [switch]$AllowBuildRecovery
    )

    & idf.py @Arguments
    if ($LASTEXITCODE -eq 0) {
        return
    }

    if (-not $AllowBuildRecovery) {
        throw ("idf.py {0} failed with exit code {1}" -f ($Arguments -join " "), $LASTEXITCODE)
    }

    Write-Warning "idf.py build failed. Cleaning stale build processes and Ninja metadata, then retrying once..."
    Stop-StaleIdfProcesses -ProjectPath $ProjectPath -IdfRoot $IdfRoot
    Reset-BuildMetadata -ProjectPath $ProjectPath
    Start-Sleep -Milliseconds 750

    & idf.py @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("idf.py {0} failed after recovery with exit code {1}" -f ($Arguments -join " "), $LASTEXITCODE)
    }
}

function Wait-ForSerialPortRelease {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PortName,

        [int]$TimeoutMs = 4000
    )

    if ([string]::IsNullOrWhiteSpace($PortName)) {
        return
    }

    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        if (Test-SerialPortAvailable -PortName $PortName) {
            return
        }
        Start-Sleep -Milliseconds 250
    }

    Write-Warning "Port $PortName is still busy after cleanup. A monitor or terminal may still be attached."
}

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

    if ($IdfCandidates) {
        $IdfPath = $IdfCandidates[0]
    }
}

$ExportScript = Join-Path $IdfPath "export.ps1"

if (-not (Test-Path $ExportScript)) {
    throw "ESP-IDF export script not found: $ExportScript"
}

Write-Host "Project: $ProjectDir"
Write-Host "Using IDF_PATH: $IdfPath"
Write-Host "Using IDF_TOOLS_PATH: $IdfToolsPath"

$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $IdfToolsPath

if (-not $SkipProcessCleanup) {
    Write-Host "Cleaning up stale ESP-IDF monitor/flash processes..."
    Stop-StaleIdfProcesses -ProjectPath $ProjectDir -IdfRoot $IdfPath
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Get-PreferredSerialPort
    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        Write-Host "Auto-selected serial port: $Port"
    }
}

if (-not [string]::IsNullOrWhiteSpace($Port)) {
    Wait-ForSerialPortRelease -PortName $Port
}
. $ExportScript
$env:IDF_CCACHE_ENABLE = "0"
Write-Host "Using IDF_CCACHE_ENABLE: $($env:IDF_CCACHE_ENABLE)"

Push-Location $ProjectDir
try {
    if (-not $NoBuild) {
        Write-Host "Building..."
        Invoke-IdfCommandWithRecovery -Arguments @("build") -ProjectPath $ProjectDir -IdfRoot $IdfPath -AllowBuildRecovery
    }

    $args = @()
    if ($Port -ne "") {
        $args += "-p"
        $args += $Port
    }
    $args += "-b"
    $args += "$Baud"
    $args += "flash"
    if (-not $NoMonitor) {
        $args += "monitor"
    }

    Write-Host ("Running: idf.py " + ($args -join " "))
    Invoke-IdfCommandWithRecovery -Arguments $args -ProjectPath $ProjectDir -IdfRoot $IdfPath
}
finally {
    Pop-Location
}
