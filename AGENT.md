# Stream Saver Agent Notes

This file is the handoff for future coding sessions. Read it before changing the project.

## Project

Stream Saver is an OBS Studio effect filter that redacts detected text regions from a source or scene before streaming or recording.

High-level flow:

1. OBS render thread captures a scaled PNG frame from the filtered source.
2. The C++ plugin sends the frame to a local TCP detector worker.
3. `worker/stream_saver_ocr.py` runs YOLO text detection and returns detected text boxes.
4. `src/matcher.cpp` converts boxes into normalized redaction regions.
5. `data/effects/redact_blur.effect` renders the redaction regions on the source.

Important paths:

- `src/stream_saver_filter.cpp`: OBS filter lifecycle, capture, worker startup/warmup, OCR submission, effect parameters.
- `src/ocr_client.cpp`: newline-delimited JSON TCP client for the worker.
- `src/matcher.cpp`: phrase normalization and redaction-region matching.
- `worker/stream_saver_ocr.py`: YOLO detector worker.
- `data/effects/redact_blur.effect`: GPU redaction shader.
- `tests/cpp/test_matcher.cpp`: matcher unit tests.
- `tests/python/test_worker_protocol.py`: worker protocol tests.
- `docs/protocol.md`: OCR protocol.

## Current State And Known Issues

Recent work focused on live OBS testing:

- Worker startup/warmup was moved earlier so the detector worker starts when the filter exists, not only after recording begins.
- Real source frames must only be submitted while recording or streaming.
- OCR results that return after recording stops should be ignored; stale regions should not carry into the next recording.
- Redaction currently uses a solid dark mask in `redact_blur.effect`, because blur left readable ghost text.
- The worker now expects a YOLO text-detection model exported to ONNX or OpenVINO IR. The current recommended off-the-shelf model is `RoyRud1902/yolo11n-text`, exported as `yolo11n-text.onnx`.
- The filter has an `Inference backend` setting: ONNX Runtime CPU, ONNX Runtime DirectML, ONNX Runtime CUDA, OpenVINO, or Custom backend. Changing backend/model/image-size/path/port restarts the worker.
- YOLO mode redacts all detected text regions. It does not recognize phrases; phrase-specific redaction would require a separate recognizer after detection.
- Full-frame debug dumps are separate from OCR-size dumps:
  - `stream-saver-last-frame.png`: OCR input frame.
  - `stream-saver-last-source-frame.png`: full source frame, when debug overlay is enabled and a downscaled OCR input is used.

As of June 4, 2026, the YOLO text-detection path is working in OBS testing. Startup/warmup behavior is good, stale first-recording results have been addressed, and small text that previously slipped through is now detected/redacted with the current detector setup.

There is no currently active blocker called out by the user. If text is missed again, treat it as detector/capture/model tuning first, not phrase matching: YOLO mode redacts all detected text and does not depend on the old OCR phrase matcher.

## Build

Windows build uses the OBS plugin-template CMake bootstrap. If `cmake` is not on PATH, this machine has Visual Studio CMake here:

```powershell
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

Build:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build_x64 --config RelWithDebInfo
```

Stage package:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --install build_x64 --config RelWithDebInfo --prefix package
```

Install into OBS:

```powershell
C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe -Command "Copy-Item -Recurse -Force (Resolve-Path 'package\stream-saver') 'C:\ProgramData\obs-studio\plugins'"
```

Restart OBS after installing. If OBS is open, Windows may keep the DLL or worker log locked; close OBS fully before copying.

## Tests

Python worker protocol:

```powershell
python -m unittest tests.python.test_worker_protocol
```

C++ matcher executable:

```powershell
.\build_x64\RelWithDebInfo\stream-saver-matcher-tests.exe
```

Build also compiles the matcher test.

## Debugging Workflow

OBS logs:

```powershell
Get-ChildItem "$env:APPDATA\obs-studio\logs" | Sort-Object LastWriteTime -Descending | Select-Object -First 5
Select-String -Path "$env:APPDATA\obs-studio\logs\<latest>.txt" -Pattern "stream-saver|OCR frame|warmup|submitted|regions|error"
```

Worker logs and debug frames:

```powershell
Get-Content "C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\stream-saver-worker.log"
Get-ChildItem "C:\ProgramData\obs-studio\plugins\stream-saver\data\worker" | Sort-Object LastWriteTime -Descending | Select-Object -First 10
```

Use `view_image` on:

```text
C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\stream-saver-last-frame.png
C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\stream-saver-last-source-frame.png
```

Read logs by failure class:

- No redaction and no `OCR frame ... submitted`: submit gating/lifecycle problem.
- `submitted` but no response until much later: detector too slow or worker busy.
- Worker `detector response ... detections=0` lacks the target: model/capture quality problem.
- Worker detects target but plugin regions are wrong: coordinate normalization problem.
- Regions are correct but visual is readable: shader/effect problem.

## Worker Protocol

The protocol still uses the legacy request type `"ocr"` even though the worker now performs YOLO text detection. The worker accepts newline-delimited JSON over TCP:

```json
{
  "type": "ocr",
  "frame_id": 42,
  "width": 960,
  "height": 540,
  "source_width": 1920,
  "source_height": 1080,
  "warmup": false,
  "format": "png-base64",
  "image": "...",
  "source_image": "..."
}
```

`width`/`height` are detector-frame coordinates. Returned boxes are in detector-frame coordinates. `source_image` is optional and only for debug dumps.

Warmup requests use `warmup: true` and a synthetic image. They should not write debug frame dumps or consume real source pixels.

## Worker Notes

Use Python 3.9-3.13. Worker dependencies are `Pillow`, `numpy`, `onnxruntime`, and optionally `openvino` or `onnxruntime-directml`.

The plugin worker preference on Windows is:

1. `data/worker/python/python.exe` running `data/worker/stream_saver_ocr.py`
2. `data/worker/stream_saver_ocr.py` through global `python.exe`
3. `data/worker/stream-saver-ocr.exe`

Managed runtime packaging:

```powershell
.\scripts\package-managed-worker.ps1
```

## Packaging Gotchas

Generated debug outputs must not be committed or packaged:

- `worker/stream-saver-last-frame.png`
- `worker/stream-saver-last-source-frame.png`
- `stream-saver-worker.log`

`CMakeLists.txt` excludes `stream-saver-last-*.png` and `stream-saver-worker.log` from the worker install.

## Current Suggested Next Steps

If detection quality regresses:

1. Inspect the latest worker `detector response ... detections=` line and saved frame dump.
2. If small text is absent, verify the YOLO model is trained for text detection and raise YOLO image size.
3. If detections exist but are not redacted, inspect normalized region coordinates and shader limits.
4. For phrase-specific redaction, add a lightweight recognizer on cropped YOLO boxes.
