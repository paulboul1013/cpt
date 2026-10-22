#!/usr/bin/env python3
"""Regenerate cs/CS syntax-only fixtures and hand-authored goldens.

Run from the repository root: python3 tests/make-cs-fixtures.py
Not required by make test. Reuses the existing small-PDF fixture writer.
"""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parent
gs = runpy.run_path(str(ROOT / "make-gs-fixtures.py"))
pdf = gs["pdf"]
RES = b"<< /Font << /F1 5 0 R >> >>"
PREFIX = b"/CSp cs /DeviceRGB CS "
ENGLISH = PREFIX + gs["TEXT"].replace(b"(A) Tj", b"/CSp CS /DeviceGray cs (A) Tj")
CHINESE = PREFIX + gs["CN_TEXT"].replace(b"[<0003>", b"/CSp CS /Pattern cs [<0003>")
FIXTURES = {
    "cs-english": pdf([(RES, ENGLISH)], [gs["FONT"]]),
    "cs-chinese": pdf([(RES, CHINESE)], [gs["cn_font"], gs["cid"], gs["chinese_tools"]["stream"](gs["chinese_tools"]["CMAP"])]),
    "cs-bad-type": pdf([(RES, b"1 cs")], [gs["FONT"]]),
    "cs-bad-arity": pdf([(RES, b"/DeviceRGB /DeviceGray CS")], [gs["FONT"]]),
    "cs-later-failure": pdf([(b"<< /Font << /F1 7 0 R >> >>", ENGLISH), (b"<< >>", b"BT CS ET")], [gs["FONT"]]),
}
EXPECTED = {
    "cs-english": (0, "ABCA\n", ""),
    "cs-chinese": (0, "中文測試\n", ""),
}
for name, page, offset, reason in [
    ("cs-bad-type", 3, 2, "wrong operator operand type"),
    ("cs-bad-arity", 3, 23, "wrong operator operand count"),
    ("cs-later-failure", 5, 3, "wrong operator operand count"),
]:
    data = FIXTURES[name]
    page_at = data.index(f"{page} 0 obj\n".encode())
    EXPECTED[name] = (3, "", f"content: malformed error at byte {page_at}: decoded byte {offset}: {reason}\n")

if __name__ == "__main__":
    rows = []
    for name, data in FIXTURES.items():
        (ROOT / "fixtures" / (name + ".pdf")).write_bytes(data)
    for name, (code, stdout, stderr) in EXPECTED.items():
        for suffix, value in [("stdout", stdout), ("stderr", stderr)]:
            if value:
                (ROOT / "golden" / f"{name}.{suffix}").write_text(value, encoding="utf-8")
        kind = "malformed" if code else "success"
        out = f"tests/golden/{name}.stdout" if stdout else "-"
        err = f"tests/golden/{name}.stderr" if stderr else "-"
        rows.append(f"{name}|{kind}|text:tests/fixtures/{name}.pdf|{code}|{out}|{err}\n")
        if code:
            rows.append(f"{name}-items|{kind}|items:tests/fixtures/{name}.pdf|{code}|-|{err}\n")
    manifest = ROOT / "fixtures.tsv"
    previous = "".join(line for line in manifest.read_text().splitlines(keepends=True)
                       if not line.startswith("cs-"))
    manifest.write_text(previous + "".join(rows))
    print(f"Generated {len(FIXTURES)} PDFs and {len(rows)} manifest rows")
