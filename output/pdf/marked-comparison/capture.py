#!/usr/bin/env python3
"""Recheck saved fixtures, real CLI/trace output and Poppler rendering.

Run after make tests/geometry-test tests/font-text-test pdftext.
Requires Poppler and Pillow only for this optional visual check.
"""
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent


def run(command, stem, status=0):
    result = subprocess.run([str(arg) for arg in command], cwd=ROOT, capture_output=True)
    (OUT / f"{stem}.stdout").write_bytes(result.stdout)
    (OUT / f"{stem}.stderr").write_bytes(result.stderr)
    (OUT / f"{stem}.exit").write_text(f"{result.returncode}\n")
    assert result.returncode == status, (command, result.returncode, result.stderr)
    return result


def lines(data):
    return [json.loads(line) for line in data.splitlines()]


def near(actual, expected):
    assert abs(actual - expected) < 1e-8, (actual, expected)


for kind in ("empty", "control"):
    path = ROOT / f"tests/fixtures/marked-{kind}.pdf"
    text = run([ROOT / "pdftext", path], f"{kind}-text")
    assert text.stdout == (b"A A\n" if kind == "empty" else b"ABCA\n") and not text.stderr
    items = run([ROOT / "pdftext", "--dump-text-items", path], f"{kind}-items")
    raw = run([ROOT / "tests/geometry-test", path], f"{kind}-raw")
    decoded = run([ROOT / "tests/font-text-test", path], f"{kind}-font")
    events = lines(raw.stdout)
    assert len(events) == 3
    for event, x, dx in zip(events, (24, 31.2, 45.6), (7.2, 14.4, 7.2)):
        near(event["origin"][0], x)
        near(event["origin"][1], 180)
        near(event["advance"][0], dx)
    retained = lines(items.stdout)
    assert len(retained) == (2 if kind == "empty" else 3)
    assert [item["order"] for item in retained] == ([0, 2] if kind == "empty" else [0, 1, 2])
    assert [item["sequence"] for item in retained] == list(range(len(retained)))
    near(retained[-1]["x"], 45.6)
    assert len(lines(decoded.stdout)) == 2 * len(retained)
    subprocess.run(["pdftoppm", "-r", "144", "-singlefile", "-png", str(path), str(OUT / kind)], check=True)
    run(["pdftotext", "-enc", "UTF-8", path, "-"], f"{kind}-poppler")

# Markers have no drawing effect. Both PNGs must be pixel-identical.
with Image.open(OUT / "empty.png") as a, Image.open(OUT / "control.png") as b:
    assert ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getbbox() is None

original = ROOT / "output/pdf/chinese-comparison/test_git.pdf"
if original.exists():
    digest = hashlib.sha256(original.read_bytes()).hexdigest()
    assert digest == "d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70"
    (OUT / "test_git.sha256").write_text(digest + "\n")
    for mode, args in (("text", []), ("items", ["--dump-text-items"])):
        result = run([ROOT / "pdftext", *args, original], f"test_git-{mode}", status=4)
        assert not result.stdout
        assert result.stderr == b"content: unsupported error at byte 3796: decoded byte 125: content operator is not supported\n"
else:
    print("Original test_git.pdf unavailable; original-file verification skipped.")
print("Marked-content visual/text/geometry comparisons passed.")
