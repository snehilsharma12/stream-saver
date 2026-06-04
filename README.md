# Stream Saver

Stream Saver is an OBS Studio effect filter that redacts sensitive text from a source or scene before it is streamed or recorded. It samples incoming video frames, sends them to a local PaddleOCR worker, matches detected text against user-configured words or phrases, and blurs matching regions.

The plugin is designed for Windows x64 and Linux x86_64. OCR and phrase matching happen locally.

## Status

This repository contains the initial implementation scaffold:

- Native OBS C++ effect filter named `stream-saver`
- Local TCP OCR worker protocol
- PaddleOCR Python worker
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

The worker requires Python and PaddleOCR:

```bash
python -m pip install -r worker/requirements.txt
```

Use Python 3.9-3.13 for the worker. The pinned latest Paddle stack uses `paddleocr==3.6.0` and `paddlepaddle==3.3.1`; PaddlePaddle does not currently publish Python 3.14 wheels.

For release packages, prefer a managed Python environment in `data/worker/python`
so users do not need global Python or global PaddleOCR:

```powershell
.\scripts\package-managed-worker.ps1
```

The PyInstaller executable path remains available as a fallback, but the plugin
prefers the managed Python runtime first on Windows.

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

The render path is intentionally non-blocking. OCR requests are skipped if the worker is still busy. In interval mode, frames are submitted only when `frame_index % interval == 0`.

The plugin attempts to launch the bundled OCR worker automatically. Use the worker path override if you are running the Python worker directly during development.
