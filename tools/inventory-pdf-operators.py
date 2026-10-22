#!/usr/bin/env python3
"""Inventory operators in page Contents with the repo's actual M7 parser.

Run: python3 tools/inventory-pdf-operators.py INPUT.pdf --out-dir OUTPUT
Compiles its diagnostic helper in a temporary directory. Production code and
input PDFs are read only. No third-party Python modules are required.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def build(directory):
    makefile = (ROOT / "Makefile").read_text()
    block = re.search(r"^SRC = \\\n(.*?)^HDR =", makefile, re.M | re.S).group(1)
    sources = re.findall(r"src/[\w_]+\.c", block)
    sources = [s for s in sources if s not in ("src/main.c", "src/content_interpreter.c")]
    executable = directory / "inventory"
    command = [*shlex.split(os.environ.get("CC", "gcc")), "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-iquote", str(ROOT / "src"),
               str(ROOT / "tools/pdf_operator_inventory.c"),
               *(str(ROOT / s) for s in sources), "-lz", "-lm", "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return executable


def self_test(executable, directory):
    writer = runpy.run_path(str(ROOT / "tests/make-font-fixtures.py"))
    # Operator-looking text in names, strings and nested operands must not count.
    content = (b"/F1 12 Tf /m cs /Span << /ActualText (Do) /Nested [/scn true null] >> BDC "
               b"BT (Do scn m) Tj [(q Q) -12 (Do)] TJ ET EMC q Q 0 0 m")
    path = directory / "test.pdf"
    path.write_bytes(writer["pdf"]([(b"<< /Font << /F1 5 0 R >> >>", content)], [writer["COURIER"]]))
    result = subprocess.run([str(executable), str(path)], capture_output=True, text=True, check=True)
    counts = Counter(bytes.fromhex(row.split("\t")[3]).decode() for row in result.stdout.splitlines() if row.startswith("O\t"))
    expected = Counter({name: 1 for name in ["Tf", "cs", "BDC", "BT", "Tj", "TJ", "ET", "EMC", "q", "Q", "m"]})
    assert counts == expected, (counts, expected)
    path.write_bytes(writer["pdf"]([(b"<< >>", b"BI /W 1 /H 1 ID raw EI")], []))
    result = subprocess.run([str(executable), str(path)], capture_output=True, text=True)
    assert result.returncode and "inventory is incomplete" in result.stderr


def collect(executable, source, output):
    operators = {}
    pages = []
    xobjects = {}
    bdc_keys = Counter()
    gs_codes = Counter()
    gs_errors = []
    scanned_pages = set()
    with gzip.open(output / "operators.tsv.gz", "wt", encoding="utf-8") as raw:
        proc = subprocess.Popen([str(executable), str(source)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for line in proc.stdout:
            raw.write(line)
            row = line.rstrip("\n").split("\t")
            if row[0] == "D":
                document_pages = int(row[1])
            elif row[0] == "P":
                pages.append(dict(page=int(row[1]),file_offset=int(row[2]),decoded_bytes=int(row[3])))
                scanned_pages.add(int(row[1]))
            elif row[0] == "O":
                page, offset = int(row[1]), int(row[2])
                name = bytes.fromhex(row[3]).decode("latin1")
                item = operators.setdefault(name, dict(count=0,pages=set(),status_counts=Counter(),first=dict(page=page,decoded_offset=offset)))
                item["count"] += 1
                item["pages"].add(page)
                item["status_counts"][row[4]] += 1
            elif row[0] == "X":
                subtype = bytes.fromhex(row[4]).decode("latin1") or "unresolved"
                item = xobjects.setdefault(subtype, dict(invocations=0,objects=set(),pages=set(),resolver_errors=0))
                item["invocations"] += 1
                item["objects"].add(int(row[6]))
                item["pages"].add(int(row[1]))
                item["resolver_errors"] += int(row[5]) != 0
            elif row[0] == "B":
                bdc_keys.update(bytes.fromhex(key).decode("latin1") for key in row[3].split(",") if key)
            elif row[0] == "G":
                gs_codes[int(row[4])] += 1
                if int(row[4]) and len(gs_errors) < 20:
                    gs_errors.append(dict(page=int(row[1]),decoded_offset=int(row[2]),name_hex=row[3],code=int(row[4]),object_offset=int(row[5]),message=bytes.fromhex(row[6]).decode()))
        stderr = proc.stderr.read()
        status = proc.wait()
    (output / "scan.stderr").write_text(stderr)
    for item in operators.values():
        item["pages"] = sorted(item["pages"])
        item["page_count"] = len(item["pages"])
        item["status_counts"] = dict(item["status_counts"])
    for item in xobjects.values():
        item["pages"] = sorted(item["pages"])
        item["objects"] = sorted(item["objects"])
    return dict(page_count=document_pages,scanned_pages=len(scanned_pages),page_contents_scan_complete=status==0,
                scanner_exit_code=status,scanner_error=stderr,decoded_bytes=sum(p["decoded_bytes"] for p in pages),
                total_operations=sum(o["count"] for o in operators.values()),operators=operators,pages=pages,
                xobjects=xobjects,bdc_property_keys=dict(bdc_keys),gs_validation_codes=dict(gs_codes),gs_errors=gs_errors,
                status_legend=dict(S="M7 accepted",U="repo rejects as unsupported",M="repo rejects as malformed",I="unknown operator ignored by repo",E="other dispatch error"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    source = args.input.resolve()
    output = args.out_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="cpt-operator-inventory-") as tmp:
        directory = Path(tmp)
        executable = build(directory)
        self_test(executable, directory)
        report = collect(executable, source, output)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    report.update(input=str(source),input_bytes=source.stat().st_size,sha256=digest,self_tests_passed=True,
                  support_source_sha256=hashlib.sha256((ROOT / "src/content_interpreter.c").read_bytes()).hexdigest())
    (output / "result.json").write_text(json.dumps(report, ensure_ascii=True, indent=2)+"\n")
    rows = ["operator\tcount\tpage_count\tstatus_counts\tfirst_page\tfirst_decoded_offset\n"]
    for name,item in sorted(report["operators"].items(),key=lambda pair: (pair[1]["first"]["page"],pair[1]["first"]["decoded_offset"])):
        rows.append(f"{name}\t{item['count']}\t{item['page_count']}\t{json.dumps(item['status_counts'],sort_keys=True)}\t{item['first']['page']}\t{item['first']['decoded_offset']}\n")
    (output / "summary.tsv").write_text("".join(rows))
    overview = {key:report[key] for key in ["page_count","scanned_pages","page_contents_scan_complete","total_operations","decoded_bytes","bdc_property_keys","gs_validation_codes","scanner_error"]}
    overview["xobjects"] = {name:dict(invocations=item["invocations"],objects=len(item["objects"]),pages=len(item["pages"]),resolver_errors=item["resolver_errors"]) for name,item in report["xobjects"].items()}
    print(json.dumps(overview,indent=2))
    print("".join(rows))
    if not report["page_contents_scan_complete"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
