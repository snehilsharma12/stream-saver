param(
    [string]$Python = "python",
    [string]$Worker = "worker\stream_saver_ocr.py"
)

$ErrorActionPreference = "Stop"

& $Python -m PyInstaller `
    --clean `
    --onefile `
    --name stream-saver-ocr `
    --distpath worker `
    --workpath build\pyinstaller `
    --specpath build\pyinstaller `
    --collect-all onnxruntime `
    --collect-all openvino `
    $Worker
