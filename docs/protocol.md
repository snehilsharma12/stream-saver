# Stream Saver Detector Protocol

The OBS plugin and detector worker communicate over localhost TCP. Messages are newline-delimited JSON.

## Request: health

```json
{"type":"health"}
```

Response:

```json
{"type":"health","ok":true,"engine":"yolo","backend":"onnxruntime"}
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
  "debug": false,
  "format": "png-base64",
  "image": "...",
  "source_image": "..."
}
```

`width` and `height` describe the PNG sent to OCR. `source_width` and
`source_height` describe the original OBS source frame. `debug` enables worker
frame dumps. `source_image` is optional and is used only for full-frame debug
dumps when the OCR image was downscaled.

Response:

```json
{
  "type": "ocr_result",
  "frame_id": 42,
  "detections": [
    {
      "text": "text",
      "confidence": 0.94,
      "box": [100, 120, 260, 120, 260, 150, 100, 150]
    }
  ]
}
```

`box` is an eight-number quadrilateral in detector image coordinates. YOLO mode
uses `"text":"text"` as a region label; it does not recognize phrase content.

## Error

```json
{"type":"error","message":"failed to decode image"}
```
