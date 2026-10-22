#!/usr/bin/env python3
"""Verify all operators and extracted text in the unchanged test_git.pdf.

Requires a built pdftext, C compiler, Python 3 and Poppler. This optional real
document check is separate from the dependency-free core make test gate.
"""
import argparse
import base64
from collections import Counter, defaultdict
import gzip
import hashlib
import html
import json
import math
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
SHA256 = "d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70"
SAMPLES = (3, 22, 479)


def compact(text):
    return "".join(text.split())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path,
                        default=ROOT / "output/pdf/chinese-comparison/test_git.pdf")
    parser.add_argument("--binary", type=Path, default=ROOT / "pdftext")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "output/pdf/test-git-support")
    parser.add_argument("--render", action="store_true", help="render representative pages as PNG")
    args = parser.parse_args()
    source, binary, out = args.input.resolve(), args.binary.resolve(), args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == SHA256, "unexpected input PDF"
    results = {}

    def run(name, command):
        start = time.monotonic()
        with (out / f"{name}.stdout").open("wb") as stdout, (out / f"{name}.stderr").open("wb") as stderr:
            result = subprocess.run([str(c) for c in command], stdout=stdout, stderr=stderr)
        results[name] = dict(command=[str(c) for c in command], exit_code=result.returncode,
                             seconds=round(time.monotonic() - start, 3))
        assert result.returncode == 0, f"{name} failed; see {out / (name + '.stderr')}"
        assert not (out / f"{name}.stderr").stat().st_size, f"{name} emitted diagnostics"
        return (out / f"{name}.stdout").read_bytes()

    run("inventory", [sys.executable, ROOT / "tools/inventory-pdf-operators.py", source,
                       "--out-dir", out / "inventory"])
    inventory = json.loads((out / "inventory/result.json").read_text())
    assert inventory["page_count"] == inventory["scanned_pages"] == 479
    assert inventory["page_contents_scan_complete"] and inventory["total_operations"] == 1037334
    assert len(inventory["operators"]) == 33
    assert all(item["status_counts"] == {"S": item["count"]} for item in inventory["operators"].values())
    content = run("content", [binary, "--dump-content", source]).decode()
    assert len(content.splitlines()) == 480 and content.startswith("PAGES 479\n")
    assert sum(int(line.split()[3]) for line in content.splitlines()[1:]) == 1037334
    actual = run("extract", [binary, source]).decode("utf-8")
    (out / "actual.txt").write_text(actual, encoding="utf-8")
    reference = run("reference", ["pdftotext", source, "-"]).decode("utf-8")
    (out / "reference.txt").write_text(reference, encoding="utf-8")

    # Read the real JSONL output, validate every item and retain a compressed
    # trace. No large raw diagnostic file or third-party Python dependency.
    start = time.monotonic()
    per_page = defaultdict(Counter)
    counts = Counter()
    sample_items = {page: [] for page in SAMPLES}
    with (out / "items.stderr").open("wb") as stderr, (out / "items.jsonl.gz").open("wb") as raw:
        proc = subprocess.Popen([str(binary), "--dump-text-items", str(source)], stdout=subprocess.PIPE, stderr=stderr)
        with gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as trace:
            for line in proc.stdout:
                trace.write(line)
                item = json.loads(line)
                text = bytes.fromhex(item["text"]).decode("utf-8")
                assert item["text_len"] == len(text.encode()) and item["horizontal"] == 1
                assert item["replacements"] == 0 and 1 <= item["page"] <= 479
                assert all(math.isfinite(item[key]) for key in ("x", "y", "width", "em_height"))
                per_page[item["page"]].update(compact(text))
                counts["items"] += 1
                counts["utf8_bytes"] += item["text_len"]
                if item["page"] in sample_items:
                    sample_items[item["page"]].append(item)
        code = proc.wait()
    assert code == 0 and not (out / "items.stderr").stat().st_size
    results["items"] = dict(command=[str(binary), "--dump-text-items", str(source)],
                            exit_code=code, seconds=round(time.monotonic() - start, 3))
    for page, items in sample_items.items():
        (out / f"page-{page}-items.jsonl").write_text(
            "".join(json.dumps(item, ensure_ascii=True) + "\n" for item in items))
    references = reference.split("\f")
    assert len(references) == 480 and not references[-1].strip()
    differences = []
    for page, text in enumerate(references[:-1], 1):
        ref_counts = Counter(compact(text))
        source_counts = per_page.get(page, Counter())
        extra, missing = source_counts - ref_counts, ref_counts - source_counts
        if extra or missing:
            differences.append(dict(page=page, extra=dict(extra), missing=dict(missing)))
    # Poppler suppresses one end-of-line hyphen. Layout/read-order whitespace
    # can differ; counters explicitly do not assert identical reading order.
    assert Counter(compact(actual)) - Counter(compact(reference)) == Counter({"-": 1})
    assert not (Counter(compact(reference)) - Counter(compact(actual)))
    assert len(differences) == 1 and differences[0]["extra"] == {"-": 1} and not differences[0]["missing"]
    assert "</html>#---#Toremove" in compact(actual)
    assert "</html>#--#Toremove" in compact(references[160])
    assert len(per_page) == 396 and counts["items"] > 100000
    if args.render:
        for page in SAMPLES:
            run(f"render-{page}", ["pdftoppm", "-f", page, "-l", page, "-singlefile", "-r", "90",
                                   "-png", source, out / f"page-{page}"])
            # The pinned source uses [0 0 595 842], Rotate 0 on these pages.
            # Overlay real baseline origins and advance vectors, not glyph boxes.
            png = base64.b64encode((out / f"page-{page}.png").read_bytes()).decode()
            svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 595 842">',
                   f'<image width="595" height="842" href="data:image/png;base64,{png}"/>']
            for item in sample_items[page]:
                x, y = item["x"], 842 - item["y"]
                dx, dy = item["advance"]
                title = html.escape(f'order={item["order"]} offset={item["offset"]} ' + item["preview"])
                svg.append(f'<g><title>{title}</title><circle cx="{x}" cy="{y}" r=".7" fill="#d73027"/>'
                           f'<path d="M{x} {y} l{dx} {-dy}" stroke="#008a64" stroke-width=".45" opacity=".65"/></g>')
            (out / f"page-{page}-overlay.svg").write_text("\n".join(svg) + "\n</svg>\n")
    assert hashlib.sha256(source.read_bytes()).hexdigest() == SHA256
    report = dict(sha256=SHA256, pages=479, operator_kinds=33, operations=1037334,
                  text_pages=len(per_page), counts=dict(counts), actual_bytes=len(actual.encode()),
                  reference_bytes=len(reference.encode()), replacements=0,
                  reference_character_differences=differences,
                  normalized_reading_order_equal=compact(actual) == compact(reference),
                  clipping_policy="extract source text without filtering by clipping paths",
                  image_policy="validate Image XObject metadata; no pixel decoding or OCR",
                  poppler_version=subprocess.run(["pdftotext", "-v"], capture_output=True, text=True).stderr.splitlines()[0],
                  source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in
                      ("src/content_interpreter.c", "src/text_state.c", "src/font_text.c", "src/xobject.c")},
                  runs=results)
    (out / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({key: value for key, value in report.items() if key not in ("runs", "source_sha256")},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
