param(
    [string]$PythonVersion = "3.13.3",
    [string]$Architecture = "amd64",
    [string]$PackageRoot = "package\stream-saver\data\worker"
)

$ErrorActionPreference = "Stop"

$workerRoot = Resolve-Path $PackageRoot
$pythonRoot = Join-Path $workerRoot "python"
$downloadDir = Join-Path $PWD "build\python-embed"
$zipPath = Join-Path $downloadDir "python-$PythonVersion-embed-$Architecture.zip"
$url = "https://www.python.org/ftp/python/$PythonVersion/python-$PythonVersion-embed-$Architecture.zip"

New-Item -ItemType Directory -Force $downloadDir | Out-Null
New-Item -ItemType Directory -Force $pythonRoot | Out-Null

if (!(Test-Path $zipPath)) {
    Invoke-WebRequest -Uri $url -OutFile $zipPath
}

Expand-Archive -Path $zipPath -DestinationPath $pythonRoot -Force

$pth = Get-ChildItem $pythonRoot -Filter "python*._pth" | Select-Object -First 1
if ($pth) {
    $content = Get-Content $pth.FullName
    $content = $content | ForEach-Object {
        if ($_ -eq "#import site") { "import site" } else { $_ }
    }
    Set-Content -Path $pth.FullName -Value $content -Encoding ASCII
}

$getPip = Join-Path $downloadDir "get-pip.py"
if (!(Test-Path $getPip)) {
    Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile $getPip
}

& (Join-Path $pythonRoot "python.exe") $getPip
& (Join-Path $pythonRoot "python.exe") -m pip install --upgrade pip
& (Join-Path $pythonRoot "python.exe") -m pip install -r "worker\requirements.txt"

Write-Host "Managed worker runtime created at $pythonRoot"
Write-Host "The plugin will prefer $pythonRoot\python.exe over global Python and the PyInstaller worker."
