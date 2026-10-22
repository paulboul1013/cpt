# test_git.pdf 全書文字提取驗收

原始檔案全部 479 頁已成功提取。這次補上剩餘 17 種指令，保留工作區已有的中文、gs、
cs／CS、scn／SCN 與 BDC／EMC 實作。原檔沒有修改。

## 實際結果

| 項目 | 結果 |
|---|---|
| 原檔大小 | 36,959,295 bytes |
| SHA-256 | `d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70` |
| 掃描頁數 | 479／479 |
| 運算子 | 33 種，1,037,334 次，全部 M7 接受 |
| 正式文字提取 | exit 0，stderr 空白 |
| UTF-8 純文字 | 325,374 bytes |
| TextItem | 177,267 個，分布在 396 頁 |
| 未映射字元 | 0 |
| Image XObject | 252 次呼叫，248 個不同影像，180 頁；沒有 Form |
| sanitizer 實際全書 | exit 0，無 ASan／UBSan／LeakSanitizer 診斷 |

實作前在第一頁 Page object byte 3796、decoded byte 149 的 `w` 停止，exit 4，stdout 空白。
新增的指令為 `w J M j d m l h W* n re f Do W S c B*`。

- [實際純文字](actual.txt)
- [指令與逐頁分布](inventory/result.json) 與 [摘要表](inventory/summary.tsv)
- [驗收結果、執行命令、耗時與原始碼 hash](result.json)
- [完整壓縮 TextItem trace](items.jsonl.gz)
- [sanitizer 驗收結果](asan/result.json)
- [標準測試紀錄](make-test.log) 與 [sanitizer 測試紀錄](make-asan.log)

## 文字比較的範圍

參考為 Poppler 24.02.0 的 [實際輸出](reference.txt)，325,654 bytes。
逐頁去除空白後比較 Unicode 字元計數，479 頁均沒有缺少參考字元；
只有第 161 頁多一個 ASCII `-`。來源碼區塊的 `# ---` 在本工具保留三個連字號，
Poppler 合併換行後成為 `# --# To remove`，因此少一個。

純文字順序不完全相同。既有 M11 依基線與 x 座標排序，Poppler 對混合字型、行內程式碼
與分行有不同分組；空白與 tab 也不相同。字元計數用來驗證完整性，不能證明閱讀順序一致。
本次沒有改寫 M11 排版，也沒有宣稱輸出與 Poppler 逐 byte 相同。

## 畫面與座標

以下 PNG 由未修改的原始 PDF 渲染。SVG 疊上本工具的實際 TextItem 基線起點與 advance：
紅點是 origin，綠線是 advance，不是 glyph bbox。滑鼠指向標記可看 order／decoded offset。
SVG 使用原頁 MediaBox `[0 0 595 842]` 與 Rotate 0，將 PDF 的左下原點轉成 SVG 的左上原點。

| PDF 頁碼 | 原始畫面 | 座標對照 | 原始 TextItem |
|---|---|---|---|
| 3 | [目錄頁](page-3.png) | [基線與 advance](page-3-overlay.svg) | [JSONL](page-3-items.jsonl) |
| 22 | [標題與項目符號](page-22.png) | [基線與 advance](page-22-overlay.svg) | [JSONL](page-22-items.jsonl) |
| 479 | [正文與影像截圖](page-479.png) | [基線與 advance](page-479-overlay.svg) | [JSONL](page-479-items.jsonl) |

第 3 頁提取「目錄」，第 22 頁提取「本書內容」與項目文字，第 479 頁提取標題、正文
與原生文字程式碼。第 479 頁 SourceTree 截圖中的文字是影像像素，不進行 OCR。
抽查的 baseline 與文字位置相符，影像沒有造成後續文字位置偏移。

## 支援政策與測試

這是來源文字提取。路徑、繪製與裁切經語法驗證後不改變文字參數與座標，
不依裁切可見性過濾文字。Do 必須解析並驗證 Image stream 與尺寸，影像像素不解碼。
Form 與其他 subtype 保持 unsupported，避免默默丟棄 Form 內的文字。
完整契約見 [路徑與影像支援](../../../docs/drawing-text.md)。

`make test` 與沙箱外的 `make asan` 均通過 157 筆 CLI fixtures、全部單元測試與 CLI 原子輸出測試。
新增 9 份靜態 PDF、15 筆 golden cases，以及 drawing／XObject 單元測試。
全書的一般版與 sanitizer 版純文字、content 摘要和解壓後 TextItem trace 完全一致。
沙箱內的 LeakSanitizer 受 ptrace 限制，正式 sanitizer 驗收在沙箱外執行。

重跑受控與真實檔案驗收：

```sh
make test
make asan
python3 tests/make-drawing-fixtures.py
python3 tools/verify-test-git.py --render
python3 tools/verify-test-git.py --binary ./pdftext-asan --out-dir output/pdf/test-git-support/asan
```

核心測試只需 C 工具鏈、libc、zlib 與 libm。額外全書驗收需要原始 PDF、Python 3、
Poppler 與 C 編譯器，不需要第三方 Python 套件。腳本檢查固定原檔 hash，
以免意外驗收了不同版本的 PDF。
