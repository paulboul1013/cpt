#!/usr/bin/env python3
"""Regenerate the A-subset PDFs and handwritten expectations; no product calls.

Run from any directory. Core tests use the saved PDFs and do not need Python.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent
FONT = (b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding "
        b"/FirstChar 32 /LastChar 126 /Widths [" + b"600 " * 95 +
        b"] /FontDescriptor << /MissingWidth 600 >> >>")
EMPTY = b"/Span << /ActualText <> >> BDC "
START = b"BT /F1 12 Tf 1 0 0 1 24 180 Tm "


def pdf(contents):
    n = len(contents)
    out = bytearray(b"%PDF-1.4\n")
    offsets = [0] * (4 + 2 * n)

    def obj(num, body):
        offsets[num] = len(out)
        out.extend(b"%d 0 obj\n" % num + body + b"\nendobj\n")

    obj(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    obj(2, b"<< /Type /Pages /MediaBox [0 0 400 240] /Count %d /Kids [" % n +
        b" ".join(b"%d 0 R" % (3 + 2 * i) for i in range(n)) + b"] >>")
    for i, content in enumerate(contents):
        obj(3 + 2 * i, b"<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>" %
            (3 + 2 * n, 4 + 2 * i))
        obj(4 + 2 * i, b"<< /Length %d >>\nstream\n" % len(content) + content + b"\nendstream")
    obj(3 + 2 * n, FONT)
    xref = len(out)
    out.extend(b"xref\n0 %d\n0000000000 65535 f \n" % len(offsets))
    for at in offsets[1:]:
        out.extend(b"%010d 00000 n \n" % at)
    out.extend(b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(offsets), xref))
    return bytes(out), offsets


SUCCESS = {
    "marked-pass": (START + b"/Span << >> BDC (ABC) Tj EMC ET", b"ABC\n"),
    "marked-empty": (START + b"(A) Tj " + EMPTY + b"(BC) Tj EMC (A) Tj ET", b"A A\n"),
    "marked-literal": (START + b"/Span << /ActualText () >> BDC (BC) Tj EMC (A) Tj ET", b"A\n"),
    "marked-unmapped": (START + EMPTY + b"<00> Tj EMC (A) Tj ET", b"A\n"),
    "marked-all-empty": (START + EMPTY + b"(ABC) Tj EMC ET", b""),
    "marked-control": (START + b"(A) Tj (BC) Tj (A) Tj ET", b"ABCA\n"),
}
for name, (content, expected) in SUCCESS.items():
    data, _ = pdf([content])
    (ROOT / "fixtures" / f"{name}.pdf").write_bytes(data)
    (ROOT / "golden" / f"{name}.stdout").write_bytes(expected)

(ROOT / "golden/marked-all-empty.stderr").write_text("pdftext: no extractable text layer\n")

# Error text and operator selection are written here independently of pdftext.
ERRORS = {
    "marked-later-unclosed": (EMPTY, "malformed", "", "unfinished marked-content scope at end of content"),
    "marked-later-type": (b"/Span << /ActualText null >> BDC EMC", "malformed", "BDC", "marked-content ActualText must be a string"),
    "marked-later-nonempty": (b"/Span << /ActualText (x) >> BDC EMC", "unsupported", "BDC", "marked-content nonempty ActualText is unsupported"),
    "marked-later-named": (b"/Span /P1 BDC EMC", "unsupported", "BDC", "marked-content requires /Span and inline properties"),
    "marked-cross-text": (EMPTY + b"BT EMC ET", "malformed", "EMC", "EMC crosses BT/ET text object"),
    "marked-cross-marked": (b"BT " + EMPTY + b"ET EMC", "malformed", "ET", "ET crosses marked-content scope"),
}
for name, (bad, kind, operator, reason) in ERRORS.items():
    later = name.startswith("marked-later")
    data, offsets = pdf([START + b"(A) Tj ET", bad] if later else [bad])
    at = bad.index(operator.encode()) if operator else len(bad)
    (ROOT / "fixtures" / f"{name}.pdf").write_bytes(data)
    (ROOT / "golden" / f"{name}.stderr").write_text(
        f"content: {kind} error at byte {offsets[5 if later else 3]}: decoded byte {at}: {reason}\n")
