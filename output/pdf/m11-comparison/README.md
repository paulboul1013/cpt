# M11 plain text acceptance evidence

Open [index.html](index.html) for Poppler renders, reading-order overlays and
the actual `pdftext` stdout/stderr next to `pdftotext -raw` and `-layout`
references. [manifest.json](manifest.json) records hashes, renderer version
and every command with its exit code.

Reproduce from the repository root:

```sh
make -B test
make -B asan
python3 output/pdf/m11-comparison/capture.py
```

Requires Poppler and Pillow (evidence only, never a core test dependency).

The expected text for each successful case is written by hand in
`capture.py` from the fixture's intended reading, and compared byte for byte
with the actual stdout. Overlay numbers are output order, recomputed from
`--dump-text-items` with the M11 grouping rule; grey lines join consecutive
items. Poppler output is shown for comparison only.

Results: `text-reading-order.pdf` (shuffled draw order, two text pages around
an empty page) and `font-winansi.pdf` match exactly. The superscript in
"E = mc2" becomes its own line above the base text, the documented v1.0
limitation for rise beyond the line tolerance. Rotated text, `hello.pdf`
(`w` operator) and `compilerbook.pdf` (xref stream) exit 4 with empty stdout.
