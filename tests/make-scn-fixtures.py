#!/usr/bin/env python3
"""Regenerate scn/SCN extraction fixtures and hand-authored goldens.

Run from the repository root: python3 tests/make-scn-fixtures.py
Not required by make test. Reuses the existing small-PDF fixture writer.
"""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parent
gs = runpy.run_path(str(ROOT / "make-gs-fixtures.py"))
pdf = gs["pdf"]
RES = b"<< /Font << /F1 5 0 R >> >>"
PREFIX = b"/DeviceRGB cs 0 0 0 scn /DeviceRGB CS 1 0 0 SCN "
ENGLISH = PREFIX + gs["TEXT"].replace(b"(A) Tj", b"/Pattern cs /Missing scn (A) Tj")
CHINESE = PREFIX + gs["CN_TEXT"].replace(b"[<0003>", b"/Pattern CS 0 .5 1 /P SCN [<0003>")
FIXTURES = {
    "scn-english": pdf([(RES, ENGLISH)], [gs["FONT"]]),
    "scn-chinese": pdf([(RES, CHINESE)], [gs["cn_font"], gs["cid"], gs["chinese_tools"]["stream"](gs["chinese_tools"]["CMAP"])]),
    "scn-bad-type": pdf([(RES, b"0 /P 1 scn")], [gs["FONT"]]),
    "scn-bad-arity": pdf([(RES, b"SCN")], [gs["FONT"]]),
    "scn-later-failure": pdf([(b"<< /Font << /F1 7 0 R >> >>", ENGLISH), (b"<< >>", b"BT /P 1 scn ET")], [gs["FONT"]]),
}
EXPECTED = {
    "scn-english": (0, "ABCA\n", ""),
    "scn-chinese": (0, "中文測試\n", ""),
}
for name, page, offset, reason in [
    ("scn-bad-type", 3, 7, "wrong operator operand type"),
    ("scn-bad-arity", 3, 0, "wrong operator operand count"),
    ("scn-later-failure", 5, 8, "wrong operator operand type"),
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
                       if not line.startswith("scn-"))
    manifest.write_text(previous + "".join(rows))
    print(f"Generated {len(FIXTURES)} PDFs and {len(rows)} manifest rows")
