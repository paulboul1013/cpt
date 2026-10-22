# pdftext

版本 1.0.0。用 C11 寫的 PDF 文字提取工具：讀取 PDF 的原生文字層，依單欄、水平的閱讀順序輸出 UTF-8 純文字。只依賴 libc（含 libm）與 zlib。

完整規格與各階段契約見 [pdftext_roadmap.md](docs/archive/pdftext_roadmap.md)。

## 建置與測試

```sh
make                 # 建出 ./pdftext
make test            # 單元測試、CLI fixtures 與 golden
make asan            # 同一套測試，開啟 ASan／UBSan／LeakSanitizer
make CC=clang test   # 以 clang 建置與測試
```

`CC`、`CFLAGS`、`LDFLAGS`、`LDLIBS` 都可以覆寫。核心測試只用 POSIX shell，不需要 Python 或其他工具；`output/pdf/*/capture.py` 的視覺對照需要 Poppler、Pillow 與 fontTools，只用於額外驗收。

## 用法

```sh
./pdftext input.pdf                  # 純文字寫到 stdout
./pdftext input.pdf > output.txt     # 用 shell 重導向存檔
./pdftext -o output.txt input.pdf    # 由程式寫檔：成功才以 rename 取代，失敗不動原檔
./pdftext --help
./pdftext --version
```

`-o` 先寫同目錄的隱藏暫存檔，成功才 rename 成目標檔；目標若是 symlink，會被換成一般檔案。檔名不可以 `-` 開頭（請寫 `./-name`）。

整份文件成功才輸出；任何一頁失敗時 stdout（或 `-o` 檔案）不會有部分結果。錯誤、警告與提示寫到 stderr：

- 沒有任何文字：stdout 空白，stderr 印 `pdftext: no extractable text layer`，exit 0。
- 有無法解碼的字元：照樣輸出 U+FFFD，stderr 印一行警告，exit 0。

### 除錯模式

一次只能選一個模式，輸出寫到 stdout，不能和 `-o` 一起用。

| 模式 | 內容 |
|---|---|
| `--dump-header` | PDF header 版本 |
| `--dump-xref` | traditional xref table 與 trailer |
| `--dump-trailer` | trailer dictionary |
| `--dump-object N` | 第 N 個 indirect object（generation 取自 xref） |
| `--dump-pages` | 頁樹摘要 |
| `--dump-contents` | 每頁解碼後的 Contents 長度 |
| `--dump-content` | 每頁 content 運算子摘要 |
| `--dump-text-items` | 每個文字片段一行 JSON（source order，不是閱讀順序） |
| `--dump-objects` | 依序 dump 檔案中的每個物件 |
| `--object`、`--indirect` | 單一物件的語法除錯 |

### Exit code

| Code | 意義 |
|---:|---|
| 0 | 成功（包含沒有文字的 PDF） |
| 1 | 命令列用法錯誤 |
| 2 | I/O 錯誤（讀檔或寫 `-o` 檔案失敗） |
| 3 | PDF 格式錯誤 |
| 4 | 不支援的 PDF 功能 |
| 5 | 記憶體不足 |
| 6 | 超過資源上限 |

## 目前支援範圍

支援：

- 未加密 PDF、traditional xref table、無 incremental update。
- direct／indirect object、Contents array、未壓縮 stream 與單一 FlateDecode。
- Catalog、Pages tree 與頁面屬性繼承。
- Simple Font（`/Type1`、`/TrueType`），明示的 `/Widths`、`/MissingWidth`；`/WinAnsiEncoding` 或沒有 `/Encoding`（只映射 ASCII 0x20–0x7E）。
- 中文文字提取：`/Type0`、`/Identity-H`、單一 CIDFontType0／CIDFontType2 descendant，必須附有 `/ToUnicode`。支援 `/W` 的字寬陣列與範圍、`/DW`，省略 `/DW` 時使用規格預設 1000。
- ToUnicode 支援固定單 byte 的 Simple Font 與固定雙 byte 的 Identity-H：`begincodespacerange`、`beginbfchar`、`beginbfrange` 字串／陣列形式；解碼 UTF-16BE、代理對與一對多 Unicode 映射。Simple Font 有有效 ToUnicode 時也接受 descriptor 的 Symbolic flag。
- `gs`／ExtGState 保守子集：可省略的 `/Type /ExtGState`、`SM` 0..1、boolean `SA`、`SMask /None`、`AIS false`、`CA`／`ca` 等於 1。完整驗證字典後繼續提取，保留原有文字狀態；支援 direct／indirect 值。
- `cs`／`CS`：驗證單一 name 參數後略過色彩效果，不查詢 ColorSpace 資源，不改變文字與座標。BT／ET 內外皆可使用。
- `scn`／`SCN`：接受數值參數、尾端可選的 pattern name，或單獨的 pattern name。驗證語法後略過色彩效果，不查詢 ColorSpace／Pattern 資源；不驗證各色彩空間的分量數或值域，不影響文字提取。
- `BDC`／`EMC` 的空 ActualText 子集：只接受 `/Span` 與 inline dictionary。沒有 ActualText 時照常提取；`/ActualText <>` 或 `()` 抑制區段的文字輸出，仍執行字寬、位移與字型驗證。支援巢狀區段，須與 BT／ET 正確巢狀；可跨同頁 Contents stream，不可跨頁。
- 線條樣式 `w J j M d` 與路徑 `m l c h re W W* n S f B*`：驗證參數、值域與 current point；繪圖、裁切不改變文字座標，也不依可見區域過濾來源文字。
- `Do` 的 Image XObject：解析本頁有效 Resources，驗證 stream、Type／Subtype 與正整數尺寸；不解碼像素、filter 或 mask，不做 OCR。Form 與其他 XObject subtype 仍拒絕。
- UTF-8 輸出與單欄、水平的閱讀順序；Tr=3 不可見文字（例如掃描檔的 OCR 層）也會輸出。

