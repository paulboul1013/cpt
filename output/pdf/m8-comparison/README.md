# M8 geometry acceptance evidence

Open [index.html](index.html) for actual PDF renders beside project geometry
origin/advance overlays, numeric expected/actual/delta comparisons and raw traces.

Reproduce from the repository root:

```sh
make -B test
make -B asan
python3 output/pdf/m8-comparison/capture.py
```

`capture.py` requires Poppler (`pdftoppm`, `pdfinfo`, `pdftotext`) and Python Pillow.
These are only for visual evidence, never core test dependencies. Core fixtures
and JSON-line goldens are static files. `tests/geometry-test input.pdf` stages the
entire document and emits the library's diagnostic trace only after success.
It uses an explicit **test-only Courier-600 adapter** for F1 codes 32–126, matching
the fixture's actual `/Widths`. It is not a production font lookup/fallback.

The trace includes raw string/font hex, text state, before/after/line/CTM and
rendering matrices, default user-space origin and advance vector, decoded offset,
TJ array index and per-page source order. Page ordinal is supplied by the consumer.
The library formatter uses a thread-local C numeric locale and nine decimal places.
Trace precision is for diagnostics; C tests compare the underlying doubles.

`manifest.json` records source and binary SHA-256 hashes, renderer version, all
commands/exit codes, pdfinfo page properties and actual PNG dimensions. Original
images are Poppler renders at 120 DPI, page 1. Overlays are separate annotated
images; the original images are retained. The two controlled PDFs use Rotate=0,
CropBox=MediaBox, UserUnit=1. Their mapping is `px=(x-x0)*120/72`,
`py=(y1-y)*120/72`. No such simplified transform is assumed for the other PDFs.

The independent expected table in `capture.py` covers single/multiple lines,
Td relative to the line matrix after showing text, both signs of TJ adjustment,
Tc/Tw/Tz/Ts, two noncommuting cm orders, q/Q restoration and Tr=3. It compares raw
ASCII bytes, origin, advance, rendering matrix and rendering mode against actual
project execution, at absolute 1e-8 + relative 1e-9 tolerance. All 16 segments pass.
The original M7 fixture's second string has 17 bytes, hence advance 163.2 at
600/1000 × 16 per code; the hand-written initial expectation was corrected after
checking the raw bytes. No parser calculation was changed to fit that table.

Manual inspection: both controlled PDF originals and overlays align in position,
baseline, spacing and direction (2 px visual tolerance). The pink Tr=3 event has
no visible glyph in the original, as expected. Advance endpoints are not glyph
ink bounds. Poppler word boxes in `*-reference.stdout` are external reference,
not project output and not baseline coordinates.

`hello.pdf` still fails at `w` in content; `compilerbook.pdf` fails at xref streams.
Both have empty stdout and exit 4 in CLI and geometry probe, with real screenshots
and stderr saved. Unicode decoding, TextItem, reading order, glyph ink bbox and
general graphics compatibility remain outside M8.
