# Stream Saver Agent Notes

This file is the handoff for future coding sessions. Read it before changing the project.

## Project

Stream Saver is an OBS Studio effect filter that redacts configured words and phrases from a source or scene before streaming or recording.

High-level flow:

1. OBS render thread captures a scaled PNG frame from the filtered source.
2. The C++ plugin sends the frame to a local TCP OCR worker.
3. `worker/stream_saver_ocr.py` runs PaddleOCR and returns detected text boxes.
4. `src/matcher.cpp` matches detections against user phrases and converts boxes into normalized redaction regions.
5. `data/effects/redact_blur.effect` renders the redaction regions on the source.

Important paths:

- `src/stream_saver_filter.cpp`: OBS filter lifecycle, capture, worker startup/warmup, OCR submission, effect parameters.
- `src/ocr_client.cpp`: newline-delimited JSON TCP client for the worker.
- `src/matcher.cpp`: phrase normalization and redaction-region matching.
- `worker/stream_saver_ocr.py`: PaddleOCR worker.
- `data/effects/redact_blur.effect`: GPU redaction shader.
- `tests/cpp/test_matcher.cpp`: matcher unit tests.
- `tests/python/test_worker_protocol.py`: worker protocol tests.
- `docs/protocol.md`: OCR protocol.

## Current State And Known Issues

Recent work focused on live OBS testing:

- Worker startup/warmup was moved earlier so the OCR worker starts when the filter exists, not only after recording begins.
- Real source frames must only be submitted while recording or streaming.
- OCR results that return after recording stops should be ignored; stale regions should not carry into the next recording.
- Redaction currently uses a solid dark mask in `redact_blur.effect`, because blur left readable ghost text.
- PaddleOCR is configured for faster screen OCR with `PP-OCRv4_mobile_det` and `en_PP-OCRv4_mobile_rec`.
- Full-frame debug dumps are separate from OCR-size dumps:
  - `stream-saver-last-frame.png`: OCR input frame.
  - `stream-saver-last-source-frame.png`: full source frame, when debug overlay is enabled and a downscaled OCR input is used.

As of the latest session, startup is much better, but small text is not consistently detected/redacted. The latest user screenshot showed larger regions redacted but small visible words like `Garden`, `Steam`, and `123 Dump Drive` remaining unredacted. The likely next work is tuning OCR capture size, detection thresholds, and/or adding targeted crops around likely text instead of only one scaled full-frame OCR pass.

Also watch for large empty redactions. This came from line-combining in `src/matcher.cpp` sweeping across too many OCR boxes before finding a multi-word phrase. The matcher now limits combination by word count and gap, but keep testing with OBS recursion and small text.

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
- `submitted` but no response until much later: OCR too slow or worker busy.
- Worker `ocr response ... texts=` lacks the target: OCR model/capture quality problem.
- Worker detects target but plugin regions are wrong: matcher or coordinate normalization problem.
- Regions are correct but visual is readable: shader/effect problem.

## OCR Protocol

The worker accepts newline-delimited JSON over TCP:

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

`width`/`height` are OCR-frame coordinates. Returned boxes are in OCR-frame coordinates. `source_image` is optional and only for debug dumps.

Warmup requests use `warmup: true` and a synthetic image. They should not write debug frame dumps or consume real source pixels.

## Worker Notes

Use Python 3.9-3.13. Python 3.14 is not supported by the pinned PaddlePaddle wheel stack.

The worker disables MKL-DNN/oneDNN because PaddlePaddle 3.x has failed on some Windows CPU/runtime combinations.

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

For the small-text issue:

1. Inspect the latest worker `texts=` line and saved OCR dump.
2. If small words are absent from `texts=`, raise OCR capture size or tune Paddle detection thresholds.
3. If small words are present but not redacted, inspect `src/matcher.cpp` and phrase normalization.
4. If redaction regions exist but are visually missing, inspect normalized region coordinates and shader limits.
5. Consider adding a crop/tiling OCR mode for small text: OCR the full frame at a modest scale, plus targeted lower-band or text-heavy crops at higher resolution.

