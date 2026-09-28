from pathlib import Path
import hashlib,html,json,re,xml.etree.ElementTree as ET
out=Path('output/pdf/m7-comparison')
manifest=json.loads((out/'manifest.json').read_text())
manifest['tools']['pdftext_sha256']=hashlib.sha256(Path('pdftext').read_bytes()).hexdigest()
raw=(out/'visual-trace.stdout').read_text()
segments=re.findall(r'^RAW_STRING_HEX ([0-9A-F]*)$',raw,re.M)
preview=[bytes.fromhex(s).decode('ascii') for s in segments]
reference=(out/'visual-reference.stdout').read_text()
assert ''.join(''.join(preview).split())==''.join(reference.split())
assert preview==['PDF visual comparison','Line 2: Hello PDF','A','B','SCALED','Restored after Q']
assert (out/'visual-cpt.stdout').read_text()=='PAGES 1\nPAGE 1 OPS 22 TEXT_SHOWS 5\n'
# Word bboxes are in Poppler's top-left convention, not M8 baseline coordinates.
words=[]
root=ET.fromstring((out/'visual-bbox.stdout').read_text())
for word in root.iter():
 if word.tag.endswith('word'):words.append({'text':word.text,**{k:float(v) for k,v in word.attrib.items()}})
manifest['checks']={'visual_ascii_content_and_source_order':'pass','visual_m7_summary':'pass','m8_geometry':'not implemented; pending','unicode_decoding':'not implemented; ASCII preview only'}
manifest['visual_reference_word_bboxes']=words
(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
sections=[]
configs=[
 ('visual','可成功解析的真實 PDF 測試頁','已核對 M7 操作數與 ASCII bytes 內容／順序；M8 座標尚未實作。','tests/fixtures/visual-m7-text.pdf'),
 ('hello','現有 hello.pdf','Poppler 可顯示與提取參考文字；本專案在第一個 w operator 停止（exit 4）。','tests/hello.pdf'),
 ('compilerbook','現有 compilerbook.pdf 第 1 頁','Poppler 可顯示封面；本專案尚不支援 xref stream，未進入 Contents／M7（exit 4）。','tests/compilerbook.pdf')]
for name,title,status,source in configs:
 def read(kind,stream):return (out/(name+'-'+kind+'.'+stream)).read_text()
 cli=read('cpt','stdout') or '(stdout 為空)\n'
 err=read('cpt','stderr') or '(stderr 為空)\n'
 ref=read('reference','stdout').replace('\f','').rstrip()
 trace=read('trace','stdout')
 detail=f'<details><summary>M6/M7 實際診斷 trace（非 geometry 結果）</summary><pre>{html.escape(trace or "未取得 decoded content")}</pre></details>'
 sourcehref='../../../'+source
 sections.append(f'''<section><h2>{html.escape(title)}</h2><p class="status">{html.escape(status)}</p>
 <p><a href="{sourcehref}">原始 PDF</a> · <a href="{name}-cpt.stdout">CLI stdout</a> · <a href="{name}-cpt.stderr">CLI stderr</a> · <a href="{name}-reference.stdout">Poppler 參考文字</a> · <a href="{name}-bbox.stdout">Poppler word bboxes</a></p>
 <div class="comparison"><figure><a href="{name}-page1.png"><img src="{name}-page1.png" alt="{html.escape(title)} 原始頁面渲染"></a><figcaption>原始 PDF 第 1 頁，Poppler 120 DPI，未加 overlay</figcaption></figure>
 <div><h3>本專案實際輸出</h3><pre>{html.escape(cli)}</pre><h3>本專案 stderr</h3><pre>{html.escape(err)}</pre>
 <h3>外部參考：Poppler pdftotext</h3><pre>{html.escape(ref)}</pre>{detail}</div></div></section>''')
page='''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PDF 畫面與實際解析結果對照</title><style>
*{box-sizing:border-box}body{margin:0;background:#f2f4f7;color:#172033;font-family:system-ui,sans-serif;line-height:1.6}main{max-width:1300px;margin:auto;padding:32px 24px}h1{font-size:30px}h2{font-size:23px;margin:0}h3{font-size:16px;margin:20px 0 8px}section{background:white;border:1px solid #d8dee8;border-radius:12px;padding:24px;margin:28px 0}a{color:#1c57b7}.intro{max-width:900px}.status{background:#eaf0fb;padding:12px 16px;border-radius:6px}.comparison{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:28px;align-items:start}figure{margin:0}img{display:block;width:100%;height:auto;border:1px solid #d8dee8}figcaption{font-size:13px;color:#566477;margin-top:8px}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f5f7fa;border:1px solid #e1e6ee;padding:14px;font:14px/1.6 ui-monospace,monospace;margin:0}details{margin-top:20px}summary{cursor:pointer;color:#1c57b7}@media(max-width:800px){.comparison{grid-template-columns:1fr}main{padding:18px}section{padding:16px}}
</style><main><h1>PDF 畫面與實際解析結果對照</h1><p class="intro">左側是實際 PDF 頁面渲染；右側是目前 pdftext 的真實 CLI 輸出及外部參考文字。M7 尚未做字型 Unicode 解碼或座標計算，M8 尚未實作。因此成功解析、畫面內容核對與 geometry 一致性是分開記錄的驗收結果。</p><p>原始字串 HEX 來自 M7 visitor；ASCII_PREVIEW 只便於核對本測試頁，不是正式文字提取。TEXT_SHOWS 計操作數：這頁 5 次文字顯示操作包含 6 個字串片段。</p>'''+''.join(sections)+'''<p><a href="manifest.json">來源 SHA-256、命令、工具版本與核對結果</a> · <a href="content_probe.c">使用真實 M6/M7 接口的診斷程式</a></p></main></html>'''
(out/'index.html').write_text(page)
print('PASS: M7 summary; six ASCII segments match reference content/source order after whitespace normalization.')
print('M8 geometry remains pending; bbox comparison has not been claimed as passing.')
