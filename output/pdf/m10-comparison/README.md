# M10 TextItem acceptance evidence

Open [index.html](index.html) for Poppler renders beside overlays of the actual
`pdftext --dump-text-items` output, with expected/actual/delta tables.
[manifest.json](manifest.json) records hashes, renderer version, the fonts
Poppler used, and every command with its exit code.

Reproduce from the repository root:

```sh
make -B test
make -B asan
python3 output/pdf/m10-comparison/capture.py
```

Requires Poppler, `fc-match`, Pillow and fontTools; evidence only, never a
core test dependency. Old PNGs are deleted before rendering; any renderer or
reference failure stops the script.

## What is compared

Expected items are computed in `capture.py` from PDF text-space rules (its own
matrix product, not the C code) with widths from the rendering font. For each
item: origin x/y, advance vector, width, em_height, nominal and effective font
size, UTF-8 bytes, rendering mode, horizontal flag and source order. Numeric
tolerance is 1e-6 absolute (the dump prints nine decimals) plus 1e-9 relative.

Overlay boxes run from the baseline origin along the advance vector with
height `em_height`. **They are not glyph ink bounding boxes**; they show the
effective em size the project reports. Numbers are item sequence.

## Results

- `text-items-transform.pdf`: 9 items. Covers `cm` scaling (nominal 12,
  effective 18), a 90° `cm` rotation (kept, `horizontal=0`, advance `(0,w)`),
  Tr=3 invisible text (kept, pink box with no visible glyph), three TJ
  segments, an empty string (no item; source order 7 skipped) and rise.
- `font-winansi.pdf`: the first four items (ASCII, Euro/quotes/dashes,
  é/Œ/œ/bullet/ellipsis, bullet aliases) match byte-for-byte UTF-8 and geometry.
- Manual inspection: every box starts at the glyph baseline origin and ends at
  the advance end within 2 px; the rotated box follows the rotated baseline.
- `hello.pdf` (unsupported `w`) and `compilerbook.pdf` (xref stream): exit 4,
  empty stdout, real stderr saved.

Items are in source order. Reading order, synthetic spaces and plain-text
output are M11.
