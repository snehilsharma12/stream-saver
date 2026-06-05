param(
    [string]$Distro = "",
    [string]$ModelPath = "package\stream-saver\data\worker\yolo11n-text.onnx",
    [string]$ModelUrl = "",
    [switch]$SkipBuild,
    [switch]$SkipVenv,
    [switch]$SkipSmoke
)

$ErrorActionPreference = "Stop"

function Invoke-Wsl {
    param([string[]]$Arguments)

    if ($Distro) {
        & wsl.exe -d $Distro -- @Arguments
    } else {
        & wsl.exe -- @Arguments
    }
}

function ConvertTo-WslPath {
    param([string]$WindowsPath)

    if ($Distro) {
        return (& wsl.exe -d $Distro -- wslpath -a "$WindowsPath").Trim()
    }
    return (& wsl.exe -- wslpath -a "$WindowsPath").Trim()
}

function Quote-Bash {
    param([string]$Value)

    return "'" + ($Value -replace "'", "'\''") + "'"
}

$wslStatus = & wsl.exe --status 2>&1
if ($LASTEXITCODE -ne 0) {
    throw @"
WSL is not installed or not available.

Install it first, for example:
  wsl.exe --install -d Ubuntu

Then open Ubuntu once to finish setup and rerun this script.
"@
}

$repoWindowsPath = (Resolve-Path ".").Path
$repoLinuxPath = ConvertTo-WslPath $repoWindowsPath

$args = @("bash", "scripts/package-release-linux.sh")

if ($ModelPath) {
    $resolvedModel = (Resolve-Path $ModelPath).Path
    $modelLinuxPath = ConvertTo-WslPath $resolvedModel
    $args += @("--model-path", $modelLinuxPath)
} elseif ($ModelUrl) {
    $args += @("--model-url", $ModelUrl)
}

if ($SkipBuild) {
    $args += "--skip-build"
}
if ($SkipVenv) {
    $args += "--skip-venv"
}
if ($SkipSmoke) {
    $args += "--skip-smoke"
}

Write-Host "Running Linux release packaging through WSL at $repoLinuxPath"
$quotedArgs = $args | ForEach-Object { Quote-Bash $_ }
Invoke-Wsl @("bash", "-lc", "cd $(Quote-Bash $repoLinuxPath) && $($quotedArgs -join ' ')")
