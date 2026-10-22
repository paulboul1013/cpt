#!/usr/bin/env python3
"""Compare plain-text extraction on the same PDF, sequentially, with warm caches.

Run from any directory. Build isolated -O0/-O2 binaries from the current repo
sources, verify outputs, warm each tool once, then alternate timing order.
Only this report directory receives persistent files. Requires GCC and Poppler.
"""
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import tempfile
import time

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
PDF = ROOT / "output/pdf/chinese-comparison/test_git.pdf"


def main():
    data = PDF.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == "d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70"
    makefile = (ROOT / "Makefile").read_text()
    block = re.search(r"^SRC = \\\n(.*?)^HDR =", makefile, re.M | re.S).group(1)
    sources = re.findall(r"src/[\w_]+\.c", block)
    report = dict(input_sha256=digest, input_bytes=len(data), pages=479,
                  platform=platform.platform(), repetitions=7, warmups=1,
                  compiler=subprocess.check_output(["gcc", "--version"], text=True).splitlines()[0],
                  source_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
                  policy="Sequential runs, alternating order, warm filesystem cache, stdout to /dev/null; wall time includes process startup and extraction, excludes compilation.",
                  tools={})
    poppler = shutil.which("pdftotext")
    assert poppler
    report["poppler_version"] = subprocess.run([poppler, "-v"], capture_output=True, text=True).stderr.splitlines()[0]
    with tempfile.TemporaryDirectory(prefix="cpt-speed-") as tmp:
        commands = {"poppler": [poppler, str(PDF), "-"]}
        for opt in ("O0", "O2"):
            exe = Path(tmp) / f"pdftext-{opt}"
            build = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-g", "-" + opt,
                     *[str(ROOT / p) for p in sources], "-lz", "-lm", "-o", str(exe)]
            result = subprocess.run(build, capture_output=True)
            assert result.returncode == 0, result.stderr.decode()
            assert not result.stderr, result.stderr.decode()
            label = "repo_" + opt
            commands[label] = [str(exe), str(PDF)]
            report["tools"][label] = dict(build=build, samples_seconds=[])
        report["tools"]["poppler"] = dict(samples_seconds=[])
        for label, command in commands.items():
            result = subprocess.run(command, capture_output=True)
            assert result.returncode == 0 and not result.stderr, (label, result.stderr)
            report["tools"][label].update(output_bytes=len(result.stdout),
                output_sha256=hashlib.sha256(result.stdout).hexdigest())
            if label == "repo_O0":
                assert result.stdout == (OUT.parent / "actual.txt").read_bytes()
        assert report["tools"]["repo_O0"]["output_sha256"] == report["tools"]["repo_O2"]["output_sha256"]
        for command in commands.values():
            result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            assert result.returncode == 0 and not result.stderr
        labels = list(commands)
        for trial in range(7):
            for label in labels if trial % 2 == 0 else list(reversed(labels)):
                start = time.perf_counter()
                result = subprocess.run(commands[label], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                elapsed = time.perf_counter() - start
                assert result.returncode == 0 and not result.stderr, (label, result.stderr)
                report["tools"][label]["samples_seconds"].append(elapsed)
    for item in report["tools"].values():
        values = item["samples_seconds"]
        item.update(median_seconds=statistics.median(values), min_seconds=min(values),
                    max_seconds=max(values), median_pages_per_second=479 / statistics.median(values))
    poppler_seconds = report["tools"]["poppler"]["median_seconds"]
    for item in report["tools"].values():
        item["time_relative_to_poppler"] = item["median_seconds"] / poppler_seconds
    assert hashlib.sha256(PDF.read_bytes()).hexdigest() == digest
    (OUT / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({name: {k: v for k, v in item.items() if k not in ("build",)}
                      for name, item in report["tools"].items()}, indent=2))


if __name__ == "__main__":
    main()
