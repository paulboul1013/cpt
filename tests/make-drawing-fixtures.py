#!/usr/bin/env python3
"""Regenerate drawing/Image XObject fixtures and explicit CLI expectations.

Run from the repo root. Core tests consume static PDFs without Python.
"""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parent
gs = runpy.run_path(str(ROOT / "make-gs-fixtures.py"))
pdf = gs["pdf"]
IMAGE = (b"<< /Type /XObject /Subtype /Image /Width 1 /Height 1 "
         b"/ColorSpace /DeviceGray /BitsPerComponent 8 /Length 1 >>\nstream\nx\nendstream")
FORM = b"<< /Subtype /Form /BBox [0 0 1 1] /Length 0 >>\nstream\n\nendstream"
RES = b"<< /Font << /F1 5 0 R >> /XObject << /Im 6 0 R >> >>"
CN_RES = b"<< /Font << /F1 5 0 R >> /XObject << /Im 8 0 R >> >>"
DRAW = (b"1 w 0 J 10 M 2 j [] 0 d 0 0 m 20 0 l 20 20 l h W* n "
        b"0 0 1 1 re f 0 0 m 1 2 3 4 5 6 c B* 0 0 10 10 re W S "
        b"q 10 0 0 10 100 100 cm /Im Do Q ")
FIXTURES = {
    "drawing-english": pdf([(RES, DRAW + gs["TEXT"])], [gs["FONT"], IMAGE]),
    "drawing-chinese": pdf([(CN_RES, DRAW + gs["CN_TEXT"])],
        [gs["cn_font"], gs["cid"], gs["chinese_tools"]["stream"](gs["chinese_tools"]["CMAP"]), IMAGE]),
    "drawing-image-only": pdf([(RES, DRAW)], [gs["FONT"], IMAGE]),
    "drawing-bad-dash": pdf([(RES, b"[0 0] 0 d")], [gs["FONT"], IMAGE]),
    "drawing-bad-path": pdf([(RES, b"0 0 m n 1 1 l")], [gs["FONT"], IMAGE]),
    "drawing-missing-image": pdf([(b"<< >>", b"/Im Do")], []),
    "drawing-bad-image": pdf([(RES, b"/Im Do")], [gs["FONT"], IMAGE.replace(b"/Width 1", b"/Width 0")]),
}
LATER_RES = b"<< /Font << /F1 7 0 R >> /XObject << /Im 8 0 R >> >>"
for suffix, resources, image in [("missing", b"<< >>", IMAGE), ("form", LATER_RES, FORM)]:
    first_res = LATER_RES if suffix == "missing" else LATER_RES.replace(b"/Im 8 0 R", b"/Im 9 0 R")
    FIXTURES["drawing-later-" + suffix] = pdf(
        [(first_res, DRAW + gs["TEXT"]), (resources, b"/Im Do")], [gs["FONT"], image, IMAGE])
EXPECTED = {
    "drawing-english": (0, "ABCA\n", ""),
    "drawing-chinese": (0, "中文測試\n", ""),
    "drawing-image-only": (0, "", "pdftext: no extractable text layer\n"),
}
for name, page, offset, code, obj, reason in [
    ("drawing-bad-dash", 3, 8, 3, None, "wrong operator operand type"),
    ("drawing-bad-path", 3, 12, 3, None, "path operator requires a current point"),
    ("drawing-missing-image", 3, 4, 3, 3, "missing XObject resource or value"),
    ("drawing-bad-image", 3, 4, 3, 6, "Image dimensions must be positive integers"),
    ("drawing-later-missing", 5, 4, 3, 5, "missing XObject resource or value"),
    ("drawing-later-form", 5, 4, 4, 8, "only Image XObjects are supported"),
]:
    data = FIXTURES[name]
    page_at = data.index(f"{page} 0 obj\n".encode())
    if obj is not None:
        at = data.index(f"{obj} 0 obj\n".encode())
        reason = f"XObject byte {at} (xobject): {reason}"
    kind = "malformed" if code == 3 else "unsupported"
    EXPECTED[name] = (code, "", f"content: {kind} error at byte {page_at}: decoded byte {offset}: {reason}\n")

if __name__ == "__main__":
    rows = []
    for name, data in FIXTURES.items():
        (ROOT / "fixtures" / f"{name}.pdf").write_bytes(data)
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
                       if not line.startswith("drawing-"))
    manifest.write_text(previous + "".join(rows))
    print(f"Generated {len(FIXTURES)} PDFs and {len(rows)} cases")
