param(
    [string]$Port = "COM5",
    [int]$Baud = 115200,
    [int]$DurationSeconds = 30,
    [string]$IdfToolsPath = "",
    [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WorkspaceDir = Split-Path -Parent $ProjectDir

if ([string]::IsNullOrWhiteSpace($IdfToolsPath)) {
    $IdfToolsPath = Join-Path $WorkspaceDir ".espressif"
}

if ([string]::IsNullOrWhiteSpace($PythonExe)) {
    $PythonExe = Join-Path $IdfToolsPath "python_env\\idf5.5_py3.12_env\\Scripts\\python.exe"
}

if (-not (Test-Path $PythonExe)) {
    throw "ESP-IDF Python was not found: $PythonExe"
}

Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
        ($_.Name -like 'python*') -and
        ($_.CommandLine -match 'idf_monitor|monitor.py|esp_idf_monitor')
    } |
    ForEach-Object {
        try {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop
            Write-Host ("Stopped stale monitor PID {0}" -f $_.ProcessId)
        } catch {
        }
    }

Write-Host ("Resetting {0} into the flashed app..." -f $Port)
& $PythonExe -m esptool --chip esp32s3 -p $Port --before default_reset --after hard_reset chip_id | Out-Host

Start-Sleep -Milliseconds 400

$serialPort = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serialPort.DtrEnable = $false
$serialPort.RtsEnable = $false
$serialPort.ReadTimeout = 500
$serialPort.NewLine = "`n"

try {
    $serialPort.Open()
    Write-Host ("Reading {0} at {1} baud for {2} seconds..." -f $Port, $Baud, $DurationSeconds)

    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        try {
            $line = $serialPort.ReadLine()
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                Write-Output $line.TrimEnd("`r")
            }
        } catch [System.TimeoutException] {
        }
    }
} finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
    $serialPort.Dispose()
}
