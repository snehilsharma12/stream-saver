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
  "source_width": 1920,
  "source_height": 1080,
  "format": "png-base64",
  "image": "...",
  "source_image": "..."
}
```

`width` and `height` describe the PNG sent to OCR. `source_width` and
`source_height` describe the original OBS source frame. `source_image` is
optional and is used only for full-frame debug dumps when the OCR image was
downscaled.

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

`box` is an eight-number quadrilateral in OCR image coordinates.

## Error

```json
{"type":"error","message":"failed to decode image"}
```