不支援（會回報錯誤，不會默默略過）：

- 加密、xref stream、object stream、incremental update、損壞檔修復。
- 預定義 CJK Encoding CMap、`/Identity-V`、自訂 Encoding CMap、`UseCMap`、可變長度字元碼、缺少 ToUnicode 的 Type0 字型。
- `/Differences`、Type3、Symbol／ZapfDingbats、沒有 ToUnicode 的 symbolic Simple Font、沒有 `/Widths` 的 Simple Font。
- 頁面 `/Rotate` 非 0、旋轉／鏡像／直書的文字。
- ExtGState 的 `Font`、非 1 的透明度、soft mask dictionary、`AIS true` 與其他 key。錯型或超出合法範圍的值回報 malformed。
- 非空 ActualText（包含 `<FEFF>`）、named Properties、其他 tag／property key、`BMC`／`MP`／`DP`；不宣稱完整 ActualText 或 Tagged PDF 支援。ActualText 值不是字串、作用域未配對或交叉關閉會回報 malformed。
- 上述子集以外的路徑指令（如 `v y s f*`）、其他顏色指令（如 `sc`／`SC`）、glyph clipping（Tr 4–7）、Form／PS XObject 與 inline image：content stream 中出現就回報 unsupported（完全不屬於 PDF 規格的未知運算子則會被忽略）。

## 已知限制

- 不偵測多欄；多欄頁面會依 y 座標交錯輸出。
- 上標、下標沒有特別處理：基線差超過行容差（`max(1.5, 較小字級 × 0.25)`）時會自成一行。
- 補空白用「間距大於前一段字級 × 0.25」判斷；剛好等於門檻的字距調整不會補空白。
- 不做斷字合併、連字展開、Unicode 正規化或段落判斷。
- ToUnicode 映射中的多字元序列會原樣輸出；不另外猜測或展開連字。未映射字元以一個 U+FFFD 取代；截斷的雙 byte 字元或無效 UTF-16BE 回報格式錯誤。
- 純文字、`--dump-content` 與 `--dump-text-items` 保證全有或全無；其他除錯模式是串流輸出，失敗前可能已印出部分內容（exit code 仍會反映錯誤）。

## 專案結構

處理流程為 `reader → xref → document → pages → contents → content interpreter → text state → font → text items → reading order`，每層都有對應的 `src/*.h` 介面與 `tests/*_test.c`。各階段視覺驗收見 [docs/pdf-visual-comparison.md](docs/pdf-visual-comparison.md)。

中文功能的介面、影響範圍與驗證見 [docs/chinese-text.md](docs/chinese-text.md)。
可重跑的中文 fixtures 產生器為 `tests/make-chinese-fixtures.py`，核心測試直接使用已提交的 PDF，不需 Python。

BDC／EMC 的契約、實作與測試證據見 [交接與驗收](docs/agents/bdc-emc-handoff.md)，
畫面／文字／座標對照見 [marked-comparison](output/pdf/marked-comparison/README.md)。

`test_git.pdf` 全部 479 頁、33 種指令已可提取；範圍與測試見 [路徑與影像支援](docs/drawing-text.md)，
實際文字、逐頁指令與座標對照見 [全書驗收](output/pdf/test-git-support/README.md)。
