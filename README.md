# Stream Saver

Stream Saver is an OBS Studio effect filter that redacts text from a source or scene before it is streamed or recorded. It samples incoming video frames, sends them to a local YOLO text detector worker, and masks detected text regions.

The plugin is designed for Windows x64 and Linux x86_64. OCR and phrase matching happen locally.

## Status

This repository contains the initial implementation scaffold:

- Native OBS C++ effect filter named `stream-saver`
- Local TCP detector worker protocol
- YOLO Python worker with ONNX Runtime and OpenVINO backend options
- GPU effect asset for redaction
- Unit tests for phrase matching
- Manual packaging and import instructions

## Build

The repository uses the official OBS plugin-template CMake bootstrap. On Windows, it downloads OBS deps and OBS source into `.deps`, builds the minimal `libobs` development artifacts, then builds this plugin.

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --preset windows-x64'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --build --preset windows-x64'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --install build_x64 --config RelWithDebInfo --prefix package'
```

Linux example:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix package
```

## Worker Setup

Release packages should include a private Python runtime, detector dependencies,
and `yolo11n-text.onnx`, so end users do not need global Python or a manual model
download.

For development, the worker requires Python plus the detector runtime:

```bash
python -m pip install -r worker/requirements.txt
```

Use Python 3.9-3.13 for the worker. The recommended off-the-shelf model is
`RoyRud1902/yolo11n-text`, exported from `best.pt` to `yolo11n-text.onnx`.
It is a nano-class, single-class text detector trained for documents,
screenshots, UI interfaces, and scene text. You can also provide another
YOLOv8/YOLOv10 text-detection model exported to ONNX for ONNX Runtime or to
OpenVINO IR for OpenVINO.

The filter includes an `Inference backend` setting:

- `ONNX Runtime CPU` is the portable default.
- `ONNX Runtime DirectML` is the Windows GPU path that can work well for AMD.
  Use `DirectML device ID` to choose the adapter on systems with both an iGPU
  and dGPU; try `0` and `1`, then compare the worker log timings.
- `ONNX Runtime CUDA` targets Nvidia CUDA runtimes.
- `OpenVINO` targets Intel CPU/iGPU acceleration.
- `Custom backend` passes an advanced backend string to the worker.

YOLO mode redacts every detected text region. It does not perform phrase
recognition by itself.

For release packages, prefer a managed Python environment in `data/worker/python`
so users do not need global Python or global detector dependencies:

```powershell
.\scripts\package-managed-worker.ps1
```

The PyInstaller executable path remains available as a fallback, but the plugin
prefers the managed Python runtime first on Windows.

## Release Package

Build a ready-to-install Windows zip with:

```powershell
.\scripts\package-release-windows.ps1 -ModelPath C:\path\to\yolo11n-text.onnx
```

The script stages the OBS plugin folder, adds the managed Python runtime,
copies the model, runs a bundled-worker smoke test, and writes:

```text
dist\stream-saver-windows-x64.zip
```

Users should extract the zip so the `stream-saver` folder lands under:

```text
C:\ProgramData\obs-studio\plugins
```

Build a ready-to-install Linux tarball on Linux with:

```bash
bash scripts/package-release-linux.sh --model-path /path/to/yolo11n-text.onnx
```

From Windows with WSL installed, run:

```powershell
.\scripts\package-release-linux-wsl.ps1
```

The script stages the OBS local plugin folder layout, creates a private Python
venv with Linux worker dependencies, copies the model, runs a bundled-worker
smoke test, and writes:

```text
dist/stream-saver-linux-x86_64.tar.gz
```

Users should extract the tarball and run:

```bash
./install.sh
```

The installer detects native OBS and Flatpak OBS config paths. It can also be
forced with `./install.sh --native`, `./install.sh --flatpak`, or
`./install.sh --dir /custom/obs/plugins`.

## Install Into OBS

Windows:

```text
C:\ProgramData\obs-studio\plugins\stream-saver\bin\64bit\stream-saver.dll
C:\ProgramData\obs-studio\plugins\stream-saver\data\locale\en-US.ini
C:\ProgramData\obs-studio\plugins\stream-saver\data\effects\redact_blur.effect
C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\stream_saver_ocr.py
C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\python\python.exe
```

Linux:

```text
~/.config/obs-studio/plugins/stream-saver/bin/64bit/stream-saver.so
~/.config/obs-studio/plugins/stream-saver/data/locale/en-US.ini
~/.config/obs-studio/plugins/stream-saver/data/effects/redact_blur.effect
~/.config/obs-studio/plugins/stream-saver/data/worker/stream-saver-ocr
```

After copying the folder, restart OBS, open a source or scene's filters, and add `Stream Saver` under effect filters.

## Development Notes

The render path is intentionally non-blocking. Detection requests are skipped if the worker is still busy, so `Every frame` means the filter submits the newest OBS frame whenever inference is ready rather than waiting for the interval gate. In interval mode, frames are submitted only when `frame_index % interval == 0`. Changing the worker path, port, backend, model path, or image size restarts the worker so the new runtime setting is applied.

The plugin attempts to launch the bundled detector worker automatically. Use the worker path override if you are running the Python worker directly during development.
