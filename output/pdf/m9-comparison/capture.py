#!/usr/bin/env python3
"""Optional M9 visual/Unicode acceptance; run from repo root after make -B test.

Requires Poppler (pdftoppm, pdfinfo, pdftotext), Pillow, fontTools and fc-match.
Never a core test dependency. Expected widths come from the font files Poppler
actually renders with (fc-match), not from the fixture's /Widths; expected
Unicode comes from Python's cp1252 codec plus the documented PDF WinAnsi rules.
"""
import hashlib, html, json, subprocess
from pathlib import Path
from PIL import Image, ImageDraw
from fontTools.ttLib import TTFont

OUT = Path('output/pdf/m9-comparison')
DPI = 120
ABS, REL = 1e-8, 1e-9
AFM = Path('/mnt/c/Users/USER/AppData/Local/Programs/Python/Python311/Lib/site-packages/'
           'matplotlib/mpl-data/fonts/afm/phvr8a.afm')


def run(name, args):
    r = subprocess.run(args, capture_output=True)
    (OUT / (name + '.stdout')).write_bytes(r.stdout)
    (OUT / (name + '.stderr')).write_bytes(r.stderr)
    return {'command': args, 'exit': r.returncode, 'stdout': name + '.stdout', 'stderr': name + '.stderr'}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def render_font(family):
    path = subprocess.run(['fc-match', family, '--format=%{file}'], capture_output=True, text=True, check=True).stdout
    font = TTFont(path)
    cmap, hmtx, upem = font.getBestCmap(), font['hmtx'].metrics, font['head'].unitsPerEm
    return path, lambda cp: hmtx[cmap[cp]][0] * 1000 / upem if cp in cmap else None


def win_ansi(b):
    """Project policy (PDF Reference Appendix D), independent of src/font.c."""
    if b < 0x20: return 0xFFFD
    if b == 0x7F or b in (0x81, 0x8D, 0x8F, 0x90, 0x9D): return 0x2022
    return ord(bytes([b]).decode('cp1252'))


def ascii_fallback(b):
    return b if 0x20 <= b <= 0x7E else 0xFFFD


helv_path, helv = render_font('Helvetica')
cour_path, cour = render_font('Courier')


def helv_width(b):
    cp = win_ansi(b)
    if cp == 0xFFFD: return 0.0  # code outside /Widths: descriptor default zero
    return helv(cp)


def cour_width(b):
    return cour(b)


# Fixture /Widths must equal the widths of the font that is actually rendered.
fixture_src = Path('tests/make-font-fixtures.py').read_text()
ns = {}
exec(compile(fixture_src.split('def widths')[0], 'fixtures', 'exec'), ns)
width_check = []
for i, w in enumerate(ns['HELVETICA_WINANSI']):
    b = 32 + i
    width_check.append({'code': b, 'fixture': w, 'render_font': helv_width(b)})
width_mismatch = [r for r in width_check if r['fixture'] != r['render_font']]
afm = {}
if AFM.exists():
    for line in AFM.read_text(encoding='latin-1').splitlines():
        if line.startswith('C '):
            fields = dict(p.strip().split(' ', 1) for p in line.split(';') if p.strip())
            afm[fields['N']] = int(fields['WX'])

# Independent geometry script for the controlled fixtures:
# (raw bytes, x, y, size, Tw, width fn, decode fn), in source order.
def line(raw, x, y, size, width, decode, tw=0.0):
    return [raw, x, y, size, tw, width, decode]


winansi = [
    line(b'Helvetica AVAWiil 0123', 24, 204, 18, helv_width, win_ansi),
    line(bytes.fromhex('80209171922093649420962097'), 24, 176, 16, helv_width, win_ansi),
    line(b'caf\xe9 \x8cuvre \x9cuf \x95 \x85', 24, 150, 16, helv_width, win_ansi),
    line(bytes.fromhex('7f818d8f909d'), 24, 124, 16, helv_width, win_ansi),
    line(b'A B\xa0C\xadD', 24, 98, 16, helv_width, win_ansi, tw=5),
    line(b'A\x00B', 24, 72, 16, helv_width, win_ansi),
]


def advance(row):
    raw, _, _, size, tw, width, _ = row
    return sum(width(b) / 1000 * size + (tw if b == 0x20 else 0) for b in raw)


x = 24 + advance(winansi[-1])
for raw, adjust in ((b'W', 80), (b'A', -120), (b'V', None)):
    row = line(raw, x, 72, 16, helv_width, win_ansi)
    winansi.append(row)
    x += advance(row) + (-adjust / 1000 * 16 if adjust is not None else 0)
winansi += [line(b'Courier ASCII', 24, 44, 12, cour_width, ascii_fallback),
            line(b'Restored Helvetica', 24, 20, 12, helv_width, win_ansi)]
pages_expected = {1: [line(b'Same name AW', 24, 120, 20, helv_width, win_ansi)],
                  2: [line(b'Same name AW', 24, 120, 20, cour_width, ascii_fallback)]}

