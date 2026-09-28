from pathlib import Path
import hashlib,json,subprocess
out=Path('output/pdf/m7-comparison')
cases=[('visual','tests/fixtures/visual-m7-text.pdf'),('hello','tests/hello.pdf'),('compilerbook','tests/compilerbook.pdf')]
manifest={'scope':'M7 evidence; page-1 screenshots/reference text; CLI handles whole document', 'cases':[]}
for name,source in cases:
    row={'name':name,'source':source,'sha256':hashlib.sha256(Path(source).read_bytes()).hexdigest(),'commands':[]}
    commands=[
        ('render',['pdftoppm','-f','1','-l','1','-singlefile','-r','120','-png',source,str(out/(name+'-page1'))]),
        ('reference',['pdftotext','-f','1','-l','1','-layout',source,'-']),
        ('bbox',['pdftotext','-f','1','-l','1','-bbox',source,'-']),
        ('cpt',['./pdftext','--dump-content',source]),
        ('trace',['/tmp/cpt-visual-content-probe',source])]
    for kind,argv in commands:
        r=subprocess.run(argv,capture_output=True)
        (out/(name+'-'+kind+'.stdout')).write_bytes(r.stdout)
        (out/(name+'-'+kind+'.stderr')).write_bytes(r.stderr)
        row['commands'].append({'kind':kind,'argv':argv,'exit_code':r.returncode})
    manifest['cases'].append(row)
manifest['tools']={}
for name,cmd in [('poppler',['pdftoppm','-v']),('pdftotext',['pdftotext','-v']),('git',['git','rev-parse','HEAD'])]:
    r=subprocess.run(cmd,capture_output=True,text=True)
    manifest['tools'][name]=(r.stdout+r.stderr).strip()
(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({r['name']:{c['kind']:c['exit_code'] for c in r['commands']} for r in manifest['cases']},indent=2))
