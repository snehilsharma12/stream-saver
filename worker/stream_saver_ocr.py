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
from dataclasses import dataclass
from typing import Any


@dataclass
class Detection:
    text: str
    confidence: float
    box: list[float]


class OcrEngine:
    def __init__(self, lang: str = "en", use_textline_orientation: bool = True) -> None:
        # PaddleOCR 3.x enables MKL-DNN/oneDNN by default on CPU. On some
        # Windows CPU/runtime combinations it fails during OCR inference, so
        # keep the worker on the more conservative Paddle CPU backend.
        os.environ.setdefault("PADDLE_PDX_ENABLE_MKLDNN_BYDEFAULT", "False")

        try:
            from paddleocr import PaddleOCR
        except Exception as exc:  # pragma: no cover - environment dependent
            raise RuntimeError(
                "PaddleOCR is not installed. Run: python -m pip install -r worker/requirements.txt"
            ) from exc

        self._use_textline_orientation = use_textline_orientation
        self._engine = PaddleOCR(
            use_textline_orientation=use_textline_orientation,
            lang=lang,
            enable_mkldnn=False,
        )

    def recognize_png_base64(self, image_b64: str) -> list[Detection]:
        if not image_b64:
            return []

        try:
            import numpy as np
            from PIL import Image
        except Exception as exc:  # pragma: no cover - environment dependent
            raise RuntimeError("Pillow and numpy are required by the OCR worker") from exc

        raw = base64.b64decode(image_b64)
        image = Image.open(io.BytesIO(raw)).convert("RGB")
        array = np.asarray(image)

        result = self._engine.predict(
            array,
            use_textline_orientation=self._use_textline_orientation,
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
        detections = self.engine.recognize_png_base64(image)

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
    parser.add_argument("--no-textline-orientation", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)

    StreamSaverHandler.engine = OcrEngine(
        lang=args.lang,
        use_textline_orientation=not args.no_textline_orientation,
    )
    with ThreadingServer((args.host, args.port), StreamSaverHandler) as server:
        print(f"stream-saver OCR worker listening on {args.host}:{args.port}", flush=True)
        server.serve_forever()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
