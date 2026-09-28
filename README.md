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

## 支援範圍（v1.0）

支援：

- 未加密 PDF、traditional xref table、無 incremental update。
- direct／indirect object、Contents array、未壓縮 stream 與單一 FlateDecode。
- Catalog、Pages tree 與頁面屬性繼承。
- Simple Font（`/Type1`、`/TrueType`），明示的 `/Widths`、`/MissingWidth`；`/WinAnsiEncoding` 或沒有 `/Encoding`（只映射 ASCII 0x20–0x7E）。
- UTF-8 輸出與單欄、水平的閱讀順序；Tr=3 不可見文字（例如掃描檔的 OCR 層）也會輸出。

不支援（會回報錯誤，不會默默略過）：

- 加密、xref stream、object stream、incremental update、損壞檔修復。
- `/ToUnicode`、`/Differences`、Type0／CIDFont、Type3、Symbol／ZapfDingbats、symbolic font、沒有 `/Widths` 的字型。
- 頁面 `/Rotate` 非 0、旋轉／鏡像／直書的文字。
- 線寬、路徑、顏色、clipping、marked content、XObject、inline image 等 PDF 圖形運算子：content stream 中出現就回報 unsupported（完全不屬於 PDF 規格的未知運算子則會被忽略）。

## 已知限制

- 不偵測多欄；多欄頁面會依 y 座標交錯輸出。
- 上標、下標沒有特別處理：基線差超過行容差（`max(1.5, 較小字級 × 0.25)`）時會自成一行。
- 補空白用「間距大於前一段字級 × 0.25」判斷；剛好等於門檻的字距調整不會補空白。
- 不做斷字合併、連字展開、Unicode 正規化或段落判斷。
- 純文字、`--dump-content` 與 `--dump-text-items` 保證全有或全無；其他除錯模式是串流輸出，失敗前可能已印出部分內容（exit code 仍會反映錯誤）。

## 專案結構

處理流程為 `reader → xref → document → pages → contents → content interpreter → text state → font → text items → reading order`，每層都有對應的 `src/*.h` 介面與 `tests/*_test.c`。各階段視覺驗收見 [docs/pdf-visual-comparison.md](docs/pdf-visual-comparison.md)。
