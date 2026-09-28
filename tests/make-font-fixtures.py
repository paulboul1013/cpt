#!/usr/bin/env python3
"""Regenerate the static M9 font fixtures in tests/fixtures/.

Not used by `make test`; the generated PDFs are committed. Run from the
repository root: python3 tests/make-font-fixtures.py

HELVETICA_WINANSI holds /Widths for codes 32..255 under PDF WinAnsiEncoding,
taken from Adobe's Helvetica AFM (phvr8a.afm, Version 001.006, glyph names per
PDF Reference Appendix D). The 1990 AFM has no Euro; 0x80 uses 556 from URW
NimbusSans-Regular, the font Poppler substitutes for Helvetica. Every other
value equals NimbusSans' hmtx advance (checked by
output/pdf/m9-comparison/capture.py). Unused WinAnsi codes use bullet (350).
"""
import os

HELVETICA_WINANSI = [
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 350,
    556, 350, 222, 556, 333, 1000, 556, 556, 333, 1000, 667, 333, 1000, 350, 611, 350,
    350, 222, 222, 333, 333, 350, 556, 1000, 333, 1000, 500, 333, 944, 350, 500, 667,
    278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,
    400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
    667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
    556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500,
]
assert len(HELVETICA_WINANSI) == 224


def widths(values):
    return b"[" + b" ".join(str(v).encode() for v in values) + b"]"


HELVETICA = (b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding "
             b"/FirstChar 32 /LastChar 255 /Widths " + widths(HELVETICA_WINANSI) +
             b" /FontDescriptor << /Type /FontDescriptor /FontName /Helvetica /Flags 32 >> >>")
# No /Encoding: project ASCII fallback. Courier is monospace 600 for 32..126.
COURIER = (b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /FirstChar 32 /LastChar 126 "
           b"/Widths " + widths([600] * 95) + b" >>")


