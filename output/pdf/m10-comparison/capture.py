#!/usr/bin/env python3
"""Optional M10 TextItem acceptance; run from repo root after make -B test.

Requires Poppler, Pillow, fontTools and fc-match (evidence only, never core
test dependencies). Expected items are computed here from PDF text-space rules
with widths from the font file Poppler renders with, and compared with the
actual `pdftext --dump-text-items` output.
"""
import hashlib, html, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw
from fontTools.ttLib import TTFont

OUT = Path('output/pdf/m10-comparison')
DPI = 120
ABS, REL = 1e-6, 1e-9  # dump prints nine decimals


def run(name, args):
    r = subprocess.run(args, capture_output=True)
    (OUT / (name + '.stdout')).write_bytes(r.stdout)
    (OUT / (name + '.stderr')).write_bytes(r.stderr)
    return {'command': args, 'exit': r.returncode, 'stdout': name + '.stdout', 'stderr': name + '.stderr'}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def render_font(family):
    path = subprocess.run(['fc-match', family, '--format=%{file}'], capture_output=True, text=True, check=True).stdout
    f = TTFont(path)
    cmap, hmtx, upem = f.getBestCmap(), f['hmtx'].metrics, f['head'].unitsPerEm
    return path, lambda cp: hmtx[cmap[cp]][0] * 1000 / upem


helv_path, helv = render_font('Helvetica')
cour_path, cour = render_font('Courier')


def win_ansi(b):
    if b < 0x20: return 0xFFFD
    if b == 0x7F or b in (0x81, 0x8D, 0x8F, 0x90, 0x9D): return 0x2022
    return ord(bytes([b]).decode('cp1252'))


def mul(m, n):  # PDF row-vector convention: apply m, then n
    a, b, c, d, e, f = m
    A, B, C, D, E, F = n
    return [a * A + b * C, a * B + b * D, c * A + d * C, c * B + d * D, e * A + f * C + E, e * B + f * D + F]


I = [1, 0, 0, 1, 0, 0]


def item(text, size, tm, ctm=I, width=helv, decode=win_ansi, rise=0.0, mode=0, order=None):
    """Independent expectation for one shown string (hscale 1, Tc=Tw=0)."""
    raw = text if isinstance(text, bytes) else text.encode('latin-1')
    render = mul(mul([size, 0, 0, size, 0, rise], tm), ctm)
    total = sum(width(decode(b)) / 1000 * size for b in raw)
    basis = mul(tm, ctm)
    adv = [total * basis[0], total * basis[1]]
    return {'text': ''.join(chr(decode(b)) for b in raw).encode(), 'x': render[4], 'y': render[5],
            'dx': adv[0], 'dy': adv[1], 'em': math.hypot(render[2], render[3]), 'size': size,
            'mode': mode, 'horizontal': int(render[0] > 0 and render[3] > 0 and abs(render[1]) < 1e-12 and abs(render[2]) < 1e-12),
            'order': order}


def tj(parts, size, x, y):
    out, pos = [], x
    for p in parts:
        if isinstance(p, (int, float)):
            pos += -p / 1000 * size
        else:
            it = item(p, size, [1, 0, 0, 1, pos, y])
            out.append(it)
            pos += it['dx']
    return out


transform = [item('Normal 16pt', 16, [1, 0, 0, 1, 24, 200]),
             item('Scaled cm 1.5', 12, [1, 0, 0, 1, 16, 110], [1.5, 0, 0, 1.5, 0, 0]),
             item('Rotated 90', 14, I, [0, 1, -1, 0, 380, 30]),
             item('Invisible Tr3', 14, [1, 0, 0, 1, 24, 130], mode=3)]
transform += tj(['Kern', -300, 'ed', 200, 'TJ'], 14, 24, 100)
transform += [item('After empty', 14, [1, 0, 0, 1, 24, 70]), item('Rise 4', 12, [1, 0, 0, 1, 24, 40], rise=4)]
orders = [0, 1, 2, 3, 4, 5, 6, 8, 9]  # order 7 is the skipped empty string
for it, o in zip(transform, orders): it['order'] = o

