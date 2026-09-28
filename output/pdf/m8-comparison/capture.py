#!/usr/bin/env python3
"""Optional visual acceptance only; run from repo root. Requires Poppler/Pillow."""
import hashlib, html, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw
OUT=Path('output/pdf/m8-comparison')
DPI=120
ABS,REL=1e-8,1e-9

def run(name,args):
    r=subprocess.run(args,capture_output=True)
    (OUT/(name+'.stdout')).write_bytes(r.stdout)
    (OUT/(name+'.stderr')).write_bytes(r.stderr)
    return {'command':args,'exit':r.returncode,'stdout':name+'.stdout','stderr':name+'.stderr'}
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
# Independent hand-computed expected origins, advances, and rendering matrices.
expected={
 'visual':[
  ('PDF visual comparison',36,192,201.6,16,16,0),
  ('Line 2: Hello PDF',36,164,163.2,16,16,0),
  ('A',36,136,9.6,16,16,0),('B',49.6,136,9.6,16,16,0),
  ('SCALED',36,96,64.8,18,18,0),('Restored after Q',36,48,115.2,12,12,0)],
 'geometry':[
  ('LINE ONE',36,250,57.6,12,12,0),('LINE TWO',46,226,57.6,12,12,0),
  ('A',46,208,7.2,12,12,0),('B',51.76,208,7.2,12,12,0),('C',61.96,208,7.2,12,12,0),
  ('A B',46,194,22.08,9.6,12,0),('INVISIBLE',46,172,64.8,12,12,3),
  ('SCALE',36,110,72,24,24,0),('RESTORED',36,76,57.6,12,12,0),('REVERSE',36,40,100.8,24,24,0)]}
cases=[('visual','tests/fixtures/visual-m7-text.pdf',[0,0,360,240]),
       ('geometry','tests/fixtures/geometry-raw.pdf',[0,0,400,300]),
       ('hello','tests/hello.pdf',None),('compilerbook','tests/compilerbook.pdf',None)]
manifest={'dpi':DPI,'page':1,'adapter':'test Courier-600: F1 codes 32..126, matches explicit PDF Widths; no production font parser',
          'tolerance':{'absolute':ABS,'relative':REL,'visual_pixels':2},
          'versions':{'pdftoppm':subprocess.run(['pdftoppm','-v'],capture_output=True,text=True).stderr.strip()},
          'binaries':{p:digest(p) for p in ['pdftext','tests/geometry-test']},'cases':[]}
