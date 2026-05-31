# Stream Saver OCR Protocol

The OBS plugin and OCR worker communicate over localhost TCP. Messages are newline-delimited JSON.

## Request: health

```json
{"type":"health"}
```

Response:

```json
{"type":"health","ok":true,"engine":"paddleocr"}
```

## Request: ocr

```json
{
  "type": "ocr",
  "frame_id": 42,
  "width": 1280,
  "height": 720,
  "format": "png-base64",
  "image": "..."
}
```

Response:

```json
{
  "type": "ocr_result",
  "frame_id": 42,
  "detections": [
    {
      "text": "123 Main St",
      "confidence": 0.94,
      "box": [100, 120, 260, 120, 260, 150, 100, 150]
    }
  ]
}
```

`box` is an eight-number quadrilateral in source image coordinates.

## Error

```json
{"type":"error","message":"failed to decode image"}
```
