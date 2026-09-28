# pdftext：架構與 Roadmap

> 本文件是架構、後續 milestone 與版本邊界的權威來源。M2 Object Parser 已於 2026-08-24 通過 release acceptance；其已完成規格與驗收結果見 [M2 驗收記錄](pdftext_m2_acceptance.md)。M3 Indirect Object Parser、M4 traditional xref／trailer、M5 Document／Resolver／Pages Tree 、M6 Contents／FlateDecode 與 M7 Content Interpreter 已完成；下一個 milestone 是 M8 Text State／Geometry。

## 1. Roadmap 定位

pdftext 的長期目標是以 C11 實作一個分層、可擴充的 PDF 原生文字提取 CLI：

    PDF bytes
       ↓
    Binary Reader
       ↓
    PDF Lexer
       ↓
    Object Parser
       ↓
    Indirect Objects / References
       ↓
    xref + trailer
       ↓
    Object Resolver
       ↓
    Catalog
       ↓
    Pages Tree
       ↓
    Page / Resources / Contents
       ↓
    Content Stream Decode
       ↓
    Content Stream Interpreter
       ↓
    Text State / Matrix
       ↓
    Font Decode
       ↓
    TextItem[]
       ↓
    Reading Order
       ↓
    Plain Text

Roadmap 的 milestone 編號已統一如下；舊文件中 M4–M8 的命名曾有偏移，之後以本表為準。

### 現行程式的閱讀路徑（M2 基線）

| 順序 | 模組 | 責任與交接 |
|---|---|---|
| 1 | `src/main.c` | CLI 選擇連續 object dump、`--object`、`--indirect`，或整份 PDF 的 `--dump-xref` 模式，連接下層模組。 |
| 2 | `src/reader.c`、`src/reader.h` | 載入有大小限制的 binary input，提供 peek/get/seek/tell/eof。 |
| 3 | `src/lexer.c`、`src/lexer.h` | 從 reader 的 bytes 產生 token，跳過 PDF whitespace 與 comments。 |
| 4 | `src/parser.c`、`src/parser.h` | 消耗 token，驗證 standalone object grammar，組合巢狀物件並清理失敗時的部分結果。 |
| 5 | `src/object.c`、`src/object.h` | 定義 tagged object、array/dictionary/reference，負責儲存、dump 與遞迴釋放。 |
| 6 | `src/xref.c`、`src/xref.h` | 從完整 PDF 的檔尾解析 traditional xref 和 trailer，保留物件 offset 與 /Root。 |
| 共用 | `src/error.*`、`src/limits.*`、`src/bytes.h` | 提供錯誤回報、資源上限與可包含 NUL 的 byte payload。 |

閱讀時可從 `main.c` 追輸入流程，再依 reader → lexer → parser → object model 追資料流；parser 透過 object API 建樹，不讓 lexer 承擔 PDF object grammar。此時 CLI 輸入仍是 standalone object fixture，尚不能擷取完整 PDF 的文字。

## 2. Normalized Milestones

| Milestone | 主題 | 結束時必須能做什麼 |
|---|---|---|
| M0 | Binary Reader | 讀取 whole-file buffer，提供 peek/get/seek/tell/eof；PDF header 驗證在 M3 開工前補齊 |
| M1 | Lexer | 完整 token 化 PDF whitespace、comment、number、name、string、delimiter、keyword |
| M2 | Object Parser | 建立可遞迴釋放的 primitive/array/dict/ref object tree |
| M3 | Indirect Object Parser | 解析 object number、generation、obj/endobj 與 stream raw bytes |
| M4 | xref / trailer | 解析 traditional xref table、subsections、trailer、startxref |
| M5 | Document / Resolver / Pages | 建立 resolver、Catalog、Pages Tree、Page inheritance |
| M6a | Raw Contents Stream | 取得 stream reference、direct／indirect array 的未壓縮 stream bytes |
| M6b | FlateDecode | 以 zlib 解壓單一 FlateDecode，並遵守 decoded-size limit |
| M7 | Content Interpreter | 執行 operand stack、BT/ET、文字 operators 與未知 operator policy |
| M8 | Text State / Geometry | 執行 text matrix、line matrix、CTM、q/Q/cm 與 spacing |
| M9 | Font Decode | 支援 Simple Font、ASCII、WinAnsiEncoding 與 width metrics |
| M10 | TextItem | 產生帶有 UTF-8、位置、寬度、font、page 的 text items |
| M11 | Reading Order / CLI | 排序 page/y/x、插入 spacing、完成 pdftext CLI 與 debug modes |

