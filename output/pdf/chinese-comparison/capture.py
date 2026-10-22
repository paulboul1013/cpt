#!/usr/bin/env python3
"""Optional visual acceptance; requires fontTools and Poppler, not core tests.

Build an embedded Type0/Identity-H PDF from an installed CJK font, render the
actual PDF, compare pdftext with Poppler and independently computed advances.
Run: python3 output/pdf/chinese-comparison/capture.py
"""
from pathlib import Path
from io import BytesIO
import argparse
import hashlib
import json
import runpy
import subprocess

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
TEXT = "中文提取測試"


def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", default="/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf")
    args = parser.parse_args()
    helpers = runpy.run_path(str(ROOT / "tests/make-chinese-fixtures.py"))
    font = TTFont(args.font, recalcTimestamp=False)
    sub = subset.Subsetter()
    sub.populate(text=TEXT)
    sub.subset(font)
    font_data = BytesIO()
    font.save(font_data)
    cmap = font.getBestCmap()
    upem = font["head"].unitsPerEm
    codes, mappings, widths = [], [], {}
    for char in TEXT:
        glyph = cmap[ord(char)]
        code = font.getGlyphID(glyph)
        codes.append(code)
        widths[code] = font["hmtx"].metrics[glyph][0] * 1000 / upem
    for char in sorted(set(TEXT)):
        mappings.append(f"<{font.getGlyphID(cmap[ord(char)]):04X}> <{char.encode('utf-16-be').hex()}>")
    to_unicode = ("begincmap /CMapType 2 def "
        "1 begincodespacerange <0000> <FFFF> endcodespacerange " +
        str(len(mappings)) + " beginbfchar\n" + "\n".join(mappings) + "\nendbfchar endcmap").encode()
    encoded = "".join(f"{code:04X}" for code in codes)
    content = f"BT /F1 24 Tf 1 0 0 1 24 180 Tm <{encoded}> Tj ET".encode()
    w = " ".join(f"{code} [{width:.9f}]" for code, width in sorted(widths.items()))
    descriptor = (b"<< /Type /FontDescriptor /FontName /DroidSansFallback /Flags 4 "
        b"/FontBBox [0 -300 1100 1100] /ItalicAngle 0 /Ascent 1000 /Descent -300 "
        b"/CapHeight 1000 /StemV 80 /FontFile2 9 0 R >>")
    embedded = font_data.getvalue()
    # Objects 5..9: root font, CID font, ToUnicode, descriptor, font program.
    pdf = helpers["pdf"]([(b"<< /Font << /F1 5 0 R >> >>", content)], [
        b"<< /Type /Font /Subtype /Type0 /BaseFont /DroidSansFallback /Encoding /Identity-H "
        b"/DescendantFonts [6 0 R] /ToUnicode 7 0 R >>",
        ("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /DroidSansFallback "
         "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
         "/CIDToGIDMap /Identity /FontDescriptor 8 0 R /DW 1000 /W [" + w + "] >>").encode(),
        helpers["stream"](to_unicode, True), descriptor,
        b"<< /Length %d /Length1 %d >>\nstream\n" % (len(embedded), len(embedded)) + embedded + b"\nendstream",
    ])
    path = OUT / "embedded-chinese.pdf"
    path.write_bytes(pdf)
    actual = run([str(ROOT / "pdftext"), str(path)])
    expected = (TEXT + "\n").encode()
    assert actual.stdout == expected and not actual.stderr
    reference = run(["pdftotext", "-enc", "UTF-8", str(path), "-"])
    assert reference.stdout.decode().strip() == TEXT
    trace = run([str(ROOT / "pdftext"), "--dump-text-items", str(path)])
    item = json.loads(trace.stdout)
    expected_advance = sum(widths[code] for code in codes) * 24 / 1000
    assert abs(item["advance"][0] - expected_advance) < 1e-7
    run(["pdftoppm", "-f", "1", "-singlefile", "-scale-to", "1000", "-png", str(path), str(OUT / "page1")])
    (OUT / "actual.txt").write_bytes(actual.stdout)
    (OUT / "reference.txt").write_bytes(reference.stdout)
    (OUT / "items.jsonl").write_bytes(trace.stdout)
    manifest = {
        "input_sha256": hashlib.sha256(pdf).hexdigest(),
        "binary_sha256": hashlib.sha256((ROOT / "pdftext").read_bytes()).hexdigest(),
        "font_sha256": hashlib.sha256(Path(args.font).read_bytes()).hexdigest(),
        "expected_text": TEXT, "expected_advance": expected_advance,
        "actual_advance": item["advance"][0], "text_and_geometry_match": True,
        "poppler_version": run(["pdftotext", "-v"]).stderr.decode().strip(),
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(manifest, ensure_ascii=False))


if __name__ == "__main__":
    main()
