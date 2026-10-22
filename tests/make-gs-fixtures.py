#!/usr/bin/env python3
"""Regenerate gs fixtures and hand-authored expectations. Not needed by make test.

Run from the repository root: python3 tests/make-gs-fixtures.py
"""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parent
font_tools = runpy.run_path(str(ROOT / "make-font-fixtures.py"))
chinese_tools = runpy.run_path(str(ROOT / "make-chinese-fixtures.py"))
pdf = font_tools["pdf"]
GSA = b"<< /Type /ExtGState /SM 0.02 /SMask /None /AIS false /SA true /ca 1 /CA 1 >>"
FONT = b"<< /Type /Font /Subtype /Type1 /BaseFont /Test /FirstChar 65 /LastChar 67 /Widths [600 650 700] >>"
RES = b"<< /Font << /F1 5 0 R >> /ExtGState << /GSa 6 0 R >> >>"
TEXT = b"BT /F1 12 Tf 1 0 0 1 24 200 Tm (ABC) Tj (A) Tj ET"
CN_TEXT = b"BT /F1 12 Tf 1 0 0 1 24 200 Tm <00010002> Tj [<0003> -100 <0004>] TJ ET"
cn_font = b"<< /Type /Font /Subtype /Type0 /BaseFont /Chinese /Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>"
cid = b"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Chinese /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /DW 1000 /W [1 [1000 900 800]] >>"
CN_RES = b"<< /Font << /F1 5 0 R >> /ExtGState << /GSa " + GSA + b" >> >>"
FIXTURES = {
    "gs-english": pdf([(RES, b"/GSa gs " + TEXT.replace(b"(A) Tj", b"/GSa gs (A) Tj"))], [FONT, GSA]),
    "gs-english-control": pdf([(RES, TEXT)], [FONT, GSA]),
    "gs-chinese": pdf([(CN_RES, b"/GSa gs " + CN_TEXT.replace(b"[<0003>", b"/GSa gs [<0003>"))], [cn_font, cid, chinese_tools["stream"](chinese_tools["CMAP"])]),
    "gs-chinese-control": pdf([(CN_RES, CN_TEXT)], [cn_font, cid, chinese_tools["stream"](chinese_tools["CMAP"])]),
    "gs-bad-alpha": pdf([(RES, b"/GSa gs " + TEXT)], [FONT, b"<< /CA 2 >>"]),
}
for kind, state in [("font", b"<< /SM .02 /Font [7 0 R 12] >>"),
                    ("unknown", b"<< /ca 1 /Unknown true >>")]:
    first = b"<< /Font << /F1 7 0 R >> /ExtGState << /GSa 8 0 R >> >>"
    later = b"<< /Font << /F1 7 0 R >> /ExtGState << /GSa 9 0 R >> >>"
    FIXTURES["gs-later-" + kind] = pdf([(first, b"/GSa gs " + TEXT), (later, b"/GSa gs")], [FONT, GSA, state])

EXPECTED = {
    "gs-english": (0, "ABCA\n", ""),
    "gs-english-control": (0, "ABCA\n", ""),
    "gs-chinese": (0, "中文測試\n", ""),
    "gs-chinese-control": (0, "中文測試\n", ""),
}
for name, obj, page, code, reason in [
    ("gs-bad-alpha", 6, 3, 3, "ExtGState SM/CA/ca must be finite number in 0..1"),
    ("gs-later-font", 9, 5, 4, "ExtGState Font is unsupported"),
    ("gs-later-unknown", 9, 5, 4, "unsupported ExtGState key"),
]:
    data = FIXTURES[name]
    page_at = data.index(f"{page} 0 obj\n".encode())
    obj_at = data.index(f"{obj} 0 obj\n".encode())
    classification = "malformed" if code == 3 else "unsupported"
    EXPECTED[name] = (code, "", f"content: {classification} error at byte {page_at}: decoded byte 5: ExtGState byte {obj_at} (extgstate): {reason}\n")

if __name__ == "__main__":
    for name, data in FIXTURES.items():
        (ROOT / "fixtures" / (name + ".pdf")).write_bytes(data)
    rows = []
    for name, (code, stdout, stderr) in EXPECTED.items():
        for suffix, value in [("stdout", stdout), ("stderr", stderr)]:
            if value:
                (ROOT / "golden" / f"{name}.{suffix}").write_text(value, encoding="utf-8")
        kind = {0: "success", 3: "malformed", 4: "unsupported"}[code]
        out = f"tests/golden/{name}.stdout" if stdout else "-"
        err = f"tests/golden/{name}.stderr" if stderr else "-"
        rows.append(f"{name}|{kind}|text:tests/fixtures/{name}.pdf|{code}|{out}|{err}\n")
        if code:
            rows.append(f"{name}-items|{kind}|items:tests/fixtures/{name}.pdf|{code}|-|{err}\n")
    manifest = ROOT / "fixtures.tsv"
    previous = "".join(line for line in manifest.read_text().splitlines(keepends=True)
                       if not line.startswith("gs-"))
    manifest.write_text(previous + "".join(rows))
    print(f"Generated {len(FIXTURES)} PDFs and {len(rows)} manifest rows")
