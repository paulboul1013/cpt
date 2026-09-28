# PDF visual comparison evidence

Open `index.html` for PDF page screenshots beside actual parser output.

`capture.py` and `build_report.py` run from the repository root; they require
Poppler and Python 3 only for this optional visual-evidence workflow. Neither is
part of `make test` / `make asan`. The PDFs and fixture goldens are static.

Reproduce (the source binary path is deliberately in /tmp):

```sh
make
cc -std=c11 -Wall -Wextra -Wpedantic -iquote src output/pdf/m7-comparison/content_probe.c src/reader.c src/lexer.c src/parser.c src/object.c src/error.c src/limits.c src/xref.c src/document.c src/pages.c src/contents.c src/filter.c src/content_lexer.c src/content_interpreter.c -lz -o /tmp/cpt-visual-content-probe
python3 output/pdf/m7-comparison/capture.py
python3 output/pdf/m7-comparison/build_report.py
```

The screenshot/reference tools read only page 1. `pdftext --dump-content` reads
the entire document. The optional probe reads page 1 and prints diagnostic
operations; its output is not production CLI output or M8 geometry output.

The controlled PDF uses Courier and an explicit Widths array of 600 units per
code for codes 32..126. This is actual PDF font data, not a production fallback.
No M8 metrics provider has been implemented or executed here.

Only ASCII byte content/source order and the M7 operation summary are currently
asserted. Whitespace normalization does not verify spacing, positioning, Unicode
mapping, or general reading order. Poppler word bboxes use a top-left convention
and are not the text baseline or M8 geometry results.

`manifest.json` records source hashes, binary hash, commit, commands, exit codes,
tool versions, and the precise scope of the passing/pending checks. The book PDF
is an existing local untracked input; do not automatically add it to git.
