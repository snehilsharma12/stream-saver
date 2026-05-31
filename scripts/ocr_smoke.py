import base64
import io
import json
import socket
import sys

from PIL import Image, ImageDraw, ImageFont


def main() -> int:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 48741

    image = Image.new("RGB", (960, 540), "white")
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 96)
    draw.text((120, 180), "Steam", font=font, fill="black")

    buffer = io.BytesIO()
    image.save(buffer, format="PNG")

    request = {
        "type": "ocr",
        "frame_id": 1,
        "width": 960,
        "height": 540,
        "format": "png-base64",
        "image": base64.b64encode(buffer.getvalue()).decode("ascii"),
    }

    with socket.create_connection(("127.0.0.1", port), timeout=10) as client:
        client.settimeout(90)
        client.sendall((json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8"))
        print(client.makefile().readline().strip())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