cases = [
    ('winansi', 'tests/fixtures/font-winansi.pdf', [0, 0, 400, 240], {1: winansi}),
    ('pages', 'tests/fixtures/font-pages.pdf', [0, 0, 400, 240], pages_expected),
    ('geometry', 'tests/fixtures/geometry-raw.pdf', [0, 0, 400, 300], None),
    ('hello', 'tests/hello.pdf', None, None),
    ('compilerbook', 'tests/compilerbook.pdf', None, None),
]
manifest = {
    'dpi': DPI, 'tolerance': {'absolute': ABS, 'relative': REL, 'visual_pixels': 2},
    'adapter': 'real M9 font adapter (src/font.c) through tests/font-text-test staged probe',
    'versions': {'pdftoppm': subprocess.run(['pdftoppm', '-v'], capture_output=True, text=True).stderr.strip()},
    'render_fonts': {'Helvetica': helv_path, 'Courier': cour_path},
    'afm_reference': str(AFM) if AFM.exists() else None,
    'width_check': {'codes': len(width_check), 'mismatches': width_mismatch},
    'binaries': {p: digest(p) for p in ['pdftext', 'tests/font-text-test']}, 'cases': []}
assert not width_mismatch, width_mismatch
sections, all_pass = [], True
for name, path, box, expected in cases:
    entry = {'name': name, 'source': path, 'sha256': digest(path), 'commands': []}
    cmds = entry['commands']
    pages = sorted(expected) if expected else [1]
    for p in pages:
        (OUT / f'{name}-page{p}.png').unlink(missing_ok=True)  # never reuse old renders
        cmds.append(run(f'{name}-render{p}', ['pdftoppm', '-f', str(p), '-l', str(p), '-singlefile', '-r', str(DPI),
                                              '-png', path, str(OUT / f'{name}-page{p}')]))
    cmds.append(run(name + '-info', ['pdfinfo', '-f', '1', '-l', str(pages[-1]), '-box', path]))
    cmds.append(run(name + '-reference', ['pdftotext', '-raw', '-enc', 'UTF-8', path, '-']))
    assert all(c['exit'] == 0 for c in cmds), (name, 'external reference command failed', cmds)
    cmds.append(run(name + '-cpt', ['./pdftext', '--dump-content', path]))
    cmds.append(run(name + '-trace', ['./tests/font-text-test', path]))
    entry['page_attributes'] = (OUT / (name + '-info.stdout')).read_text()
    trace = (OUT / (name + '-trace.stdout')).read_text()
    lines = [json.loads(l) for l in trace.splitlines()]
    raws = [l for l in lines if 'decode' not in l]
    decs = [l for l in lines if 'decode' in l]
    right_parts = []
    if name == 'geometry':
        assert cmds[-1]['exit'] == 0
        m8 = Path('tests/golden/geometry.stdout').read_text()
        same = ''.join(json.dumps(r, separators=(',', ':')) for r in raws) == ''.join(
            json.dumps(json.loads(l), separators=(',', ':')) for l in m8.splitlines())
        widths_ok = all(w == 600 for d in decs for w in d['decode']['widths'])
        entry['m8_raw_equal'] = same
        entry['courier_render_width_600'] = all(cour(c) == 600 for c in range(32, 127))
        all_pass &= same and widths_ok and entry['courier_render_width_600']
        right_parts.append(f'<h3>Real adapter vs M8 test adapter</h3><p>Raw geometry lines identical to '
                           f'tests/golden/geometry.stdout: <b>{"PASS" if same else "FAIL"}</b>. All widths 600 from '
                           f'/Widths: {"PASS" if widths_ok else "FAIL"}. Rendered Courier ({html.escape(cour_path)}) advance '
                           f'600 for 32..126: {"PASS" if entry["courier_render_width_600"] else "FAIL"}.</p>')
    elif expected:
        assert cmds[-1]['exit'] == 0
        rows = []
        for p in pages:
            exp_rows = expected[p]
            got = [(r, d) for r, d in zip(raws, decs) if r['page'] == p]
            assert len(got) == len(exp_rows), (name, p, len(got), len(exp_rows))
            info = entry['page_attributes']
            img = Image.open(OUT / f'{name}-page{p}.png').convert('RGB')
            draw = ImageDraw.Draw(img)
            for i, ((r, d), (raw, x, y, size, tw, width, decode)) in enumerate(zip(got, exp_rows)):
                adv = advance([raw, x, y, size, tw, width, decode])
                wanted = [x, y, adv, 0, size, 0, 0, size, x, y]
                actual = r['origin'] + r['advance'] + r['rendering']
                delta = [a - b for a, b in zip(actual, wanted)]
                utf8 = ''.join(chr(decode(b)) for b in raw).encode('utf-8')
                ok_bytes = bytes.fromhex(r['bytes']) == raw
                ok_utf8 = bytes.fromhex(d['decode']['utf8']) == utf8
                ok_geo = all(abs(v) <= ABS + REL * abs(w) for v, w in zip(delta, wanted))
                passed = ok_bytes and ok_utf8 and ok_geo
                all_pass &= passed
                rows.append({'page': p, 'raw_hex': raw.hex(), 'expected_utf8': utf8.hex(),
                             'actual_utf8': d['decode']['utf8'], 'replacements': d['decode']['replacements'],
                             'width_sources': sorted(set(d['decode']['width_sources'])),
                             'expected': wanted, 'actual': actual, 'delta': delta,
                             'bytes_ok': ok_bytes, 'unicode_ok': ok_utf8, 'geometry_ok': ok_geo, 'pass': passed})
                px = (x - box[0]) * DPI / 72; py = (box[3] - y) * DPI / 72
                ex = px + r['advance'][0] * DPI / 72
                draw.line((px, py, ex, py), fill='#005fbd', width=2)
                draw.line((ex, py - 6, ex, py + 6), fill='#cc2460', width=2)
                draw.ellipse((px - 3, py - 3, px + 3, py + 3), fill='#005fbd')
                draw.text((px, py + 4), str(i + 1), fill='#005fbd')
            img.save(OUT / f'{name}-overlay{p}.png')
        entry['comparison'] = rows
        table = ('<table><tr><th>#</th><th>Page</th><th>Raw hex</th><th>Project UTF-8 (escaped)</th><th>Width sources</th>'
                 '<th>Origin / advance expected</th><th>Actual</th><th>Max Δ</th><th>Bytes</th><th>Unicode</th><th>Geometry</th></tr>')
        for i, row in enumerate(rows):
            text = bytes.fromhex(row['actual_utf8']).decode('utf-8')
            esc = ''.join(c if 0x20 <= ord(c) < 0x7f else f'\\u{ord(c):04x}' for c in text)
            table += (f'<tr><td>{i + 1}</td><td>{row["page"]}</td><td><code>{row["raw_hex"]}</code></td><td>{html.escape(text)}<br><code>{html.escape(esc)}</code></td>'
                      f'<td>{", ".join(row["width_sources"])}</td><td>{[round(v, 6) for v in row["expected"][:3]]}</td>'
                      f'<td>{[round(v, 6) for v in row["actual"][:3]]}</td><td>{max(map(abs, row["delta"])):.3g}</td>'
                      + ''.join(f'<td>{"PASS" if row[k] else "FAIL"}</td>' for k in ('bytes_ok', 'unicode_ok', 'geometry_ok')) + '</tr>')
        table += '</table>'
        imgs = ''.join(f'<p>Page {p}</p><img src="{name}-overlay{p}.png">' for p in pages)
        right_parts.append(f'<h3>Project origin/advance overlay</h3>{imgs}<p>Blue: baseline origin and advance; pink tick: advance end (not an ink bbox).</p>{table}')
    else:
        assert cmds[-1]['exit'] == 4 and trace == ''
        assert cmds[-2]['exit'] == 4 and (OUT / (name + '-cpt.stdout')).read_bytes() == b''
        right_parts.append(f'<h3>Unsupported (exit 4)</h3><p>Actual stdout empty; real rejection point:</p>'
                           f'<pre>{html.escape((OUT / (name + "-trace.stderr")).read_text())}</pre>')
    originals = ''.join(f'<p>Page {p}</p><a href="{name}-page{p}.png"><img src="{name}-page{p}.png"></a>' for p in pages)
    ref = (OUT / (name + '-reference.stdout')).read_text(errors='replace')
    sections.append(f'<section><h2>{name}</h2><p>Source {html.escape(path)} · SHA-256 {entry["sha256"]}</p><div class="grid"><div><h3>Original PDF, Poppler render</h3>{originals}</div><div>{"".join(right_parts)}</div></div>'
                    f'<details><summary>External reference: pdftotext -raw (not project output)</summary><pre>{html.escape(ref)}</pre></details>'
                    f'<details><summary>Actual project staged trace</summary><pre>{html.escape(trace)}</pre></details></section>')
    manifest['cases'].append(entry)
