# Installing Stream Saver

## Windows

For end users, download `stream-saver-windows-x64.zip` and extract it so the
`stream-saver` folder is here:

```text
C:\ProgramData\obs-studio\plugins\stream-saver
```

Release maintainers can build that zip with:

```powershell
.\scripts\package-release-windows.ps1 -ModelPath C:\path\to\yolo11n-text.onnx
```

For development, build and install to a package directory:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --preset windows-x64'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --build --preset windows-x64'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --install build_x64 --config RelWithDebInfo --prefix package'
```

Copy `package\stream-saver` to:

```text
C:\ProgramData\obs-studio\plugins\stream-saver
```

Expected files:

```text
C:\ProgramData\obs-studio\plugins\stream-saver\bin\64bit\stream-saver.dll
C:\ProgramData\obs-studio\plugins\stream-saver\data\locale\en-US.ini
C:\ProgramData\obs-studio\plugins\stream-saver\data\effects\redact_blur.effect
C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\stream_saver_ocr.py
C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\python\python.exe
C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\yolo11n-text.onnx
```

## Linux

For end users, download `stream-saver-linux-x86_64.tar.gz`, extract it, and run:

```bash
./install.sh
```

The installer detects native OBS and Flatpak OBS config paths. You can force a
target with `./install.sh --native`, `./install.sh --flatpak`, or
`./install.sh --dir /custom/obs/plugins`.

Release maintainers can build that tarball on Linux with:

```bash
bash scripts/package-release-linux.sh --model-path /path/to/yolo11n-text.onnx
```

From Windows with WSL installed:

```powershell
.\scripts\package-release-linux-wsl.ps1
```

For development, build and install to a package directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix package
```

Copy `package/stream-saver` to:

```text
~/.config/obs-studio/plugins/stream-saver
```

Expected files:

```text
~/.config/obs-studio/plugins/stream-saver/bin/64bit/stream-saver.so
~/.config/obs-studio/plugins/stream-saver/data/locale/en-US.ini
~/.config/obs-studio/plugins/stream-saver/data/effects/redact_blur.effect
~/.config/obs-studio/plugins/stream-saver/data/worker/stream-saver-ocr
~/.config/obs-studio/plugins/stream-saver/data/worker/python/bin/python
~/.config/obs-studio/plugins/stream-saver/data/worker/yolo11n-text.onnx
```

## OBS Usage

1. Close OBS before copying plugin files.
2. Restart OBS.
3. Select a source or scene.
4. Open `Filters`.
5. Add `Stream Saver` under `Effect Filters`.
6. Enter sensitive phrases, one per line.
7. Choose `Every frame` or `Every X frames`.

The plugin starts the bundled OCR worker automatically. If OBS cannot start it, set `Worker path override` to the development worker executable or script.
