param(
    [string]$BuildDir = "build_x64",
    [string]$Config = "RelWithDebInfo",
    [string]$PackageRoot = "package\stream-saver",
    [string]$OutputDir = "dist",
    [string]$CMake = "",
    [string]$ModelPath = "",
    [string]$ModelUrl = "",
    [string]$PythonVersion = "3.13.3",
    [int]$SmokePort = 48743,
    [switch]$SkipBuild,
    [switch]$SkipManagedWorker,
    [switch]$SkipSmokeTest
)

$ErrorActionPreference = "Stop"

function Resolve-CMake {
    param([string]$ConfiguredPath)

    if ($ConfiguredPath) {
        return $ConfiguredPath
    }

    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $visualStudioCMake =
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $visualStudioCMake) {
        return $visualStudioCMake
    }

    throw "cmake was not found. Pass -CMake with the full path to cmake.exe."
}

function Ensure-Model {
    param(
        [string]$WorkerRoot,
        [string]$SourcePath,
        [string]$DownloadUrl
    )

    $target = Join-Path $WorkerRoot "yolo11n-text.onnx"
    if (Test-Path $target) {
        Write-Host "Model already staged at $target"
        return
    }

    if ($SourcePath) {
        if (!(Test-Path $SourcePath)) {
            throw "ModelPath does not exist: $SourcePath"
        }
        Copy-Item -Force $SourcePath $target
        Write-Host "Copied model to $target"
        return
    }

    if ($DownloadUrl) {
        Invoke-WebRequest -Uri $DownloadUrl -OutFile $target
        Write-Host "Downloaded model to $target"
        return
    }

    throw @"
Missing yolo11n-text.onnx.

Pass -ModelPath C:\path\to\yolo11n-text.onnx, pass -ModelUrl, or place
yolo11n-text.onnx in $WorkerRoot before packaging.
"@
}

function Quote-Argument {
    param([string]$Value)

    '"' + ($Value -replace '"', '\"') + '"'
}

function Invoke-SmokeTest {
    param(
        [string]$WorkerRoot,
        [int]$Port
    )

    $python = Join-Path $WorkerRoot "python\python.exe"
    $worker = Join-Path $WorkerRoot "stream_saver_ocr.py"
    $model = Join-Path $WorkerRoot "yolo11n-text.onnx"
    $out = Join-Path $PWD "build\release-worker-smoke.out.log"
    $err = Join-Path $PWD "build\release-worker-smoke.err.log"

    if (!(Test-Path $python)) {
        throw "Managed Python runtime missing: $python"
    }
    if (!(Test-Path $model)) {
        throw "Model missing: $model"
    }

    New-Item -ItemType Directory -Force "build" | Out-Null
    Remove-Item -Force $out, $err -ErrorAction SilentlyContinue

    $argumentList = @(
        (Quote-Argument $worker),
        "--port",
        "$Port",
        "--backend",
        "onnxruntime",
        "--model",
        (Quote-Argument $model),
        "--no-recognize"
    ) -join " "

    $process = Start-Process -FilePath $python -ArgumentList $argumentList -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $out -RedirectStandardError $err

    try {
        Start-Sleep -Seconds 8
        if ($process.HasExited) {
            Write-Host "Worker stderr:"
            Get-Content $err -ErrorAction SilentlyContinue
            Write-Host "Worker stdout:"
            Get-Content $out -ErrorAction SilentlyContinue
            throw "Release smoke worker exited with code $($process.ExitCode)."
        }

        & $python "scripts\ocr_smoke.py" $Port
    } finally {
        if ($process -and !$process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
}

$packageRootResolved = Join-Path $PWD $PackageRoot
$workerRoot = Join-Path $packageRootResolved "data\worker"
$outputRoot = Join-Path $PWD $OutputDir
$zipPath = Join-Path $outputRoot "stream-saver-windows-x64.zip"

if (!$SkipBuild) {
    $cmakePath = Resolve-CMake $CMake
    & $cmakePath --build $BuildDir --config $Config --target stream-saver
    & $cmakePath --install $BuildDir --config $Config --prefix package
}

if (!(Test-Path $workerRoot)) {
    throw "Worker package directory missing: $workerRoot"
}

Ensure-Model -WorkerRoot $workerRoot -SourcePath $ModelPath -DownloadUrl $ModelUrl

if (!$SkipManagedWorker) {
    & "scripts\package-managed-worker.ps1" -PythonVersion $PythonVersion -PackageRoot (Join-Path $PackageRoot "data\worker")
}

if (!$SkipSmokeTest) {
    Invoke-SmokeTest -WorkerRoot $workerRoot -Port $SmokePort
}

New-Item -ItemType Directory -Force $outputRoot | Out-Null
Compress-Archive -Path $packageRootResolved -DestinationPath $zipPath -Force

Write-Host "Release package created: $zipPath"
Write-Host "Users can extract this zip so stream-saver sits under OBS's plugins folder."