def pdf(pages, extras, media=b"[0 0 400 240]"):
    """pages: list of (resources, content); objects laid out as in pdf_builder.h."""
    count = 3 + 2 * len(pages) + len(extras)
    at = [0] * count
    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")

    def obj(n, body):
        at[n] = len(out)
        out.extend(b"%d 0 obj\n" % n + body + b"\nendobj\n")

    obj(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    kids = b" ".join(b"%d 0 R" % (3 + 2 * i) for i in range(len(pages)))
    obj(2, b"<< /Type /Pages /Kids [" + kids + b"] /Count %d /MediaBox " % len(pages) + media + b" >>")
    for i, (resources, content) in enumerate(pages):
        obj(3 + 2 * i, b"<< /Type /Page /Parent 2 0 R /Resources " + resources +
            b" /Contents %d 0 R >>" % (4 + 2 * i))
        obj(4 + 2 * i, b"<< /Length %d >>\nstream\n" % len(content) + content + b"\nendstream")
    for k, body in enumerate(extras):
        obj(3 + 2 * len(pages) + k, body)
    xref = len(out)
    out.extend(b"xref\n0 %d\n0000000000 65535 f \n" % count)
    for n in range(1, count):
        out.extend(b"%010d 00000 n \n" % at[n])
    out.extend(b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (count, xref))
    return bytes(out)


WINANSI_CONTENT = b"\n".join([
    b"BT /F1 18 Tf 1 0 0 1 24 204 Tm (Helvetica AVAWiil 0123) Tj ET",
    b"BT /F1 16 Tf 1 0 0 1 24 176 Tm <80209171922093649420962097> Tj ET",
    b"BT /F1 16 Tf 1 0 0 1 24 150 Tm (caf\\351 \\214uvre \\234uf \\225 \\205) Tj ET",
    b"BT /F1 16 Tf 1 0 0 1 24 124 Tm <7F818D8F909D> Tj ET",
    b"BT /F1 16 Tf 5 Tw 1 0 0 1 24 98 Tm (A B\\240C\\255D) Tj 0 Tw ET",
    b"BT /F1 16 Tf 1 0 0 1 24 72 Tm <410042> Tj [(W) 80 (A) -120 (V)] TJ ET",
    b"q /F2 12 Tf BT 1 0 0 1 24 44 Tm (Courier ASCII) Tj ET Q",
    # q/Q restores F1 12 after a nested F2 selection.
    b"/F1 12 Tf q /F2 12 Tf Q BT 1 0 0 1 24 20 Tm (Restored Helvetica) Tj ET",
])

# M10 TextItem transforms: user-space scaling, 90-degree rotation, invisible
# text, TJ segments, an empty string and rise.
ITEMS_CONTENT = b"\n".join([
    b"BT /F1 16 Tf 1 0 0 1 24 200 Tm (Normal 16pt) Tj ET",
    b"q 1.5 0 0 1.5 0 0 cm BT /F1 12 Tf 1 0 0 1 16 110 Tm (Scaled cm 1.5) Tj ET Q",
    b"q 0 1 -1 0 380 30 cm BT /F1 14 Tf 1 0 0 1 0 0 Tm (Rotated 90) Tj ET Q",
    b"BT /F1 14 Tf 3 Tr 1 0 0 1 24 130 Tm (Invisible Tr3) Tj 0 Tr ET",
    b"BT /F1 14 Tf 1 0 0 1 24 100 Tm [(Kern) -300 (ed) 200 (TJ)] TJ ET",
    b"BT /F1 14 Tf 1 0 0 1 24 70 Tm () Tj (After empty) Tj ET",
    b"BT /F1 12 Tf 1 0 0 1 24 40 Tm 4 Ts (Rise 4) Tj ET",
])

def helv_width(text, size):
    return sum(HELVETICA_WINANSI[b - 32] for b in text) / 1000 * size


def placed_words(words, x, y, size):
    """Show each word as its own Tj at its natural position (word gap = one
    space width), returning (x, y, word) triples for shuffling."""
    out = []
    for w in words:
        out.append((x, y, w))
        x += helv_width(w + b" ", size)
    return out


# M11 reading order: words and lines drawn out of order on two pages.
_p1 = (placed_words([b"Reading", b"order", b"test"], 24, 200, 16) +
       placed_words([b"Second", b"line", b"drawn", b"first?"], 24, 176, 12) +
       placed_words([b"Words", b"are", b"shown", b"in", b"reverse."], 24, 158, 12))
_order = [7, 11, 3, 10, 0, 6, 9, 2, 5, 8, 1, 4]
READING_P1 = b"BT /F1 16 Tf\n" + b"\n".join(
    b"/F1 %d Tf 1 0 0 1 %.3f %.3f Tm (%s) Tj" % (16 if y == 200 else 12, x, y, w)
    for x, y, w in (_p1[i] for i in _order)) + (
    b"\n1 0 0 1 24 130 Tm (Kept spaces:  two here.) Tj"
    b"\n1 0 0 1 24 106 Tm [(Kerned) -400 (T) 80 (J) -300 (segments)] TJ"
    b"\n1 0 0 1 24 82 Tm (E = mc) Tj /F1 8 Tf 5 Ts (2) Tj 0 Ts"
    b"\n/F1 12 Tf 3 Tr 1 0 0 1 24 58 Tm (Invisible OCR layer text) Tj 0 Tr"
    b"\nET")
READING_P3 = b"BT /F1 14 Tf 1 0 0 1 24 200 Tm (Page three after an empty page.) Tj ET"

FIXTURES = {
    "text-reading-order.pdf": pdf([(b"<< /Font << /F1 9 0 R >> >>", READING_P1),
                                   (b"<< /Font << /F1 9 0 R >> >>", b""),
                                   (b"<< /Font << /F1 9 0 R >> >>", READING_P3)],
                                  [HELVETICA]),
    "text-items-transform.pdf": pdf([(b"<< /Font << /F1 5 0 R >> >>", ITEMS_CONTENT)], [HELVETICA]),
    "font-winansi.pdf": pdf([(b"<< /Font << /F1 5 0 R /F2 6 0 R >> >>", WINANSI_CONTENT)],
                            [HELVETICA, COURIER]),
    # Same /F1 name, different font dictionaries on two pages.
    "font-pages.pdf": pdf([(b"<< /Font << /F1 7 0 R >> >>", b"BT /F1 20 Tf 1 0 0 1 24 120 Tm (Same name AW) Tj ET"),
                           (b"<< /Font << /F1 8 0 R >> >>", b"BT /F1 20 Tf 1 0 0 1 24 120 Tm (Same name AW) Tj ET")],
                          [HELVETICA, COURIER]),
    # Page 1 succeeds; page 2 uses a /ToUnicode font -> whole trace discarded.
    "font-later-failure.pdf": pdf([(b"<< /Font << /F1 7 0 R >> >>", b"BT /F1 12 Tf (ok) Tj ET"),
                                   (b"<< /Font << /F1 8 0 R >> >>", b"BT /F1 12 Tf (no) Tj ET")],
                                  [COURIER, COURIER[:-2] + b"/ToUnicode 9 0 R >>",
                                   b"<< /Length 0 >>\nstream\n\nendstream"]),
    "font-missing-resource.pdf": pdf([(b"<< /Font << /F1 5 0 R >> >>", b"BT /F9 12 Tf ET")], [COURIER]),
    # Tf validates the font even though only an empty string is shown.
    "font-type3-empty.pdf": pdf([(b"<< /Font << /F1 5 0 R >> >>", b"BT /F1 12 Tf () Tj ET")],
                                [b"<< /Type /Font /Subtype /Type3 /FontMatrix [0.001 0 0 0.001 0 0] >>"]),
    "font-differences.pdf": pdf([(b"<< /Font << /F1 5 0 R >> >>", b"BT /F1 12 Tf (x) Tj ET")],
                                [COURIER[:-2] + b"/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /B] >> >>"]),
}

if __name__ == "__main__":
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
    for name, data in FIXTURES.items():
        with open(os.path.join(root, name), "wb") as f:
            f.write(data)
        print(name, len(data))
