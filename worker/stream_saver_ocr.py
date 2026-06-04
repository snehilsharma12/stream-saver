#!/usr/bin/env python3
"""Local YOLO text detector worker for Stream Saver.

The worker accepts one newline-delimited JSON request per TCP connection. It
returns detection boxes in the same protocol shape the OBS plugin already
understands, but detections are text-region boxes rather than recognized text.
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import socketserver
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class Detection:
    text: str
    confidence: float
    box: list[float]


@dataclass
class PreprocessedImage:
    array: Any
    original_width: int
    original_height: int
    ratio: float
    pad_x: float
    pad_y: float


class YoloTextDetector:
    def __init__(
        self,
        model_path: str = "yolo11n-text.onnx",
        backend: str = "onnxruntime",
        device: str = "cpu",
        image_size: int = 640,
        confidence_threshold: float = 0.25,
        recognize_text: bool = True,
    ) -> None:
        self.model_path = str(self._resolve_model_path(model_path or "yolo11n-text.onnx"))
        self.backend = backend or "onnxruntime"
        self.device = device or "cpu"
        self.image_size = max(64, int(image_size))
        self.confidence_threshold = confidence_threshold
        self.recognize_text = recognize_text
        self._load_model()
        self._recognizer = None
        if recognize_text:
            self._load_recognizer()

    @staticmethod
    def _resolve_model_path(model_path: str) -> Path:
        path = Path(model_path)
        if path.is_absolute():
            return path
        return Path(__file__).resolve().parent / path

    def _load_model(self) -> None:
        if self.backend == "openvino":
            try:
                import openvino as ov
            except Exception as exc:  # pragma: no cover - environment dependent
                raise RuntimeError("OpenVINO is not installed. Install openvino for this backend.") from exc

            core = ov.Core()
            model = core.read_model(self.model_path)
            self._compiled_model = core.compile_model(model, self.device.upper())
            self._input_layer = self._compiled_model.input(0)
            self._output_layer = self._compiled_model.output(0)
            return

        try:
            import onnxruntime as ort
        except Exception as exc:  # pragma: no cover - environment dependent
            raise RuntimeError("ONNX Runtime is not installed. Install onnxruntime or onnxruntime-directml.") from exc

        providers = self._onnx_providers(ort)
        self._session = ort.InferenceSession(self.model_path, providers=providers)
        self._input_name = self._session.get_inputs()[0].name
        self._output_names = [output.name for output in self._session.get_outputs()]

    def _load_recognizer(self) -> None:
        try:
            from rapidocr_onnxruntime import RapidOCR
        except Exception as exc:  # pragma: no cover - environment dependent
            raise RuntimeError("RapidOCR is not installed. Install rapidocr-onnxruntime.") from exc

        self._recognizer = RapidOCR(use_text_det=False, use_angle_cls=False)

    def _onnx_providers(self, ort: Any) -> list[Any]:
        available = set(ort.get_available_providers())
        backend = self.backend.lower()

        if backend == "directml" and "DmlExecutionProvider" in available:
            return ["DmlExecutionProvider", "CPUExecutionProvider"]
        if backend == "cuda" and "CUDAExecutionProvider" in available:
            return ["CUDAExecutionProvider", "CPUExecutionProvider"]
        if backend == "openvino-onnx" and "OpenVINOExecutionProvider" in available:
            return ["OpenVINOExecutionProvider", "CPUExecutionProvider"]

        return ["CPUExecutionProvider"]

    def recognize_png_base64(self, image_b64: str) -> list[Detection]:
        if not image_b64:
            return []

        try:
            import numpy as np
            from PIL import Image
        except Exception as exc:  # pragma: no cover - environment dependent
            raise RuntimeError("Pillow and numpy are required by the YOLO worker") from exc

        decode_started = time.monotonic()
        raw = base64.b64decode(image_b64)
        image = Image.open(io.BytesIO(raw)).convert("RGB")
        prepared = self._preprocess(image, np)
        decode_ms = int((time.monotonic() - decode_started) * 1000)

        predict_started = time.monotonic()
        if self.backend == "openvino":
            outputs = self._compiled_model([prepared.array])[self._output_layer]
        else:
            outputs = self._session.run(self._output_names, {self._input_name: prepared.array})[0]
        predict_ms = int((time.monotonic() - predict_started) * 1000)

        detections = self._parse_detections(outputs, prepared, np)
        recognize_started = time.monotonic()
        if self.recognize_text:
            detections = self._recognize_detection_text(image, detections, np)
        recognize_ms = int((time.monotonic() - recognize_started) * 1000)
        print(
            f"yolo timing decode_ms={decode_ms} predict_ms={predict_ms} "
            f"recognize_ms={recognize_ms} image={image.width}x{image.height} backend={self.backend}",
            flush=True,
        )
        return detections

    def _preprocess(self, image: Any, np: Any) -> PreprocessedImage:
        width, height = image.size
        ratio = min(self.image_size / width, self.image_size / height)
        resized_width = max(1, int(round(width * ratio)))
        resized_height = max(1, int(round(height * ratio)))
        pad_x = (self.image_size - resized_width) / 2.0
        pad_y = (self.image_size - resized_height) / 2.0

        resized = image.resize((resized_width, resized_height))
        canvas = np.full((self.image_size, self.image_size, 3), 114, dtype=np.uint8)
        left = int(round(pad_x - 0.1))
        top = int(round(pad_y - 0.1))
        canvas[top : top + resized_height, left : left + resized_width] = np.asarray(resized)
        array = canvas.astype("float32") / 255.0
        array = np.transpose(array, (2, 0, 1))[None, :, :, :]
        return PreprocessedImage(array, width, height, ratio, left, top)

    def _parse_detections(self, output: Any, prepared: PreprocessedImage, np: Any) -> list[Detection]:
        predictions = np.asarray(output)
        predictions = np.squeeze(predictions)
        if predictions.size == 0:
            return []

        if predictions.ndim == 1:
            predictions = predictions.reshape(1, -1)
        if (
            predictions.ndim == 2
            and 5 <= predictions.shape[0] < predictions.shape[1]
            and predictions.shape[0] <= 256
        ):
            predictions = predictions.T

        raw_yolo_output = predictions.shape[0] > 1000
        detections: list[Detection] = []
        for row in predictions:
            if row.shape[0] < 5:
                continue

            x1, y1, x2, y2, confidence = self._box_and_score(row, raw_yolo_output)
            if confidence < self.confidence_threshold:
                continue

            left = (x1 - prepared.pad_x) / prepared.ratio
            top = (y1 - prepared.pad_y) / prepared.ratio
            right = (x2 - prepared.pad_x) / prepared.ratio
            bottom = (y2 - prepared.pad_y) / prepared.ratio
            left = max(0.0, min(float(prepared.original_width), float(left)))
            top = max(0.0, min(float(prepared.original_height), float(top)))
            right = max(0.0, min(float(prepared.original_width), float(right)))
            bottom = max(0.0, min(float(prepared.original_height), float(bottom)))
            if right <= left or bottom <= top:
                continue
            if self._is_oversized_box(left, top, right, bottom, prepared.original_width, prepared.original_height):
                continue

            detections.append(
                Detection(
                    text="text",
                    confidence=float(confidence),
                    box=[left, top, right, top, right, bottom, left, bottom],
                )
            )

        return self._nms(detections, iou_threshold=0.45)

    @staticmethod
    def _is_oversized_box(left: float, top: float, right: float, bottom: float, width: int, height: int) -> bool:
        box_width = max(0.0, right - left)
        box_height = max(0.0, bottom - top)
        frame_area = max(1.0, float(width * height))
        area_ratio = (box_width * box_height) / frame_area
        width_ratio = box_width / max(1.0, float(width))
        height_ratio = box_height / max(1.0, float(height))
        return area_ratio > 0.32 or width_ratio > 0.92 or height_ratio > 0.55

    @staticmethod
    def _box_and_score(row: Any, raw_yolo_output: bool) -> tuple[float, float, float, float, float]:
        first = float(row[0])
        second = float(row[1])
        third = float(row[2])
        fourth = float(row[3])

        if row.shape[0] == 5:
            confidence = float(row[4])
        elif row.shape[0] == 6:
            confidence = float(row[4])
        else:
            objectness = float(row[4])
            class_score = float(max(row[5:]))
            confidence = objectness * class_score

        if raw_yolo_output:
            center_x = first
            center_y = second
            width = third
            height = fourth
            return (
                center_x - width * 0.5,
                center_y - height * 0.5,
                center_x + width * 0.5,
                center_y + height * 0.5,
                confidence,
            )

        return first, second, third, fourth, confidence

    @staticmethod
    def _nms(detections: list[Detection], iou_threshold: float) -> list[Detection]:
        kept: list[Detection] = []
        for detection in sorted(detections, key=lambda item: item.confidence, reverse=True):
            if all(YoloTextDetector._iou(detection.box, kept_detection.box) < iou_threshold for kept_detection in kept):
                kept.append(detection)
        return kept

    @staticmethod
    def _iou(a: list[float], b: list[float]) -> float:
        a_left, a_top, a_right, a_bottom = a[0], a[1], a[4], a[5]
        b_left, b_top, b_right, b_bottom = b[0], b[1], b[4], b[5]
        inter_left = max(a_left, b_left)
        inter_top = max(a_top, b_top)
        inter_right = min(a_right, b_right)
        inter_bottom = min(a_bottom, b_bottom)
        inter_area = max(0.0, inter_right - inter_left) * max(0.0, inter_bottom - inter_top)
        a_area = max(0.0, a_right - a_left) * max(0.0, a_bottom - a_top)
        b_area = max(0.0, b_right - b_left) * max(0.0, b_bottom - b_top)
        union_area = a_area + b_area - inter_area
        return inter_area / union_area if union_area > 0.0 else 0.0

    def _recognize_detection_text(self, image: Any, detections: list[Detection], np: Any) -> list[Detection]:
        if not self._recognizer:
            return detections

        line_detections = self._group_detections_into_lines(detections)
        recognized: list[Detection] = []
        for detection in line_detections[:32]:
            left, top, right, bottom = detection.box[0], detection.box[1], detection.box[4], detection.box[5]
            if self._is_oversized_box(left, top, right, bottom, image.width, image.height):
                continue
            pad_x = max(3, int((right - left) * 0.15))
            pad_y = max(3, int((bottom - top) * 0.35))
            crop_left = max(0, int(left) - pad_x)
            crop_top = max(0, int(top) - pad_y)
            crop_right = min(image.width, int(right) + pad_x)
            crop_bottom = min(image.height, int(bottom) + pad_y)
            if crop_right <= crop_left or crop_bottom <= crop_top:
                continue

            crop = image.crop((crop_left, crop_top, crop_right, crop_bottom))
            text, score = self._read_crop(crop, np)
            if not text:
                continue

            recognized.append(
                Detection(
                    text=text,
                    confidence=min(detection.confidence, score),
                    box=detection.box,
                )
            )

        return recognized

    @staticmethod
    def _group_detections_into_lines(detections: list[Detection]) -> list[Detection]:
        @dataclass
        class LineGroup:
            detections: list[Detection]
            center_y: float
            height: float

        line_groups: list[LineGroup] = []
        for detection in sorted(detections, key=lambda item: (item.box[1] + item.box[5]) * 0.5):
            left, top, right, bottom = detection.box[0], detection.box[1], detection.box[4], detection.box[5]
            center_y = (top + bottom) * 0.5
            height = max(1.0, bottom - top)

            best_index: int | None = None
            best_distance = float("inf")
            for index, line in enumerate(line_groups):
                distance = abs(center_y - line.center_y)
                if distance > max(10.0, min(height, line.height) * 0.9):
                    continue
                if distance < best_distance:
                    best_distance = distance
                    best_index = index

            if best_index is None:
                line_groups.append(LineGroup([detection], center_y, height))
            else:
                line = line_groups[best_index]
                line.detections.append(detection)
                count = len(line.detections)
                line.center_y = ((line.center_y * (count - 1)) + center_y) / count
                line.height = ((line.height * (count - 1)) + height) / count

        lines: list[Detection] = []
        for line_group in line_groups:
            current: Detection | None = None
            for detection in sorted(line_group.detections, key=lambda item: item.box[0]):
                if current is None:
                    current = detection
                    continue

                line_height = max(1.0, current.box[5] - current.box[1])
                gap = detection.box[0] - current.box[4]
                if gap > max(48.0, line_height * 3.0):
                    lines.append(current)
                    current = detection
                    continue

                merged_left = min(current.box[0], detection.box[0])
                merged_top = min(current.box[1], detection.box[1])
                merged_right = max(current.box[4], detection.box[4])
                merged_bottom = max(current.box[5], detection.box[5])
                current = Detection(
                    text="text",
                    confidence=max(current.confidence, detection.confidence),
                    box=[
                        merged_left,
                        merged_top,
                        merged_right,
                        merged_top,
                        merged_right,
                        merged_bottom,
                        merged_left,
                        merged_bottom,
                    ],
                )
            if current is not None:
                lines.append(current)

        return sorted(lines, key=lambda item: (item.box[1], item.box[0]))

    def _read_crop(self, crop: Any, np: Any) -> tuple[str, float]:
        scaled = crop
        if crop.height < 32:
            scale = 32.0 / max(1, crop.height)
            scaled = crop.resize((max(1, int(crop.width * scale)), 32))

        result, _ = self._recognizer(np.asarray(scaled))
        if not result:
            return "", 0.0

        texts: list[str] = []
        scores: list[float] = []
        for item in result:
            if not item or len(item) < 3:
                continue
            text = str(item[1]).strip()
            if not text:
                continue
            try:
                score = float(item[2])
            except (TypeError, ValueError):
                score = 0.0
            texts.append(text)
            scores.append(score)

        if not texts:
            return "", 0.0

        return " ".join(texts), min(scores) if scores else 0.0


class StreamSaverHandler(socketserver.StreamRequestHandler):
    engine: YoloTextDetector

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
            return {
                "type": "health",
                "ok": True,
                "engine": "yolo",
                "backend": getattr(self.engine, "backend", "unknown"),
                "device": getattr(self.engine, "device", "unknown"),
                "model": getattr(self.engine, "model_path", "unknown"),
            }

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
                print(f"failed to write detector frame dump: {exc}", flush=True)
            try:
                source_dump_path.write_bytes(base64.b64decode(source_image))
            except Exception as exc:
                print(f"failed to write source frame dump: {exc}", flush=True)
        print(
            f"detector request frame={frame_id} width={request.get('width')} height={request.get('height')} "
            f"source_width={request.get('source_width')} source_height={request.get('source_height')} "
            f"warmup={warmup} image_base64_bytes={len(image)} source_image_base64_bytes={len(source_image)}",
            flush=True,
        )
        detections = self.engine.recognize_png_base64(image)
        elapsed_ms = int((time.monotonic() - started) * 1000)
        print(
            f"detector response frame={frame_id} detections={len(detections)} elapsed_ms={elapsed_ms}",
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
    parser = argparse.ArgumentParser(description="Stream Saver YOLO text detector worker")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=48741)
    parser.add_argument("--model", default="yolo11n-text.onnx")
    parser.add_argument(
        "--backend",
        choices=("onnxruntime", "directml", "cuda", "openvino", "openvino-onnx"),
        default="onnxruntime",
    )
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--confidence", type=float, default=0.25)
    parser.add_argument("--no-recognize", dest="recognize_text", action="store_false")
    parser.set_defaults(recognize_text=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    StreamSaverHandler.engine = YoloTextDetector(
        model_path=args.model,
        backend=args.backend,
        device=args.device,
        image_size=args.imgsz,
        confidence_threshold=args.confidence,
        recognize_text=args.recognize_text,
    )
    with ThreadingServer((args.host, args.port), StreamSaverHandler) as server:
        print(
            f"stream-saver YOLO worker listening on {args.host}:{args.port} "
            f"backend={args.backend} device={args.device} model={args.model}",
            flush=True,
        )
        server.serve_forever()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
