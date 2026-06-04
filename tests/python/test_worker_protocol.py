import json
import unittest

import numpy as np

from worker.stream_saver_ocr import Detection, PreprocessedImage, StreamSaverHandler, YoloTextDetector


class FakeEngine:
    backend = "fake"
    device = "cpu"
    model_path = "fake.onnx"

    def recognize_png_base64(self, image_b64):
        if image_b64 == "":
            return []
        return [Detection("text", 0.94, [1, 2, 3, 2, 3, 4, 1, 4])]


class WorkerProtocolTests(unittest.TestCase):
    def setUp(self):
        self.handler = object.__new__(StreamSaverHandler)
        self.handler.engine = FakeEngine()

    def test_health(self):
        response = self.handler._dispatch({"type": "health"})
        self.assertEqual(response["type"], "health")
        self.assertTrue(response["ok"])

    def test_ocr_result_shape(self):
        response = self.handler._dispatch(
            {"type": "ocr", "frame_id": 7, "image": "not-real-image"}
        )
        self.assertEqual(response["type"], "ocr_result")
        self.assertEqual(response["frame_id"], 7)
        self.assertEqual(response["detections"][0]["text"], "text")
        self.assertEqual(len(response["detections"][0]["box"]), 8)

        encoded = json.dumps(response, separators=(",", ":"))
        self.assertIn('"type":"ocr_result"', encoded)

    def test_empty_image_returns_empty_detection_list(self):
        response = self.handler._dispatch({"type": "ocr", "frame_id": 8, "image": ""})
        self.assertEqual(response["detections"], [])

    def test_unknown_request(self):
        response = self.handler._dispatch({"type": "wat"})
        self.assertEqual(response["type"], "error")


class YoloResultParsingTests(unittest.TestCase):
    def test_parses_yolo_xyxy_result_shape(self):
        engine = object.__new__(YoloTextDetector)
        engine.confidence_threshold = 0.25
        prepared = PreprocessedImage(
            array=None,
            original_width=640,
            original_height=360,
            ratio=1.0,
            pad_x=0.0,
            pad_y=140.0,
        )
        result = np.array([[[100.0, 160.0, 260.0, 190.0, 0.97, 0.0]]], dtype=np.float32)

        detections = engine._parse_detections(result, prepared, np)
        self.assertEqual(len(detections), 1)
        self.assertEqual(detections[0].text, "text")
        self.assertAlmostEqual(detections[0].confidence, 0.97, places=2)
        self.assertEqual(detections[0].box, [100.0, 20.0, 260.0, 20.0, 260.0, 50.0, 100.0, 50.0])

    def test_groups_rows_without_vertical_snowballing(self):
        detections = [
            Detection("text", 0.90, [10, 10, 60, 10, 60, 22, 10, 22]),
            Detection("text", 0.88, [70, 11, 130, 11, 130, 23, 70, 23]),
            Detection("text", 0.86, [12, 46, 80, 46, 80, 58, 12, 58]),
            Detection("text", 0.84, [90, 47, 150, 47, 150, 59, 90, 59]),
            Detection("text", 0.82, [14, 82, 90, 82, 90, 94, 14, 94]),
        ]

        lines = YoloTextDetector._group_detections_into_lines(detections)

        self.assertEqual(len(lines), 3)
        self.assertEqual(lines[0].box, [10, 10, 130, 10, 130, 23, 10, 23])
        self.assertEqual(lines[1].box, [12, 46, 150, 46, 150, 59, 12, 59])
        self.assertEqual(lines[2].box, [14, 82, 90, 82, 90, 94, 14, 94])


if __name__ == "__main__":
    unittest.main()