winansi_rows = [('Helvetica AVAWiil 0123', 18, 204), (bytes.fromhex('80209171922093649420962097'), 16, 176),
                (b'caf\xe9 \x8cuvre \x9cuf \x95 \x85', 16, 150), (bytes.fromhex('7f818d8f909d'), 16, 124)]
winansi = [item(t, s, [1, 0, 0, 1, 24, y]) for t, s, y in winansi_rows]
for it, o in zip(winansi, range(4)): it['order'] = o

cases = [('transform', 'tests/fixtures/text-items-transform.pdf', [0, 0, 400, 240], transform),
         ('winansi', 'tests/fixtures/font-winansi.pdf', [0, 0, 400, 240], winansi),
         ('hello', 'tests/hello.pdf', None, None), ('compilerbook', 'tests/compilerbook.pdf', None, None)]
manifest = {'dpi': DPI, 'tolerance': {'absolute': ABS, 'relative': REL, 'visual_pixels': 2},
            'versions': {'pdftoppm': subprocess.run(['pdftoppm', '-v'], capture_output=True, text=True).stderr.strip()},
            'render_fonts': {'Helvetica': helv_path, 'Courier': cour_path},
            'binaries': {'pdftext': digest('pdftext')}, 'cases': []}
sections, all_pass = [], True
for name, path, box, expected in cases:
    entry = {'name': name, 'source': path, 'sha256': digest(path), 'commands': []}
    cmds = entry['commands']
    (OUT / f'{name}-page1.png').unlink(missing_ok=True)
    cmds.append(run(name + '-render', ['pdftoppm', '-f', '1', '-l', '1', '-singlefile', '-r', str(DPI), '-png', path, str(OUT / f'{name}-page1')]))
    cmds.append(run(name + '-info', ['pdfinfo', '-f', '1', '-l', '1', '-box', path]))
    cmds.append(run(name + '-reference', ['pdftotext', '-raw', '-enc', 'UTF-8', path, '-']))
    assert all(c['exit'] == 0 for c in cmds), (name, 'external reference command failed', cmds)
    cmds.append(run(name + '-items', ['./pdftext', '--dump-text-items', path]))
    out = (OUT / (name + '-items.stdout')).read_text()
    if expected is None:
        assert cmds[-1]['exit'] == 4 and out == ''
        right = f'<h3>Unsupported (exit 4)</h3><p>stdout empty; real rejection:</p><pre>{html.escape((OUT / (name + "-items.stderr")).read_text())}</pre>'
    else:
        assert cmds[-1]['exit'] == 0
        items = [json.loads(l) for l in out.splitlines() if json.loads(l)['page'] == 1]
        if name == 'winansi': items = items[:len(expected)]
        assert len(items) == len(expected), (name, len(items), len(expected))
        img = Image.open(OUT / f'{name}-page1.png').convert('RGB')
        draw = ImageDraw.Draw(img, 'RGBA')
        to_px = lambda x, y: ((x - box[0]) * DPI / 72, (box[3] - y) * DPI / 72)
        rows = []
        for i, (got, exp) in enumerate(zip(items, expected)):
            want = [exp['x'], exp['y'], exp['dx'], exp['dy'], exp['dx'], exp['em'], exp['size'], exp['em']]
            have = [got['x'], got['y'], got['advance'][0], got['advance'][1], got['width'], got['em_height'], got['font_size'], got['effective_size']]
            delta = [a - b for a, b in zip(have, want)]
            ok_num = all(abs(d) <= ABS + REL * abs(w) for d, w in zip(delta, want))
            ok_text = bytes.fromhex(got['text']) == exp['text']
            ok_flags = got['mode'] == exp['mode'] and got['horizontal'] == exp['horizontal'] and got['order'] == exp['order']
            passed = ok_num and ok_text and ok_flags
            all_pass &= passed
            rows.append({'order': got['order'], 'text': exp['text'].decode(), 'expected': want, 'actual': have,
                         'delta': delta, 'numbers_ok': ok_num, 'text_ok': ok_text, 'flags_ok': ok_flags, 'pass': passed,
                         'horizontal': got['horizontal'], 'mode': got['mode']})
            # em box: baseline direction u, perpendicular v scaled to em_height.
            length = math.hypot(got['advance'][0], got['advance'][1]) or 1
            u = (got['advance'][0] / length, got['advance'][1] / length)
            v = (-u[1] * got['em_height'], u[0] * got['em_height'])
            p0 = (got['x'], got['y']); p1 = (p0[0] + got['advance'][0], p0[1] + got['advance'][1])
            poly = [to_px(*p0), to_px(*p1), to_px(p1[0] + v[0], p1[1] + v[1]), to_px(p0[0] + v[0], p0[1] + v[1])]
            color = (204, 36, 96) if got['mode'] == 3 else (0, 95, 189)
            draw.polygon(poly, outline=color + (255,), fill=color + (28,))
            draw.line([to_px(*p0), to_px(*p1)], fill=color + (255,), width=2)
            px, py = to_px(*p0)
            draw.ellipse((px - 3, py - 3, px + 3, py + 3), fill=color + (255,))
            draw.text((px + 2, py + 3), str(got['sequence'] + 1), fill=color + (255,))
        img.save(OUT / f'{name}-overlay.png')
        entry['comparison'] = rows
        table = ('<table><tr><th>seq</th><th>order</th><th>text</th><th>expected x,y,w,em</th><th>actual</th><th>max Δ</th>'
                 '<th>horiz</th><th>mode</th><th>result</th></tr>')
        for i, r in enumerate(rows):
            table += (f'<tr><td>{i + 1}</td><td>{r["order"]}</td><td>{html.escape(r["text"])}</td>'
                      f'<td>{[round(v, 4) for v in (r["expected"][0], r["expected"][1], r["expected"][4], r["expected"][5])]}</td>'
                      f'<td>{[round(v, 4) for v in (r["actual"][0], r["actual"][1], r["actual"][4], r["actual"][5])]}</td>'
                      f'<td>{max(map(abs, r["delta"])):.2g}</td><td>{r["horizontal"]}</td><td>{r["mode"]}</td>'
                      f'<td>{"PASS" if r["pass"] else "FAIL"}</td></tr>')
        table += '</table>'
        right = (f'<h3>Project TextItems</h3><img src="{name}-overlay.png"><p>Box = baseline origin → advance, height = em_height '
                 f'(effective em size, <b>not</b> a glyph ink bbox). Pink = Tr 3 (invisible, kept). Numbers = sequence.</p>{table}')
    ref = (OUT / (name + '-reference.stdout')).read_text(errors='replace')
    sections.append(f'<section><h2>{name}</h2><p>{html.escape(path)} · SHA-256 {entry["sha256"]}</p><div class="grid"><div><h3>Original, Poppler render</h3>'
                    f'<img src="{name}-page1.png"></div><div>{right}</div></div><details><summary>pdftotext -raw (external reference only)</summary>'
                    f'<pre>{html.escape(ref)}</pre></details><details><summary>Actual --dump-text-items</summary><pre>{html.escape(out)}</pre></details></section>')
    manifest['cases'].append(entry)
manifest['numeric_pass'] = all_pass
(OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
(OUT / 'index.html').write_text(
    '<!doctype html><html lang="en"><meta charset="utf-8"><title>M10 TextItem acceptance</title><style>body{font:16px system-ui;margin:28px;color:#152434}'
    'section{border-top:1px solid #aaa;padding:20px 0}.grid{display:grid;grid-template-columns:1fr 1.3fr;gap:24px}img{max-width:100%;border:1px solid #aaa}'
    'pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:11px}table{border-collapse:collapse;font-size:12px}td,th{padding:4px;border:1px solid #aaa}</style>'
    '<h1>M10: TextItems from pdftext --dump-text-items</h1><p>Expected values computed independently from PDF text-space rules with widths from the '
    f'rendering font ({html.escape(helv_path)}). Items are in source order, not reading order (M11).</p>' + ''.join(sections) + '</html>')
assert all_pass, 'comparison failed; inspect manifest'
print('M10 TextItem comparisons passed; inspect PNGs and overlays.')
