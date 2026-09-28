#!/usr/bin/env python3
"""Optional M11 plain-text acceptance; run from repo root after make -B test.

Requires Poppler and Pillow (evidence only). Compares the actual `pdftext`
stdout with a hand-written expected reading-order text, shows Poppler's
pdftotext (-raw and -layout) as external references, and overlays the reading
order (item sequence in output order) on the real renders.
"""
import hashlib, html, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw

OUT = Path('output/pdf/m11-comparison')
DPI = 120


def run(name, args):
    r = subprocess.run(args, capture_output=True)
    (OUT / (name + '.stdout')).write_bytes(r.stdout)
    (OUT / (name + '.stderr')).write_bytes(r.stderr)
    return {'command': args, 'exit': r.returncode, 'stdout': name + '.stdout', 'stderr': name + '.stderr'}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# Written by hand from the fixture's intended reading, not from pdftext.
EXPECTED = {
    'reading': ("Reading order test\nSecond line drawn first?\nWords are shown in reverse.\n"
                "Kept spaces:  two here.\nKerned TJ segments\n2\nE = mc\nInvisible OCR layer text\n\n"
                "Page three after an empty page.\n"),
    'winansi': ("Helvetica AVAWiil 0123\n€ ‘q’ “d” – —\n"
                "café Œuvre œuf • …\n••••••\n"
                "A B C­D\nA�BWAV\nCourier ASCII\nRestored Helvetica\n"),
}
NOTES = {
    'reading': ['Words and lines are drawn in a shuffled order; output follows y then x.',
                'Rise 5 on an 8pt "2" exceeds the 2pt line tolerance, so the superscript becomes its own line above '
                '"E = mc". This is the documented v1.0 limitation (rise is not special-cased).',
                'Tr 3 text is output. Page 2 has no text and adds no blank line.'],
    'winansi': ['UTF-8 is verbatim: NBSP, soft hyphen and U+FFFD are kept; a warning goes to stderr.',
                '"ABWAV": the TJ gaps (0 and 1.92pt) are below 16 x 0.25 = 4pt, so no synthetic spaces.'],
}
cases = [('reading', 'tests/fixtures/text-reading-order.pdf', [0, 0, 400, 240], [1, 3]),
         ('winansi', 'tests/fixtures/font-winansi.pdf', [0, 0, 400, 240], [1]),
         ('rotated', 'tests/fixtures/text-items-transform.pdf', [0, 0, 400, 240], [1]),
         ('hello', 'tests/hello.pdf', None, [1]), ('compilerbook', 'tests/compilerbook.pdf', None, [1])]


def reading_sequence(items):
    """Output order of item sequences, computed from items with the M11 rule."""
    order = []
    for page in sorted({i['page'] for i in items}):
        rows = sorted((i for i in items if i['page'] == page), key=lambda i: (-i['y'], i['sequence']))
        lines, cur = [], []
        for it in rows:
            if cur:
                a = cur[0]
                if a['y'] - it['y'] <= max(1.5, min(a['effective_size'], it['effective_size']) * 0.25):
                    cur.append(it); continue
                lines.append(cur)
            cur = [it]
        if cur: lines.append(cur)
        for ln in lines:
            order += sorted(ln, key=lambda i: (i['x'], i['sequence']))
    return order


manifest = {'dpi': DPI, 'versions': {'pdftoppm': subprocess.run(['pdftoppm', '-v'], capture_output=True, text=True).stderr.strip()},
            'binaries': {'pdftext': digest('pdftext')}, 'cases': []}