manifest['numeric_pass'] = all_pass
(OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
(OUT / 'index.html').write_text(
    '<!doctype html><html lang="en"><meta charset="utf-8"><title>M9 font acceptance</title><style>body{font:16px system-ui;margin:28px;color:#152434}'
    'section{border-top:1px solid #aaa;padding:20px 0}.grid{display:grid;grid-template-columns:1fr 1.4fr;gap:24px}img{max-width:100%;border:1px solid #aaa}'
    'pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:11px}table{border-collapse:collapse;font-size:12px}td,th{padding:4px;border:1px solid #aaa;vertical-align:top}'
    'code{overflow-wrap:anywhere}</style><h1>M9: real font widths and WinAnsi/UTF-8</h1>'
    f'<p>Expected widths from the rendered font files ({html.escape(helv_path)}, {html.escape(cour_path)}); fixture /Widths match for all '
    f'{len(width_check)} Helvetica codes. Expected Unicode from Python cp1252 plus the PDF WinAnsi bullet/control policy. Tolerance 1e-8 + 1e-9·|x|; overlay 2 px. '
    'Source order, not reading order. Poppler text is reference only.</p>' + ''.join(sections) + '</html>')
assert all_pass, 'comparison failed; inspect manifest'
print('M9 widths/Unicode/geometry comparisons passed; inspect PNGs and overlays.')
