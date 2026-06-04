#!/usr/bin/env python3
"""Local PaddleOCR worker for Stream Saver.

The worker accepts one newline-delimited JSON request per TCP connection.
It is deliberately small so it can be packaged as a standalone executable.
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import os
import socketserver
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class Detection:
    text: str
    confidence: float
    box: list[float]


class OcrEngine:
    def __init__(self, lang: str = "en", use_textline_orientation: bool = False) -> None:
        # PaddleOCR 3.x enables MKL-DNN/oneDNN by default on CPU. On some
        # Windows CPU/runtime combinations it fails during OCR inference, so
        # keep the worker on the more conservative Paddle CPU backend.
        os.environ.setdefault("PADDLE_PDX_ENABLE_MKLDNN_BYDEFAULT", "False")
        self._patch_paddlex_frozen_extra_check()

        try:
            from paddleocr import PaddleOCR
        except Exception as exc:  # pragma: no cover - environment dependent
            raise RuntimeError(
                "PaddleOCR is not installed. Run: python -m pip install -r worker/requirements.txt"
            ) from exc

        self._use_textline_orientation = use_textline_orientation
        self._engine = PaddleOCR(
            use_doc_orientation_classify=False,
            use_doc_unwarping=False,
            use_textline_orientation=use_textline_orientation,
            text_detection_model_name="PP-OCRv4_mobile_det",
            text_recognition_model_name="en_PP-OCRv4_mobile_rec" if lang == "en" else None,
            enable_mkldnn=False,
        )

    @staticmethod
    def _patch_paddlex_frozen_extra_check() -> None:
        if not getattr(sys, "frozen", False):
            return

        try:
            from paddlex.utils import deps as paddlex_deps
        except Exception:
            return

        original_is_dep_available = paddlex_deps.is_dep_available
        original_is_extra_available = paddlex_deps.is_extra_available

        def is_dep_available(dep: str, /, check_version: bool = False) -> bool:
            if check_version:
                return original_is_dep_available(dep, check_version=True)

            frozen_dep_modules = {
                "opencv-contrib-python": "cv2",
                "opencv-python": "cv2",
                "pypdfium2": "pypdfium2",
                "pyclipper": "pyclipper",
                "python-bidi": "bidi",
                "imagesize": "imagesize",
                "shapely": "shapely",
            }
            module = frozen_dep_modules.get(dep)
            if module:
                try:
                    __import__(module)
                    return True
                except Exception:
                    return False

            return original_is_dep_available(dep)

        def is_extra_available(extra: str) -> bool:
            if extra in {"ocr", "ocr-core"}:
                required_modules = ("cv2", "imagesize", "pyclipper", "pypdfium2", "bidi", "shapely")
                try:
                    return all(__import__(module) for module in required_modules)
                except Exception:
                    return False
            return original_is_extra_available(extra)

        paddlex_deps.is_dep_available = is_dep_available
        paddlex_deps.is_extra_available = is_extra_available

        try:
            import cv2
            import pypdfium2
        except Exception:
            return

        for module in list(sys.modules.values()):
            module_name = getattr(module, "__name__", "")
            if module_name.startswith("paddlex."):
                module.__dict__.setdefault("cv2", cv2)
                module.__dict__.setdefault("pypdfium2", pypdfium2)

    def recognize_png_base64(self, image_b64: str) -> list[Detection]:
        if not image_b64:
            return []

        try:
            import numpy as np
            from PIL import Image
        except Exception as exc:  # pragma: no cover - environment dependent
            raise RuntimeError("Pillow and numpy are required by the OCR worker") from exc

        decode_started = time.monotonic()
        raw = base64.b64decode(image_b64)
        image = Image.open(io.BytesIO(raw)).convert("RGB")
        array = np.asarray(image)
        decode_ms = int((time.monotonic() - decode_started) * 1000)

        predict_started = time.monotonic()
        result = self._engine.predict(
            array,
            use_doc_orientation_classify=False,
            use_doc_unwarping=False,
            use_textline_orientation=self._use_textline_orientation,
        )
        predict_ms = int((time.monotonic() - predict_started) * 1000)
        print(
            f"ocr engine timing decode_ms={decode_ms} predict_ms={predict_ms} image={image.width}x{image.height}",
            flush=True,
        )
        return self._parse_result(result)

    def _parse_result(self, result: Any) -> list[Detection]:
        detections: list[Detection] = []

        if not result:
            return detections

        first_result = result[0] if isinstance(result, list) and result else result
        if isinstance(first_result, dict) and "rec_texts" in first_result:
            texts = first_result.get("rec_texts", [])
            scores = first_result.get("rec_scores", [])
            polys = first_result.get("rec_polys") or first_result.get("dt_polys", [])

            for text, score, poly in zip(texts, scores, polys):
                flattened = self._flatten_box(poly)
                if len(flattened) == 8:
                    detections.append(
                        Detection(
                            text=str(text),
                            confidence=float(score),
                            box=flattened,
                        )
                    )

            return detections

        # PaddleOCR 2.x commonly returned [ [ [box], (text, confidence) ], ... ].
        lines = result[0] if len(result) == 1 and isinstance(result[0], list) else result
        for item in lines:
            if not item or len(item) < 2:
                continue

            box_points = item[0]
            text_info = item[1]
            if not text_info or len(text_info) < 2:
                continue

            flattened = self._flatten_box(box_points)

            if len(flattened) != 8:
                continue

            detections.append(
                Detection(
                    text=str(text_info[0]),
                    confidence=float(text_info[1]),
                    box=flattened,
                )
            )

        return detections

    @staticmethod
    def _flatten_box(box_points: Any) -> list[float]:
        flattened: list[float] = []
        for point in box_points:
            flattened.extend([float(point[0]), float(point[1])])
        return flattened


class StreamSaverHandler(socketserver.StreamRequestHandler):
    engine: OcrEngine

    def handle(self) -> None:
        line = self.rfile.readline(128 * 1024 * 1024)
        if not line:
            return

        try:
            request = json.loads(line.decode("utf-8"))
            response = self._dispatch(request)
        except Exception as exc:
            response = {"type": "error", "message": str(exc)}

        encoded = json.dumps(response, separators=(",", ":")).encode("utf-8") + b"\n"
        self.wfile.write(encoded)

    def _dispatch(self, request: dict[str, Any]) -> dict[str, Any]:
        request_type = request.get("type")
        if request_type == "health":
            return {"type": "health", "ok": True, "engine": "paddleocr"}

        if request_type != "ocr":
            return {"type": "error", "message": f"unsupported request type: {request_type}"}

        frame_id = int(request.get("frame_id", 0))
        image = str(request.get("image", ""))
        warmup = bool(request.get("warmup", False))
        started = time.monotonic()
        worker_dir = Path(__file__).resolve().parent
        dump_path = worker_dir / "stream-saver-last-frame.png"
        source_dump_path = worker_dir / "stream-saver-last-source-frame.png"
        source_image = str(request.get("source_image") or image)
        if not warmup:
            try:
                dump_path.write_bytes(base64.b64decode(image))
            except Exception as exc:
                print(f"failed to write OCR frame dump: {exc}", flush=True)
            try:
                source_dump_path.write_bytes(base64.b64decode(source_image))
            except Exception as exc:
                print(f"failed to write source frame dump: {exc}", flush=True)
        print(
            f"ocr request frame={frame_id} width={request.get('width')} height={request.get('height')} "
            f"source_width={request.get('source_width')} source_height={request.get('source_height')} "
            f"warmup={warmup} image_base64_bytes={len(image)} source_image_base64_bytes={len(source_image)}",
            flush=True,
        )
        detections = self.engine.recognize_png_base64(image)
        elapsed_ms = int((time.monotonic() - started) * 1000)
        preview = " | ".join(detection.text for detection in detections)
        print(
            f"ocr response frame={frame_id} detections={len(detections)} elapsed_ms={elapsed_ms} texts={preview}",
            flush=True,
        )

        return {
            "type": "ocr_result",
            "frame_id": frame_id,
            "detections": [
                {
                    "text": detection.text,
                    "confidence": detection.confidence,
                    "box": detection.box,
                }
                for detection in detections
            ],
        }


class ThreadingServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Stream Saver PaddleOCR worker")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=48741)
    parser.add_argument("--lang", default="en")
    parser.add_argument(
        "--textline-orientation",
        dest="use_textline_orientation",
        action="store_true",
        help="Enable textline orientation classification.",
    )
    parser.add_argument(
        "--no-textline-orientation",
        dest="use_textline_orientation",
        action="store_false",
    )
    parser.set_defaults(use_textline_orientation=False)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)

    StreamSaverHandler.engine = OcrEngine(
        lang=args.lang,
        use_textline_orientation=args.use_textline_orientation,
    )
    with ThreadingServer((args.host, args.port), StreamSaverHandler) as server:
        print(f"stream-saver OCR worker listening on {args.host}:{args.port}", flush=True)
        server.serve_forever()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