sections=[]; all_pass=True
for name,path,box in cases:
    entry={'name':name,'source':path,'sha256':digest(path),'page':1,'commands':[]}
    cmds=entry['commands']
    # Never reuse an earlier renderer's image if this run fails.
    (OUT/(name+'-page1.png')).unlink(missing_ok=True)
    cmds.append(run(name+'-render',['pdftoppm','-f','1','-singlefile','-r',str(DPI),'-png',path,str(OUT/(name+'-page1'))]))
    cmds.append(run(name+'-info',['pdfinfo','-f','1','-l','1','-box',path]))
    cmds.append(run(name+'-reference',['pdftotext','-f','1','-l','1','-bbox',path,'-']))
    assert all(c['exit']==0 for c in cmds), (name, 'external reference command failed', cmds)
    cmds.append(run(name+'-cpt',['./pdftext','--dump-content',path]))
    cmds.append(run(name+'-trace',['./tests/geometry-test',path]))
    entry['png_size']=Image.open(OUT/(name+'-page1.png')).size
    entry['page_attributes']=(OUT/(name+'-info.stdout')).read_text()
    actual=(OUT/(name+'-trace.stdout')).read_text()
    if name in expected:
        assert cmds[-1]['exit']==0
        events=[json.loads(line) for line in actual.splitlines()]
        assert len(events)==len(expected[name]); rows=[]
        page=Image.open(OUT/(name+'-page1.png')).convert('RGB'); draw=ImageDraw.Draw(page)
        # Default user space -> screenshot, valid only for these controlled pages.
        # Confirm MediaBox using pdfinfo rather than relying on guessed dimensions.
        info=entry['page_attributes']
        box_line=next(l for l in info.splitlines() if 'MediaBox:' in l)
        actual_box=[float(v) for v in box_line.split('MediaBox:')[1].split()]
        assert actual_box==box,(name,actual_box,box)
        entry.update({'media_box':box,'rotate':0,'user_unit':1,'crop_equals_media':True})
        for i,(event,exp) in enumerate(zip(events,expected[name])):
            label,x,y,advance,sx,sy,mode=exp
            wanted=[x,y,advance,0,sx,0,0,sy,x,y]
            got=event['origin']+event['advance']+event['rendering']
            delta=[a-b for a,b in zip(got,wanted)]
            passed=(bytes.fromhex(event['bytes'])==label.encode('ascii') and event['mode']==mode and
                    all(abs(d)<=ABS+REL*abs(w) for d,w in zip(delta,wanted)))
            all_pass &= passed
            rows.append({'ascii_preview':label,'expected':wanted,'actual':got,'delta':delta,'pass':passed,'mode':mode})
            px=(event['origin'][0]-box[0])*DPI/72; py=(box[3]-event['origin'][1])*DPI/72
            ex=px+event['advance'][0]*DPI/72; ey=py-event['advance'][1]*DPI/72
            color='#cc2460' if mode==3 else '#005fbd'
            draw.line((px,py,ex,ey),fill=color,width=2)
            draw.line((ex-5,ey-3,ex,ey,ex-5,ey+3),fill=color,width=2)
            draw.ellipse((px-3,py-3,px+3,py+3),fill=color)
            draw.text((px,py+4),str(i+1),fill=color)
        page.save(OUT/(name+'-overlay.png'))
        entry['comparison']=rows
        table='<table><tr><th>Raw ASCII preview</th><th>Expected origin / advance</th><th>Actual</th><th>Max delta</th><th>Result</th></tr>'
        for row in rows:
            table+=f'<tr><td>{html.escape(row["ascii_preview"])}</td><td>{row["expected"][:4]}</td><td>{row["actual"][:4]}</td><td>{max(map(abs,row["delta"])):.3g}</td><td>{"PASS" if row["pass"] else "FAIL"}</td></tr>'
        table+='</table>'
        right=f'<h3>Project geometry overlay</h3><img src="{name}-overlay.png"><p>Blue: origin and advance. Pink: Tr=3 event, intentionally invisible on original. Labels follow source order.</p>{table}'
    else:
        assert cmds[-1]['exit']==4 and actual==''
        assert cmds[-2]['exit']==4 and (OUT/(name+'-cpt.stdout')).read_bytes()==b''
        stderr=(OUT/(name+'-trace.stderr')).read_text()
        right=f'<h3>Unsupported (exit 4)</h3><p>Actual stdout is empty. This is a compatibility gap, not a success case.</p><pre>{html.escape(stderr)}</pre>'
    sections.append(f'<section><h2>{name}</h2><p>Source: {html.escape(path)} · page 1 · SHA-256 {entry["sha256"]}</p><div class="grid"><div><h3>Original PDF, rendered by Poppler</h3><a href="{name}-page1.png"><img src="{name}-page1.png"></a></div><div>{right}</div></div><details><summary>Actual project trace (raw bytes; not Unicode)</summary><pre>{html.escape(actual)}</pre></details><p><a href="{name}-reference.stdout">External Poppler word boxes</a> · <a href="{name}-trace.stdout">Project raw trace</a> · <a href="{name}-cpt.stderr">CLI stderr</a></p></section>')
    manifest['cases'].append(entry)
manifest['numeric_pass']=all_pass
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
(OUT/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><title>M8 PDF geometry acceptance</title><style>body{font:16px system-ui;margin:28px;color:#152434}section{border-top:1px solid #aaa;padding:20px 0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:24px}img{max-width:100%;height:auto;border:1px solid #aaa}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}table{border-collapse:collapse;font-size:12px}td,th{padding:6px;border:1px solid #aaa}p{overflow-wrap:anywhere}</style><h1>M8: actual PDF and project geometry</h1><p>Explicit Courier-600 test adapter only. Origins/advance vectors are not ink bounding boxes. Absolute tolerance 1e-8 + relative 1e-9; visual inspection tolerance 2 pixels. Rendering and comparisons use default user-space coordinates. Unicode, TextItem and reading order remain M9–M11 work.</p>'''+''.join(sections)+'</html>')
assert all_pass,'numeric comparison failed; inspect manifest'
print('M8 numerical/bytes/rendering comparisons passed; inspect PNGs and overlays.')
