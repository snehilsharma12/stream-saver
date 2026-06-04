# Stream Saver Skill

Use this skill when working on the Stream Saver OBS OCR redaction plugin in this repository.

## Workflow

1. Read `AGENT.md` first. It contains the current project handoff, build commands, debugging commands, and known issues.
2. Check `git status --short` before editing. The worktree may already contain user/session changes; do not revert unrelated edits.
3. For OCR/redaction bugs, inspect logs before changing code:
   - OBS logs in `%APPDATA%\obs-studio\logs`
   - Worker log in `C:\ProgramData\obs-studio\plugins\stream-saver\data\worker\stream-saver-worker.log`
   - Debug images in the same worker directory
4. Categorize the failure:
   - no submit
   - slow/no OCR response
   - OCR missed text
   - matcher produced wrong regions
   - shader rendered weak/wrong redaction
5. Keep fixes scoped to the relevant layer.

## Build And Install

Use Visual Studio CMake directly if `cmake` is not on PATH:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build_x64 --config RelWithDebInfo
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --install build_x64 --config RelWithDebInfo --prefix package
C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe -Command "Copy-Item -Recurse -Force (Resolve-Path 'package\stream-saver') 'C:\ProgramData\obs-studio\plugins'"
```

OBS must be restarted after install. If copy fails or logs are locked, ask the user to close OBS.

## Tests

Run:

```powershell
python -m unittest tests.python.test_worker_protocol
.\build_x64\RelWithDebInfo\stream-saver-matcher-tests.exe
```

## Key Files

- `src/stream_saver_filter.cpp`: lifecycle, warmup, frame capture, OCR submission, effect parameters
- `src/matcher.cpp`: text normalization and region matching
- `src/ocr_client.cpp`: TCP protocol client
- `worker/stream_saver_ocr.py`: PaddleOCR worker
- `data/effects/redact_blur.effect`: redaction shader
- `docs/protocol.md`: JSON protocol