每個 milestone 都必須有 fixture、golden output、malformed input、build gate 與 sanitizer 驗證；不能只以「程式可以跑」作為完成條件。

M0–M7 已形成目前的 object、indirect object、traditional xref、resolver、頁樹、Contents 解碼與 Content Interpreter 基線；M8–M11 是後續規劃。目前 CLI 尚不能擷取完整 PDF 的文字。

## 3. M3：Indirect Object Parser

### 開工門檻

維持 [M2 已驗收的回歸基線](pdftext_m2_acceptance.md#9-m2-完成條件)（包含 `make test`、`make asan`），並先在 reader 驗證完整 PDF input 的 `%PDF-` header；M2 standalone object fixture 模式仍可獨立運作。

### 範圍

- 解析：

      INT generation keyword(obj) object keyword(endobj)

- 驗證 object number、generation 與關鍵字。
- 解析 indirect object 內的任意 M2 object。
- 解析 stream dictionary 與 raw stream bytes。
- stream 以 /Length 為準，不能使用 strstr 搜尋 endstream。
- `/Length` 支援 direct integer 與 indirect reference。遇到 reference 時，M3 透過呼叫端注入的 length resolver/callback 取得非負整數長度；M3 本身不做 xref lookup 或 object cache。沒有 callback、解析失敗或長度不合法時，必須明確回報錯誤。
- stream keyword 後接受 PDF 規定的 EOL，再依精確長度讀 raw bytes。
- 讀完 raw bytes 後才驗證 endstream 與 endobj。
- M3 fixtures 以可控的 reference-to-length mapping stub 驗證 indirect `/Length` 的精確讀流與錯誤路徑；M5 再接上真正的 object resolver。

### 不在範圍

- xref lookup。
- filter decode。
- object cache。
- xref stream 或 object stream。

### 實作與驗收

`--indirect input-file` 解析單一 indirect object fixture，輸出物件編號、body 與 raw stream 長度；若 `/Length` 是 reference，CLI 沒有 resolver，會回報 unsupported。呼叫 parser API 時可注入 resolver，`tests/indirect_test.c` 的 stub 和真實 PDF 中已知 offset 的測試涵蓋這條路徑。完整 PDF header 由 `reader_validate_pdf_header` 驗證；M3 尚未提供整份 PDF 文件的 CLI 解析模式。

M3 的驗收包含 `tests/fixtures.tsv` 的 indirect object golden cases、`tests/indirect_test.c` 的 direct/reference `/Length`、binary payload、錯誤路徑與 `tests/hello.pdf` 測試，以及 `make test`、`make asan`。既有 M2 fixture 模式持續通過。

## 4. M4：Traditional xref 與 trailer

### v1.0 必須支援

- 整合完整 PDF 文件時，再次套用 reader 的 `%PDF-` header 驗證。
- 從檔尾尋找 startxref。
- startxref 指向 traditional xref table。
- 一個 xref table 中的多個 subsection。
- free entry；object 0 保留且不可解析。
- /Size 驗證。
- trailer dictionary 與 /Root。
- xref entry 的 object number、generation、offset 驗證。
- %%EOF 存在，但允許 %%EOF 後有 trailing bytes。

### 明確拒絕

- xref stream。
- /Prev incremental update。
- object stream。
- 找不到或不合法的 startxref。
- /Encrypt。
- xref offset 指向不相符的 indirect object。

### 實作與驗收

`--dump-xref document.pdf` 會驗證 `%PDF-` header，從檔尾的 `startxref` 讀取 traditional xref table，支援多個 subsection、free entry 與 trailer dictionary，並逐筆核對 in-use entry 的 offset、object number 與 generation。輸出 `/Root`、每個 entry 與 trailer；xref 由呼叫端釋放，解析後 reader 游標回到原位。xref entries 有獨立的 `max_xref_entries` 上限。xref stream、object stream、`/Prev` 與 `/Encrypt` 會被拒絕。

`tests/hello.pdf`、多 subsection fixture、malformed offset、`/Prev` golden fixtures 與 `tests/xref_test.c` 涵蓋正常與錯誤路徑。M4 不負責 reference resolution 或 Pages Tree，這兩項由 M5 接續。

## 5. M5：Document、Resolver 與 Pages Tree

### Object Resolver

Document 由 reader、xref、trailer、object cache 與 limits 組成。cache entry 使用：

    UNLOADED → LOADING → READY
                        ↘ FAILED

- READY object 重複 resolve 時回傳 borrowed pointer。
- LOADING 時再次 resolve 同一 object，回報 indirect object cycle。
- free xref entry 被 resolve 時報錯。
- document close 統一釋放所有 cached object。

### Catalog 與 Pages

- trailer /Root 必須 resolve 到 /Type /Catalog。
- Catalog /Pages 必須 resolve。
- Pages node 依 /Kids source order 遞迴。
- 以實際 /Kids 結果為準，/Count 只作一致性檢查。
- Pages Tree cycle 必須報錯。
- /Resources 與 /MediaBox 從 Page 向父 Pages node 繼承。
- /Contents 缺少時視為空白 page。
- /MediaBox 經整棵樹仍找不到時報 malformed PDF。
- /Resources 缺失但 page 沒有文字內容時允許；需要 font 時才報錯。
- Page、Pages、Catalog 的 /Type 錯誤時報錯。

### 實作與驗收

`src/document.[ch]` 管理 reader、xref 與 indirect object cache；`pdf_resolve()` 核對 xref 身分、回傳文件擁有的物件並支援間接 `/Length`。`src/pages.[ch]` 依 `/Kids` 順序驗證頁樹、`/Parent` 與 `/Count`，計算每頁有效的 `/Resources`、`/MediaBox`，保留原始 `/Contents`。`--dump-pages document.pdf` 輸出頁序與屬性摘要，不解碼內容流。

`tests/document_test.c`、`tests/pages_test.c` 與 35 筆 golden fixtures 覆蓋正常、繼承、循環、錯誤型別與上限路徑；`make`、`make test`、`make asan` 已通過。

## 6. M6：Contents 與 Stream Decode

### M6a：未壓縮 stream

支援：

- /Contents 是單一 stream reference。
- /Contents 可直接是 array，或是指向 stream／array 的 reference；stream 本身必須是 indirect object。
- /Contents array 內的元素是 stream reference，依 source order 串接，stream 之間補 newline。
- /Length 是 direct integer 或 indirect reference。
- 沒有 /Filter 時直接使用 raw bytes。

### M6b：FlateDecode

- 使用 zlib，不自行實作 DEFLATE。
- 支援單一 /FlateDecode。
- 未知 filter 回報 unsupported PDF feature。
- filter array 與 filter chain 延後。
- 解壓後資料受 decoded stream limit 與 total decoded budget 限制。

### 實作與驗收

`src/contents.[ch]` 依頁序回傳 caller-owned decoded bytes，`src/filter.[ch]` 處理 raw 與單一 FlateDecode。`--dump-contents document.pdf` 只輸出每頁 decoded byte length；`tests/contents_test.c` 與 48 筆 golden fixtures 驗證正常、錯誤及上限路徑。`make`、`make test`、`make asan` 已通過。

## 7. M7：Content Stream Interpreter

採用 operand stack 與 operator dispatcher；Content parser 與 PDF Object Parser 分離。

### 必須支援的 operators

- BT、ET
- Tf
- Tm
- Td、TD、T*
- Tj、TJ
- apostrophe operator
- quotation-mark operator
- q、Q、cm

### 錯誤政策

- 完全未知的 operator：清除該次 operands，debug mode 警告。
- 已知但未實作的標準 operator 一律保守回報 unsupported（包括 BI、Do、gs、Tc、Tw、Tz、TL、Tr、Ts、path／clipping／color、marked content 與 compatibility operators）；不將它們當成完全未知 operator 略過。ID／EI 單獨出現、indirect reference 與 stream operand：malformed。
- 已知 operator operand 不足或型別錯誤：malformed content stream。
- text-positioning／text-showing operators 必須位於 BT/ET 內；text-state operators（如 Tf）可在外設定並跨文字物件保留。BT 重設 text matrix 與 line matrix（PDF Reference 1.7 §5.2–5.3）。
- q/Q 不平衡、BT/ET 不平衡或 nested BT：報 malformed content stream。q、Q、cm 僅可在 BT/ET 外。
- 不支援的 graphics feature 不得靜默產生錯誤座標。

### M7 完成狀態與交接

M7 已完成。`src/content_lexer.[ch]` 透過共用 lexer 的 borrowed-buffer 入口掃描 decoded bytes，content operand parser 獨立於 PDF Object Parser；不建立假 reader，也不接受 indirect reference／stream operand。Literal string 的 CR／CRLF 正規化為 LF，保留 NUL、octal escape 與 hex string 原始字元碼。

`src/content_interpreter.h` 的逐操作 visitor 提供已驗證的 typed operands 與 decoded offset；資料只在 callback 期間借用。M8 應依順序消耗操作並自行維護 text state／geometry；M7 不計算矩陣、glyph width 或 Unicode。後續操作失敗不回滾先前 callback，消費者須暫存頁面輸出。失敗時操作摘要清零；錯誤的檔案 offset 指向 Page／Contents，訊息與 `result.error_offset` 保留精確 decoded offset。

`--dump-content input.pdf` 依頁序輸出 `PAGES N` 與 `PAGE i OPS n TEXT_SHOWS n`；`OPS` 僅計入已支援並驗證成功的操作。全頁成功後才寫 stdout，後頁失敗也沒有部分摘要。完全未知操作僅透過 debug warning callback 通知，CLI 不回顯任意 operator／string bytes；既有 `--dump-contents` 仍只顯示解碼 byte length。

驗收：新增兩組 C 單元測試與 15 筆靜態 PDF fixtures（全套共 63 筆），涵蓋 raw／Flate／Contents array、原始 string bytes、型別／參數／狀態錯誤、未知／unsupported、token／container／nesting／q 限制、callback 中止與後頁失敗無 stdout。`make -B test`、`make -B asan` 全數通過，編譯器零 warning；ASan／UBSan／LeakSanitizer 在沙箱外完成。兩位獨立 reviewer 覆核無阻擋問題，另通過 100,000 組隨機 content input 的 sanitizer probe。

## 8. M8：Text State 與 Geometry

### 初始狀態

- BT 時 text matrix 與 line matrix 都是 identity。
- Tf 未設定前執行 Tj/TJ 報錯。
- Tf 的 font size 必須大於 0。
- 每個 BT 都重設 text object 內部狀態。

### Matrix

PDF affine matrix [a b c d e f] 使用：

    x' = a*x + c*y + e
    y' = b*x + d*y + f

- Tm 同時更新 text matrix 與 line matrix。
- Td 更新 line matrix，再令 text matrix 等於 line matrix。
- TD 等同 Td，並設定 leading = -ty。
- T* 等同 Td(0, -leading)。
- cm 更新 CTM。
- q/Q 至少保存與恢復 CTM。
- 所有位置計算使用 double。
- PDF 原始座標使用左下角原點。
- /MediaBox 提供 page bounds，不改變原始 text coordinates。

v1.0 不轉換 /Rotate；遇到非 0 page rotation 回報 unsupported page rotation。完整 rotation、vertical writing 與複雜 graphics state 延後。

## 9. M9：Font Decode

Raw PDF string 是編碼後的 bytes，不等於 UTF-8；文字必須經 font decode 才能產生 UTF-8 output。

### v1.0 支援

- Simple Font。
- ASCII byte 直接映射。
- /WinAnsiEncoding。
- /Widths 與 /FirstChar。
- 找不到 glyph width 時使用 /MissingWidth。
- UTF-8 output。
- 無法解碼的 byte 使用 U+FFFD，debug mode 可提示。

### v1.0 不支援

- /Differences。
- MacRoman。
- Symbol。
- ZapfDingbats。
- Type0。
- CIDFont。
- /ToUnicode CMap。

沒有可用 metrics 時回報 unsupported font metrics，不猜測固定寬度；暫不支援 kerning。

## 10. M10：TextItem

不要在 Tj 或 TJ 直接寫 stdout。第一版以文字顯示操作為粒度：

- 每個 Tj 產生一個 TextItem。
- TJ 中每個 string segment 產生一個 TextItem。
- TJ number 只調整位置。
- 不拆成單一 glyph。

每個 item 保存：

- decoded UTF-8 text
- text length
- 起始 x/y
- width/height
- font size
- font resource name
- page number
- source order

width 依 glyph width、font size 與 horizontal scale 計算。

## 11. M11：Reading Order 與 CLI

### Reading order

- page ascending。
- 不同行依 y descending。
- 同一行依 x ascending。
- 相同座標以 content source order 作為 tie-breaker。
- line tolerance：

      max(1.5, min(font_size_1, font_size_2) * 0.25)

- gap 大於 font_size * 0.25 才插入 synthetic space。
- 原始文字內已有的 space 必須保留。
- 不做 dehyphenation、ligature 展開或段落推測。

### 純文字輸出

- 同一行使用 newline 分隔。
- page 之間使用一個空白行。
- 整份輸出只保留一個結尾 newline。
- 沒有文字的 page 不輸出空白行。
- 整份 PDF 沒有文字時輸出 no extractable text layer。
- 正常文字只輸出 stdout。
- diagnostics 與 debug dump 輸出 stderr。
- 先暫存整份 page output，成功後才寫 stdout。
- unsupported font/filter 或 malformed page 會讓整份文件失敗；未來另以 --best-effort opt-in。

### CLI

    pdftext [options] input.pdf

支援：

- --help
- --version
- --dump-header
- --dump-xref
- --dump-trailer
- --dump-object N
- --dump-pages
- --dump-content
- --dump-text-items

一次只允許一個 dump mode；dump 與正常文字輸出互斥。

## 12. v1.0 發布範圍

v1.0 必須完成 M0–M11，並支援：

- C11。
- Linux。
- GCC 與 Clang。
- libc 與 zlib；qpdf 不列入正式 dependency。
- 未加密 PDF。
- traditional xref table。
- 無 incremental update。
- 原生文字層。
- direct/ref object 與 Contents array。
- 未壓縮 stream 與單一 FlateDecode。
- Catalog、Pages Tree、page inheritance。
- Simple Font、ASCII、WinAnsiEncoding。
- UTF-8 TextItem 與單欄水平 reading order。

v1.0 不支援：

- OCR。
- encryption。
- xref stream。
- object stream。
- incremental update。
- damaged PDF recovery。
- XFA/AcroForm。
- annotation text extraction。
- vertical writing。
- /ToUnicode。
- Type0/CIDFont。
- /Differences。
- page rotation。
- sophisticated table reconstruction。
- multi-column reconstruction。

## 13. v1.1 與後續

### v1.1 候選

- /ToUnicode 的 beginbfchar。
- /ToUnicode 的 beginbfrange。
- Type0/CIDFont。
- 中文與更完整 Unicode。
- page rotation。
- /Differences。
- 更多 Simple Font encoding。
- filter chain。
- 更完整的 glyph metrics。

### 更後期

- xref stream。
- object stream。
- incremental update 與 /Prev。
- encryption。
- damaged PDF recovery。
- vertical writing。
- multi-column layout。
- table reconstruction。
- OCR。
- XFA/AcroForm。
- annotation text extraction。

## 14. Common Contracts

### Error 與 exit code

    0  success
    1  CLI usage error
    2  I/O error
    3  malformed PDF
    4  unsupported PDF feature
    5  out of memory
    6  resource limit exceeded

stderr 訊息必須包含 module、byte offset（若可取得）與人類可讀原因。

### Resource limits

由 pdf_limits 集中管理：

| 項目 | 預設上限 |
|---|---:|
| 整體 PDF | 256 MiB |
| 單一 token/string | 16 MiB |
| nested depth | 256 |
| 單一 array/dictionary entries | 1,000,000 |
| object cache | 1,000,000 objects |
| xref entries | 1,000,000 entries |
| 單一 raw stream | 256 MiB |
| 單一 decoded stream | 256 MiB |
| 整份文件 decoded bytes（含 stream 間的 newline） | 256 MiB |
| page count | 100,000 |

所有容量成長、offset、length、numeric conversion 與 decompression 都必須檢查 overflow。

### Build 與 tests

    make
    make test
    make asan

編譯 flags：

    -std=c11 -Wall -Wextra -Wpedantic -g

Makefile 必須允許 CC、CFLAGS、LDFLAGS、LDLIBS 覆寫。測試使用 POSIX shell runner，不依賴 Python、qpdf 或其他外部 runtime dependency。Binary PDF fixtures 與 expected output 提交至 repository；malformed fixtures 也必須涵蓋。

## 15. Architecture Rules

1. 每層明定 input、output、ownership 與 error contract；高層透過低層 API 交接，不繞過其抽象直接取用內部資料。
2. Pages module 不直接處理 xref，一律透過 pdf_resolve()。
3. Content parser 不知道 WinAnsi、CID 或 CMap 細節。
4. Tj/TJ 不直接 printf，先建立 TextItem。
5. Stream 以 /Length 為主，不使用 strstr 找 endstream。
6. Parser 與 layout 分層。
7. 所有 malloc 都要能回答誰負責 free。
8. 每新增 object type 都同步更新 recursive free、dump、test。
9. 每層都提供可觀察的 debug dump。
10. 所有 parser error 都要能定位 byte offset。
11. 每個 milestone 先完成 fixture，再完成 implementation，再完成 golden/ASan 驗收。

## 16. Release Gate

只有下列條件全部滿足才算 v1.0：

- [ ] M0–M11 的 milestone acceptance 全部通過。
- [ ] legal fixtures 與 golden outputs 全部通過。
- [ ] malformed 與 unsupported fixtures 的 exit code 正確。
- [ ] make 零 warning。
- [ ] make test 通過。
- [ ] make asan 通過。
- [ ] v1.0 out-of-scope feature 不會被靜默當成成功。
- [ ] README、CLI help、error/exit-code 文件與本 roadmap 一致。
