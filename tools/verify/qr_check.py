#!/usr/bin/env python3
"""Decode the QR matrix the logic tests dump, with an independent reader.

Usage:
  set QR_DUMP=%TEMP%\\qr_matrix.txt
  build\\tests\\qr_tests.exe
  python tools\\verify\\qr_check.py %TEMP%\\qr_matrix.txt [expected-text]

The dump is rows of '0' (light) and '1' (dark) joined by newlines, straight
from qrcode.h. This script scales it up with the specified 4-module quiet
zone and hands it to OpenCV's detector — an implementation that shares no
code with ours. If it prints the expected URL, a phone pointed at the image
reads it too.

Needs: pip install opencv-python-headless numpy
"""
from __future__ import annotations

import sys

import cv2
import numpy as np

EXPECTED = "https://www.windows.com/stopcode"
QUIET_ZONE = 4  # ISO/IEC 18004: four light modules on every side


def load(path: str) -> np.ndarray:
    lines = [line.strip() for line in open(path, encoding="utf-8") if line.strip()]
    if not lines or any(len(line) != len(lines) for line in lines):
        raise SystemExit(f"{path}: not a square matrix of 0/1 rows")
    return np.array([[1 if ch == "1" else 0 for ch in line] for line in lines], dtype=np.uint8)


def render(matrix: np.ndarray, scale: int = 12) -> np.ndarray:
    n = matrix.shape[0]
    card = np.full((n + 2 * QUIET_ZONE, n + 2 * QUIET_ZONE), 255, dtype=np.uint8)
    card[QUIET_ZONE:QUIET_ZONE + n, QUIET_ZONE:QUIET_ZONE + n] = np.where(matrix == 1, 0, 255)
    return np.kron(card, np.ones((scale, scale), dtype=np.uint8))


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    expected = sys.argv[2] if len(sys.argv) > 2 else EXPECTED

    matrix = load(path)
    image = render(matrix)
    image_path = path.rsplit(".", 1)[0] + ".png"
    cv2.imwrite(image_path, image)

    detector = cv2.QRCodeDetector()
    data, points, _ = detector.detectAndDecode(image)
    if not data:
        data, points, _ = detector.detectAndDecodeCurved(image)

    size = matrix.shape[0]
    print(f"matrix: {size}x{size} -> {image_path}")
    if not data:
        print("FAIL: no QR code detected (the matrix does not decode)")
        return 1
    print(f"decoded: {data}")
    if data != expected:
        print(f"FAIL: expected {expected}")
        return 1
    print("PASS: the matrix decodes to the expected text")
    return 0


if __name__ == "__main__":
    sys.exit(main())
