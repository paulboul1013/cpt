#!/usr/bin/env python3
"""Regenerate committed Chinese PDF fixtures; not required by make test.

Run from the repository root: python3 tests/make-chinese-fixtures.py
Uses the existing traditional-xref fixture writer, never an installed PDF SDK.
"""
from pathlib import Path
import runpy
import zlib

ROOT = Path(__file__).resolve().parent
pdf = runpy.run_path(str(ROOT / "make-font-fixtures.py"))["pdf"]

CMAP = b"""/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def
/CMapName /Chinese-UCS def
/CMapType 2 def
1 begincodespacerange
<0000> <FFFF>
endcodespacerange
6 beginbfchar
<0001> <4E2D>
<0002> <6587>
<0005> <4F60597D>
<0006> <D840DC00>
<0020> <0020>
<2020> <4EBA>
endbfchar
2 beginbfrange
<0003> <0004> [<6E2C> <8A66>]
<0100> <0101> <0041>
endbfrange
endcmap
CMapName currentdict /CMap defineresource pop
end end
"""


def stream(data, compressed=False):
    data = zlib.compress(data) if compressed else data
    return (b"<< /Length %d " % len(data) +
            (b"/Filter /FlateDecode " if compressed else b"") +
            b">>\nstream\n" + data + b"\nendstream")


def chinese(content, cmap=CMAP, compressed=False, encoding=b"Identity-H",
            descendant=b"CIDFontType2", widths=b"/DW 1000 /W [1 [1000 900 800] 32 32 250]"):
    font = (b"<< /Type /Font /Subtype /Type0 /BaseFont /Chinese /Encoding /" +
            encoding + b" /DescendantFonts [6 0 R] /ToUnicode 7 0 R >>")
    cid = (b"<< /Type /Font /Subtype /" + descendant +
           b" /BaseFont /Chinese /CIDSystemInfo << /Registry (Adobe) "
           b"/Ordering (Identity) /Supplement 0 >> " + widths + b" >>")
    return pdf([(b"<< /Font << /F1 5 0 R >> >>", content)],
               [font, cid, stream(cmap, compressed)])


TEXT = b"BT /F1 12 Tf 1 0 0 1 24 200 Tm <0001000200030004> Tj ET"
FIXTURES = {
    "chinese-raw.pdf": chinese(TEXT),
    "chinese-flate.pdf": chinese(TEXT, compressed=True),
    "chinese-cff.pdf": chinese(TEXT, descendant=b"CIDFontType0"),
    "chinese-unicode.pdf": chinese(b"BT /F1 12 Tf <0005000601000101FFFF> Tj ET"),
    "chinese-spacing.pdf": chinese(
        b"BT /F1 12 Tf 1 Tc 99 Tw 80 Tz 1 0 0 1 24 200 Tm "
        b"[<00010002> -500 <00030004>] TJ <00202020> Tj ET"),
    "chinese-odd.pdf": chinese(b"BT /F1 12 Tf <000100> Tj ET"),
    "chinese-bad-surrogate.pdf": chinese(TEXT, cmap=CMAP.replace(b"D840DC00", b"D8400041")),
    "chinese-bad-range.pdf": chinese(TEXT, cmap=CMAP.replace(b"[<6E2C> <8A66>]", b"[<6E2C>]")),
    "chinese-vertical.pdf": chinese(TEXT, encoding=b"Identity-V"),
    "chinese-bad-widths.pdf": chinese(TEXT, widths=b"/W [65535 [1000 900]]"),
}

# Hand-authored expectations; generation never runs pdftext to infer answers.
EXPECTED = {
    "chinese-raw": (0, "中文測試\n", ""),
    "chinese-flate": (0, "中文測試\n", ""),
    "chinese-cff": (0, "中文測試\n", ""),
    "chinese-unicode": (0, "你好𠀀AB�\n", "pdftext: warning: 1 undecodable characters replaced with U+FFFD\n"),
    "chinese-spacing": (0, "中文 測試 人\n", ""),
    "chinese-odd": (3, "", "content: malformed error at byte 145: decoded byte 22: font byte 324 (font): truncated font character code\n"),
    "chinese-bad-surrogate": (3, "", "content: malformed error at byte 145: decoded byte 10: font byte 674 (cmap): decoded byte 298: unpaired UTF-16 surrogate\n"),
    "chinese-bad-range": (3, "", "content: malformed error at byte 145: decoded byte 10: font byte 674 (cmap): decoded byte 383: CMap destination must be a hex string\n"),
    "chinese-vertical": (4, "", "content: unsupported error at byte 145: decoded byte 10: font byte 352 (font): Type0 font requires supported /Identity-H encoding\n"),
    "chinese-bad-widths": (3, "", "content: malformed error at byte 145: decoded byte 10: font byte 484 (font): invalid CID width array length\n"),
}

if __name__ == "__main__":
    for name, data in FIXTURES.items():
        (ROOT / "fixtures" / name).write_bytes(data)
        print(name, len(data))
    rows = []
    for name, (code, stdout, stderr) in EXPECTED.items():
        for suffix, value in [("stdout", stdout), ("stderr", stderr)]:
            if value:
                (ROOT / "golden" / f"{name}.{suffix}").write_text(value, encoding="utf-8")
        kind = {0: "success", 3: "malformed", 4: "unsupported"}[code]
        out = f"tests/golden/{name}.stdout" if stdout else "-"
        err = f"tests/golden/{name}.stderr" if stderr else "-"
        rows.append(f"{name}|{kind}|text:tests/fixtures/{name}.pdf|{code}|{out}|{err}\n")
    manifest = ROOT / "fixtures.tsv"
    previous = "".join(line for line in manifest.read_text().splitlines(keepends=True)
                       if not line.startswith("chinese-"))
    manifest.write_text(previous + "".join(rows))
