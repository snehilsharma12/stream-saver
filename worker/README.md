# Stream Saver OCR Worker

The worker runs PaddleOCR out of process and listens on `127.0.0.1:48741` by default.

Development:

```bash
python -m pip install -r requirements.txt
python stream_saver_ocr.py --port 48741
```

Use Python 3.9-3.13 for the worker environment. The latest PaddlePaddle release currently provides wheels through CPython 3.13, so Python 3.14 will not install the pinned stack.

The worker disables PaddleOCR's MKL-DNN/oneDNN CPU backend by default because PaddlePaddle 3.x can fail on some Windows CPU/runtime combinations during OCR inference.

Health check:

```bash
printf '{"type":"health"}\n' | nc 127.0.0.1 48741
```

Release packaging should bundle a managed Python runtime in `data/worker/python`
so users do not need global Python or global PaddleOCR. The plugin looks for
workers in this order on Windows:

1. `data/worker/python/python.exe` running `data/worker/stream_saver_ocr.py`
2. `data/worker/stream_saver_ocr.py` through global `python.exe`
3. `data/worker/stream-saver-ocr.exe`

Managed runtime example:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --install build_x64 --config RelWithDebInfo --prefix package'
.\scripts\package-managed-worker.ps1
```

This downloads the Python embeddable package, enables `site`, installs the
worker requirements into the private runtime, and places it under the OBS
plugin package.

PyInstaller packaging is currently a fallback path only. It converts
`stream_saver_ocr.py` into:

- Windows: `stream-saver-ocr.exe`
- Linux: `stream-saver-ocr`

PyInstaller example:

```powershell
python -m pip install -r worker\requirements-build.txt
.\scripts\package-worker.ps1
```

On Windows this creates `worker\stream-saver-ocr.exe`, which the OBS plugin uses only after the managed Python runtime and Python script options are unavailable.