sections, all_pass = [], True
for name, path, box, pages in cases:
    entry = {'name': name, 'source': path, 'sha256': digest(path), 'commands': []}
    cmds = entry['commands']
    for p in pages:
        (OUT / f'{name}-page{p}.png').unlink(missing_ok=True)
        cmds.append(run(f'{name}-render{p}', ['pdftoppm', '-f', str(p), '-l', str(p), '-singlefile', '-r', str(DPI), '-png', path, str(OUT / f'{name}-page{p}')]))
    cmds.append(run(name + '-raw', ['pdftotext', '-raw', '-enc', 'UTF-8', path, '-']))
    cmds.append(run(name + '-layout', ['pdftotext', '-layout', '-enc', 'UTF-8', path, '-']))
    assert all(c['exit'] == 0 for c in cmds), (name, 'external command failed', cmds)
    cmds.append(run(name + '-text', ['./pdftext', path]))
    cmds.append(run(name + '-items', ['./pdftext', '--dump-text-items', path]))
    text = (OUT / (name + '-text.stdout')).read_bytes().decode('utf-8')
    err = (OUT / (name + '-text.stderr')).read_text()
    status = cmds[-2]['exit']
    if name in EXPECTED:
        passed = status == 0 and text == EXPECTED[name]
        entry['expected_match'] = passed
        all_pass &= passed
        items = [json.loads(l) for l in (OUT / (name + '-items.stdout')).read_text().splitlines()]
        seq = reading_sequence(items)
        rank = {it['sequence']: k + 1 for k, it in enumerate(seq)}
        imgs = ''
        for p in pages:
            img = Image.open(OUT / f'{name}-page{p}.png').convert('RGB')
            draw = ImageDraw.Draw(img, 'RGBA')
            to_px = lambda x, y: ((x - box[0]) * DPI / 72, (box[3] - y) * DPI / 72)
            prev = None
            for it in seq:
                if it['page'] != p: continue
                x0, y0 = to_px(it['x'], it['y'])
                x1, y1 = to_px(it['x'] + it['width'], it['y'] + it['em_height'])
                color = (204, 36, 96) if it['mode'] == 3 else (0, 95, 189)
                draw.rectangle((x0, y1, x1, y0), outline=color + (255,), fill=color + (24,))
                if prev: draw.line((prev[0], prev[1], x0, y0), fill=(120, 120, 120, 150), width=1)
                draw.text((x0 + 1, y0 + 2), str(rank[it['sequence']]), fill=color + (255,))
                prev = (x1, y0)
            img.save(OUT / f'{name}-order{p}.png')
            imgs += f'<p>Page {p}</p><img src="{name}-order{p}.png">'
        result = 'PASS' if passed else 'FAIL'
        notes = ''.join(f'<li>{html.escape(n)}</li>' for n in NOTES[name])
        right = (f'<h3>Reading order overlay</h3>{imgs}<p>Numbers are output order; grey lines join consecutive items.</p>'
                 f'<h3>pdftext stdout (exit {status}) — expected match: <b>{result}</b></h3><pre>{html.escape(text)}</pre>'
                 f'<h3>stderr</h3><pre>{html.escape(err) or "(empty)"}</pre><ul>{notes}</ul>')
    else:
        passed = status == 4 and text == ''
        all_pass &= passed
        right = f'<h3>Unsupported (exit {status})</h3><p>stdout empty: {"PASS" if text == "" else "FAIL"}</p><pre>{html.escape(err)}</pre>'
    raw = (OUT / (name + '-raw.stdout')).read_text(errors='replace')
    layout = (OUT / (name + '-layout.stdout')).read_text(errors='replace')
    originals = ''.join(f'<p>Page {p}</p><img src="{name}-page{p}.png">' for p in pages)
    sections.append(f'<section><h2>{name}</h2><p>{html.escape(path)} · SHA-256 {entry["sha256"]}</p><div class="grid">'
                    f'<div><h3>Original, Poppler render</h3>{originals}</div><div>{right}</div></div>'
                    f'<div class="grid"><div><h3>pdftotext -raw (reference: content order)</h3><pre>{html.escape(raw)}</pre></div>'
                    f'<div><h3>pdftotext -layout (reference)</h3><pre>{html.escape(layout)}</pre></div></div></section>')
    manifest['cases'].append(entry)
manifest['pass'] = all_pass
(OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
(OUT / 'index.html').write_text(
    '<!doctype html><html lang="en"><meta charset="utf-8"><title>M11 plain text acceptance</title><style>body{font:16px system-ui;margin:28px;color:#152434}'
    'section{border-top:1px solid #aaa;padding:20px 0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:24px}img{max-width:100%;border:1px solid #aaa}'
    'pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;background:#f4f5f2;padding:8px}</style>'
    '<h1>M11: pdftext plain text in reading order</h1><p>Expected text is hand-written from each fixture. Poppler output is reference only.</p>'
    + ''.join(sections) + '</html>')
assert all_pass, 'comparison failed; inspect manifest'
print('M11 plain text comparisons passed; inspect PNGs and overlays.')
