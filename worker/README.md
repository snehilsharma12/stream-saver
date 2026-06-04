# Stream Saver YOLO Worker

The worker runs YOLO text detection out of process and listens on `127.0.0.1:48741` by default.

Development:

```bash
python -m pip install -r requirements.txt
python stream_saver_ocr.py --port 48741 --model yolo11n-text.onnx
```

Use Python 3.9-3.13 for the worker environment.

Recommended model: `RoyRud1902/yolo11n-text` from Hugging Face. Download
`best.pt`, then export it with Ultralytics:

```bash
python -m pip install ultralytics onnx onnxsim
python -c "from ultralytics import YOLO; YOLO('best.pt').export(format='onnx', imgsz=640, simplify=True)"
```

Rename the exported file to `yolo11n-text.onnx` or set the model path in OBS.
Plain COCO `yolov10n` weights are not enough for text redaction unless they were
trained to detect text.

Backend selection:

```bash
python stream_saver_ocr.py --port 48741 --backend onnxruntime --model yolo11n-text.onnx
python stream_saver_ocr.py --port 48741 --backend directml --model yolo11n-text.onnx
python stream_saver_ocr.py --port 48741 --backend cuda --model yolo11n-text.onnx
python stream_saver_ocr.py --port 48741 --backend openvino --device CPU --model yolo11n-text.xml
```

Install `onnxruntime-directml` instead of regular `onnxruntime` for the DirectML
provider. Use an OpenVINO-exported model for the `openvino` backend.

Health check:

```bash
printf '{"type":"health"}\n' | nc 127.0.0.1 48741
```

Release packaging should bundle a managed Python runtime in `data/worker/python`
so users do not need global Python or global detector dependencies. The plugin looks for
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
