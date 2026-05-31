import json
import unittest

from worker.stream_saver_ocr import Detection, OcrEngine, StreamSaverHandler


class FakeEngine:
    def recognize_png_base64(self, image_b64):
        if image_b64 == "":
            return []
        return [Detection("123 Main St", 0.94, [1, 2, 3, 2, 3, 4, 1, 4])]


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
        self.assertEqual(response["detections"][0]["text"], "123 Main St")
        self.assertEqual(len(response["detections"][0]["box"]), 8)

        encoded = json.dumps(response, separators=(",", ":"))
        self.assertIn('"type":"ocr_result"', encoded)

    def test_empty_image_returns_empty_detection_list(self):
        response = self.handler._dispatch({"type": "ocr", "frame_id": 8, "image": ""})
        self.assertEqual(response["detections"], [])

    def test_unknown_request(self):
        response = self.handler._dispatch({"type": "wat"})
        self.assertEqual(response["type"], "error")


class OcrResultParsingTests(unittest.TestCase):
    def test_parses_paddleocr_3_result_shape(self):
        engine = object.__new__(OcrEngine)
        result = [
            {
                "rec_texts": ["123 Main St"],
                "rec_scores": [0.97],
                "rec_polys": [[[1, 2], [3, 2], [3, 4], [1, 4]]],
            }
        ]

        detections = engine._parse_result(result)
        self.assertEqual(len(detections), 1)
        self.assertEqual(detections[0].text, "123 Main St")
        self.assertEqual(detections[0].confidence, 0.97)
        self.assertEqual(detections[0].box, [1.0, 2.0, 3.0, 2.0, 3.0, 4.0, 1.0, 4.0])


if __name__ == "__main__":
    unittest.main()
