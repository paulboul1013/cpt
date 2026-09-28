# M9 font width and Unicode acceptance evidence

Open [index.html](index.html) for actual Poppler renders beside project
origin/advance overlays, per-string raw hex, project UTF-8, width provenance and
expected/actual/delta tables. [manifest.json](manifest.json) records source and
binary SHA-256, renderer version, the font files Poppler actually used, every
command with exit code, and page attributes.

Reproduce from the repository root:

```sh
make -B test
make -B asan
python3 output/pdf/m9-comparison/capture.py
```

`capture.py` needs Poppler (`pdftoppm`, `pdfinfo`, `pdftotext`), `fc-match`,
Pillow and fontTools. These are evidence tools only, never core test
dependencies. Any renderer/reference failure stops the script; old PNGs are
deleted before each render, never reused.

## What is compared

- **Project output**: `tests/font-text-test input.pdf` stages the whole
  document through M6 → M7 → M8 with the **real M9 font adapter** (`src/font.c`,
  `src/font_text.c`) and prints two JSON lines per string (raw geometry, then
  decode) only after every page succeeds.
- **Independent widths**: taken with fontTools from the font files Poppler
  renders with (`fc-match Helvetica` → NimbusSans-Regular, `fc-match Courier` →
  NimbusMonoPS-Regular), not from the fixture's `/Widths`. All 224 Helvetica
  WinAnsi codes 32–255 in `tests/make-font-fixtures.py` equal the rendering
  font's advances; except Euro they also equal Adobe's Helvetica AFM
  (`phvr8a.afm` 001.006, which predates the Euro).
- **Independent Unicode**: Python's cp1252 codec plus the PDF WinAnsi policy
  (unused 0x7F/0x81/0x8D/0x8F/0x90/0x9D → U+2022, 0x00–0x1F → U+FFFD). UTF-8 is
  compared byte for byte, including NBSP, soft hyphen and replacement.
- **External reference only**: `*-reference.stdout` (`pdftotext -raw`). Poppler
  applies its own whitespace/Unicode policy and is never copied into project
  columns.

Tolerance is absolute 1e-8 + relative 1e-9 for origin, advance and rendering
matrix; 2 px for manual overlay inspection. Advance endpoints are not glyph ink
bounds. Traces are in source order, not reading order (M11).

## Results

- `font-winansi.pdf`: 11 strings with real Helvetica widths (non-monospace,
  many distinct values) plus a Courier ASCII-fallback string restored via q/Q.
  Covers Euro, smart quotes, en/em dash, é, Œ/œ, bullet and its five aliases,
  ellipsis, NBSP (no Tw) versus raw 0x20 (Tw 5), soft hyphen, a raw NUL
  (U+FFFD, width 0 via `descriptor-default-zero`) and TJ kerning. Bytes,
  Unicode and geometry all pass.
- `font-pages.pdf`: the same `/F1` name maps to Helvetica on page 1 and
  Courier on page 2; each page uses its own widths and encoding policy.
- `geometry-raw.pdf`: raw geometry lines through the real adapter are
  identical to the M8 test-adapter golden (`tests/golden/geometry.stdout`).
- Manual inspection: originals and overlays align in baseline, spacing and
  direction for every string (within 2 px).
- `hello.pdf` still stops at the unsupported `w` operator and
  `compilerbook.pdf` at xref streams: exit 4, empty stdout, real stderr saved.
  M9 did not turn either into a false success.
