# pdftext：架構與 Roadmap

> 本文件是架構、milestone 與版本邊界的權威來源。M2 Object Parser 已於 2026-08-24 通過 release acceptance（驗收記錄見 commit `d7913a5`）；M3–M11 已依序完成，各節末尾有完成狀態與驗收紀錄。2026-09-28 通過 [Release Gate](#16-release-gate)，版本為 v1.0.0。

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

M0–M11 已全部完成：`pdftext input.pdf` 依單欄、水平閱讀順序輸出 UTF-8 純文字，各層都有 debug dump 模式。

## 3. M3：Indirect Object Parser

### 開工門檻

維持 M2 已驗收的回歸基線（commit `d7913a5`，包含 `make test`、`make asan`），並先在 reader 驗證完整 PDF input 的 `%PDF-` header；M2 standalone object fixture 模式仍可獨立運作。

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
- Tf、Tc、Tw、Tz、TL、Ts、Tr
- Tm
- Td、TD、T*
- Tj、TJ
- apostrophe operator
- quotation-mark operator
- q、Q、cm

### 錯誤政策

- 完全未知的 operator：清除該次 operands，debug mode 警告。
- 已知但未實作的標準 operator 一律保守回報 unsupported（包括 BI、Do、gs、path／clipping／color、marked content 與 compatibility operators）；不將它們當成完全未知 operator 略過。ID／EI 單獨出現、indirect reference 與 stream operand：malformed。
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
- BT 只重設兩個文字矩陣；font、size、spacing、leading、hscale、rise、mode 與 CTM 跨 BT 保留。
- Tc/Tw/TL/Ts/Tz 接受有限數值；Tz 除以 100 儲存，不限制正負。
- Tr 必須是 0–7 的 integer；0–3 保留並產生事件，4–7 glyph clipping 回 unsupported。
- Tf size 有限且大於 0 是本專案 v1.0 限制。所有 text-show（含空字串／空 TJ）先要求合法 Tf。

### Matrix

PDF affine matrix [a b c d e f] 使用：

    x' = a*x + c*y + e
    y' = b*x + d*y + f

- Tm 同時更新 text matrix 與 line matrix。
- Td 更新 line matrix，再令 text matrix 等於 line matrix。
- TD 等同 Td，並設定 leading = -ty。
- T* 等同 Td(0, -leading)。
- compose(A,B) 表示先 A 再 B；cm 令 CTM = compose(M,CTM)。
- Td 令 Tlm = compose(translate(tx,ty),Tlm)，再令 Tm = Tlm；Tm 直接取代兩個矩陣。
- q/Q 保存 CTM 及全部 text-state parameters，不保存文字矩陣。
- rendering matrix = compose(compose([size*hscale 0 0 size 0 rise],Tm),CTM)。
- 初始 CTM identity；所有運算與結果必須 finite，溢位 malformed；允許 singular matrix。
- 所有位置計算使用 double。
- PDF 原始座標使用左下角原點。
- /MediaBox 提供 page bounds，不改變原始 text coordinates。

v1.0 不轉換 /Rotate；遇到非 0 page rotation 回報 unsupported page rotation。完整 rotation、vertical writing 與複雜 graphics state 延後。

### M8 interface 與驗收契約

`matrix.[ch]` 提供純矩陣運算；`text_state.[ch]` 以 M7 visitor 接入，snapshot 與 raw-string event 是借用資料。失敗後 context 只可 destroy；consumer 必須暫存輸出，後續失敗不回滾事件。

metrics callback 接收 length-aware font name 與單一原始 code byte，回傳 finite 的 width_1000。僅限水平 Simple Font；M9 負責真實 metrics adapter。非空字串缺 metrics 回 unsupported，正式 CLI 不猜固定寬度。每 glyph 推進 `(width/1000*size+Tc+(code==0x20?Tw:0))*hscale`；TJ number 推進 `-number/1000*size*hscale`。引號先換行；雙引號先保留 Tw/Tc 再換行顯示。每 string segment 保留獨立事件、rendering matrix、origin、advance vector 與來源位置，不稱作 glyph bbox。

font name 複製且 length-aware，q stack 共享 immutable storage；stack、容量、引用計數與來源計數檢查 overflow。頁面 Rotate 繼承、可間接引用，integer 且為 90 倍數；保留原值，geometry 入口拒絕任何非零值（含 360），錯誤定位 Page reference。詳細驗收與視覺證據要求見 [M8 plan](docs/m8-plan.md)。M8 已完成驗收。

### M8 完成狀態與交接

`matrix.[ch]`、`text_state.[ch]` 已實作上述契約。公開 snapshot 與 `pdf_text_event_dump` 提供安全 raw geometry 診斷；dump 以 thread-local C numeric locale 固定九位小數、font length／hex 與 raw bytes hex 輸出，不改 process locale。`pdf_text_page_interpret` 每頁初始化狀態，拒絕非零有效 Rotate；既有 CLI 摘要不使用測試 metrics。

`make -B test`、沙箱外 `make -B asan` 全部通過（ASan／UBSan／LeakSanitizer，未停用 leak detection），編譯器零 warning。65 筆 CLI fixtures 加上 matrix／Text State／Pages／整頁測試，覆蓋 raw／Flate／array 完整 trace golden、跨頁重置與無部分輸出、數值溢位、字型名稱含 NUL、q/Q 所有權、callback 失敗與 Rotate。獨立 subagent 完成契約與程式審查，所提 library dump 與視覺命令失敗檢查均已補齊。

兩份受控 PDF 共 16 個 string events 的實際 origin／advance／rendering matrix 與 raw bytes 通過手算對照，實際 PNG 與 overlay 已人工檢查；[視覺報告](output/pdf/m8-comparison/index.html) 與 [驗收紀錄](docs/pdf-visual-comparison.md#m8-完成驗收) 保存 expected／actual／delta、hash、命令與限制。hello.pdf 的 `w`、compilerbook.pdf 的 xref stream 仍 unsupported，實際 stdout 空白、exit 4。

M9 接 `pdf_text_metrics` 的單 byte 水平 Simple Font seam，自行持有 page Resources／document 並取得真實 widths；M8 不作 Unicode 解碼或 glyph bbox，不把測試 Courier-600 當正式 fallback。

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

### M9 支援政策（實作契約）

- **Simple Font 範圍**：水平、單 byte 的 `/Type1` 與 `/TrueType`。`/Type3`（FontMatrix／CharProcs 不適用 width_1000 seam）、`/MMType1`、Type0／CIDFont 與未知 subtype 為 unsupported。Font dictionary 需 `/Type /Font`、name `/Subtype`、name `/BaseFont`；缺欄位、錯型或 stream 當 dictionary 為 malformed。`/BaseFont` 為 Symbol／ZapfDingbats（含六個大寫字母加 `+` 的 subset prefix）為 unsupported；BaseFont 不當作 resource name 或 cache key。
- **Encoding**：`/WinAnsiEncoding` name，或 `/BaseEncoding /WinAnsiEncoding` 且無 `/Differences` 的 dictionary（`/Type` 若存在須為 `/Encoding`）。含 `/Differences`（即使空 array）、缺 BaseEncoding、MacRoman／MacExpert／Standard／其他 encoding 為 unsupported，錯型 malformed；明示的 unsupported encoding 不退回 ASCII。無 `/Encoding` 時採專案 ASCII fallback：0x20–0x7E 直接映射、其餘 U+FFFD，診斷標為 `ascii-fallback`，不宣稱讀懂 font builtin encoding。
- **WinAnsi → Unicode**：固定 256-code 表（PDF Reference Appendix D + Adobe Glyph List），不用 locale／iconv。0x80–0x9F 依 WinAnsi 字符；0x7F、0x81、0x8D、0x8F、0x90、0x9D 映成 U+2022；0x00–0x1F 為 U+FFFD。0xA0 保留 U+00A0、0xAD 保留 U+00AD，不做正規化。每 byte 一個 scalar，不展開 ligature、不做 normalization、kerning 或 synthetic space。
- **`/ToUnicode`、symbolic、embedded program**：font 有 `/ToUnicode` 即 unsupported；`/FontDescriptor /Flags` 若存在須為 integer，Symbolic bit（4）設定即 unsupported。不讀 FontFile／FontFile2／FontFile3、cmap 或 OS 字型；也不內建 Standard 14 AFM。
- **字寬**：`/FirstChar`、`/LastChar`、`/Widths` 三者同時存在，`0 <= FirstChar <= LastChar <= 255`，Widths 恰好 `LastChar-FirstChar+1` 個有限數值；部分缺少、錯型或長度不符為 malformed，三者皆缺為 unsupported font metrics。範圍內用 `Widths[code-FirstChar]`；範圍外若有 FontDescriptor，用 explicit `/MissingWidth`，省略則為規格 default 0（provenance `descriptor-default-zero`）；沒有 descriptor 為 unsupported font metrics。零與負寬度合法。字寬永遠以原始 code 查找，與 UTF-8 長度無關；M8 的 Tw 仍只施於原始 0x20。
- **Reference 與定位**：所有欄位可 direct 或 indirect，ref chain 以 `max_nesting_depth` 限制並偵測 cycle（malformed）。Font library 錯誤定位最近的 indirect object，否則 Resources／Page；整頁 bridge 由 M7 包成 Page offset，訊息保留 `decoded byte N: font byte M`。
- **輸出 budget**：單一 string 的 UTF-8 長度上限沿用 `max_token_size`；整份文件暫存 UTF-8 累積上限沿用 `max_total_decoded_size`，與 M6 decoded contents 各自計數；decoded event 總數跨頁以 `max_container_entries` 限制；resource name binding cache 亦以其限制。

### M9 完成狀態與交接

`src/font.[ch]` 以每頁 font context 解析 `/Resources /Font`（length-aware name、direct／indirect 欄位、有界 ref chain 與 cycle 偵測、transactional name／indirect-ref cache），提供常數時間 width（含 provenance）與 caller-owned UTF-8 decode。`src/font_text.[ch]` 是整頁 bridge：同一 M7 operation 序列交給 M8，成功 Tf／Q 後重新選取 active font，metrics 與 decode 使用同一 handle，輸出借用的 decoded event（raw geometry + UTF-8 + replacement count）；Rotate 檢查抽成共用 `pdf_text_page_check`。`--dump-content` 等既有 CLI 不變，未新增產品純文字模式。

驗收：`make -B test`、`make -B asan`（ASan／UBSan／LeakSanitizer）全部通過，編譯器零 warning。新增 font 單元測試（policy／error 分類與定位、ref cycle／depth、cache 與 token 限制、256 codes WinAnsi 全表、MissingWidth provenance）、bridge 整合測試（手算案例 1–6、q/Q 巢狀、跨 Contents stream、Tf 必驗證、文件級 UTF-8／event budget、consumer 錯誤保留）與 staged probe goldens；真實 adapter 下的 M8 fixtures raw geometry 與 M8 golden 逐 byte 相同。[視覺驗收](docs/pdf-visual-comparison.md#m9-完成驗收) 以 Poppler 實際渲染字型的 advance 與 cp1252 獨立核對 13 個 strings 的 bytes／Unicode／geometry，並人工查看 overlay。獨立 subagent code review 無 blocker，minor 項目已修正。

尚未覆蓋：allocation-failure injection（沒有 malloc fault 注入設施）；本機沒有逗號小數 locale，locale 測試會明示 skip。M10 應消費 `pdf_font_text_event`（複製所需欄位），不重新解析字型；glyph bbox、閱讀順序與純文字 CLI 仍屬 M10／M11。

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

### M10 TextItem 政策（實作契約）

- **來源**：每個 M9 decoded event 一個 item；raw bytes 長度為 0 的 string 不產生 item（其 Tf 仍由 M9 驗證），頁內 source order 因此可不連續。TJ 數字只影響後續 origin。
- **可見性**：Tr=3 不可見文字照樣產生 item 並保存 rendering mode；是否輸出屬 M11 政策。
- **位置與寬度**：起點 x/y 為 M8 origin（default user space，已套 CTM 與 rise）。保存帶號 advance 向量 `(dx,dy)`，`width = dx`，不取絕對值。
- **高度**：`em_height = hypot(c,d)`，即 rendering matrix 第二欄長度；等於 user space 的有效字級（含 CTM 縮放、不含 rise）。專案不讀 ascent／descent／FontBBox，em_height **不是 glyph bbox**。
- **字級**：保存 Tf 名目 `font_size` 與 `effective_size`（= em_height）；M11 的 line tolerance 使用 effective_size。
- **方向**：`horizontal = 1` 當 rendering matrix 的 `|b|`、`|c|` 不超過 `1e-9 × max(|a|,|d|)` 且 `a > 0`、`d > 0`；M10 不拒絕非水平 item，由 M11 決定。
- **其他欄位**：length-aware 複製的 font resource name（可含 NUL）、replacement 次數、M9 subtype／encoding 標記、1-based page、頁內 source order、全文件遞增 sequence、decoded operation offset。不保存 font handle 或任何 borrowed 指標。
- **所有權與失敗**：`pdf_text_items` 由 caller 擁有並持有全部 bytes；整份文件全有或全無，失敗時集合為空、保留原始 error。item 數受 `max_container_entries` 限制；UTF-8 總量沿用 M9 的 `max_total_decoded_size` 計數，font name bytes 另行計數並受同一上限。
- **CLI**：`--dump-text-items` 是 debug mode，整份成功後才輸出，每 item 一行 JSON（page／source order 排列，不是閱讀順序）；沒有 item 時 stdout 空且 exit 0。「no extractable text layer」只屬 M11 純文字模式。

### M10 完成狀態與交接

`src/text_items.[ch]` 消費 M9 bridge 的 borrowed decoded event，複製成 caller-owned 的 `pdf_text_items`：item 陣列加一塊 byte arena，建構期以 offset 暫存，全部成功後才轉成指標；失敗時集合歸零並保留原始 error。`pdftext --dump-text-items` 在記憶體中格式化全部 item，成功後一次寫出 stdout。JSON preview helper 移至 `pdf_font_json_preview` 共用；連結加入 libm（`hypot`）。

驗收：`make -B test`、`make -B asan` 全部通過，編譯器零 warning；CLI fixtures 由 65 筆增至 75 筆（10 筆 `items-*`：正常、無文字、後頁失敗、缺 resource、Rotate、hello.pdf）。新增 TextItem 測試涵蓋計畫的 6 個核對案例、負 Tc／負 Tz、名稱含 NUL、兩頁同名字型、200 items 的 arena growth、item／UTF-8／font-name 限制與全有或全無；M8 geometry fixtures 轉成 items 後與既有數值一致。[視覺驗收](docs/pdf-visual-comparison.md#m10-完成驗收) 以獨立矩陣計算與渲染字型寬度核對 13 個 items，並人工查看 em 框 overlay。兩個獨立 subagent：code review 無 blocker（含 realloc 逐一失敗注入 probe），minor 項目已修正；對抗式驗證跑約 4,600 份隨機 PDF 與約 31,000 個 item 的 Python oracle，零 sanitizer 報告、零差異。

交接 M11：消費 `pdf_text_items`（不重跑解析），以 page → 行（effective_size 為 line tolerance 基準）→ x 排序，source order／sequence 作 tie-breaker；需決定 Tr=3、`horizontal=0` 與 replacement 在純文字模式的政策，再實作 synthetic space 與正式 CLI。

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
- 正常文字只輸出 stdout（或 `-o` 指定的檔案）。
- `--dump-*` 的內容是該模式要求的輸出，寫 stdout；錯誤、警告與提示寫 stderr。
- 先暫存整份 page output，成功後才寫 stdout。
- unsupported font/filter 或 malformed page 會讓整份文件失敗；未來另以 --best-effort opt-in。

### CLI

    pdftext [-o output.txt] input.pdf
    pdftext MODE input.pdf

支援：

- --help、--version
- -o FILE：純文字寫入 FILE 而非 stdout，只能用於純文字模式
- --dump-header、--dump-xref、--dump-trailer、--dump-object N
- --dump-pages、--dump-contents、--dump-content、--dump-text-items
- --dump-objects（原本不帶選項的 M2 物件 dump）、--object、--indirect（單一物件語法除錯）

一次只允許一個模式；dump 與正常文字輸出互斥。

### M11 政策（實作契約）

- **CLI**：不帶模式即純文字模式。舊的無選項物件 dump 改名 `--dump-objects`，行為不變。`--help` 寫 stdout、exit 0；`--version` 印 `pdftext 1.0.0`。`--dump-header` 印 header 版本；`--dump-trailer` 印 trailer dictionary；`--dump-object N` 以 xref 的 generation 解析 object N。未知選項、缺檔名、多個模式、N 非正整數、`-o` 搭配 dump 模式、`-o` 重複：usage 寫 stderr、exit 1。
- **`-o FILE`**：在 FILE 同一目錄建立隱藏暫存檔（`.pdftext-XXXXXX`），完整寫入、fsync 並關閉成功後 rename 成 FILE；任何失敗刪除暫存檔、不改動原有 FILE，錯誤為 io（exit 2）。新檔權限依 umask。FILE 若是 symlink，會被替換成一般檔案（rename 語意），不寫入其指向的檔案。FILE 不可以 `-` 開頭（請寫 `./-name`），`-o` 出現在 `--` 之後視為檔名。stdout 不輸出文字。所有模式成功後若 stdout 寫入失敗（例如磁碟滿、pipe 關閉）一律 exit 2。
- **字級基準**：所有容差與門檻使用 M10 的 `effective_size`。
- **分行**：每頁內依 y 由大到小（同 y 依 sequence）排序後分群；item 與該行錨點（行內第一個 item）的 y 差不超過 `max(1.5, min(錨點字級, item 字級) × 0.25)` 即併入，否則開新行。行內依 x 由小到大，x 相同依 sequence。不偵測多欄；rise 不特別處理。
- **補空白**：同行相鄰 item 的空隙 = 後者 x −（前者 x + 前者 width）；大於前者 `effective_size × 0.25` 時插入一個 U+0020。前者以 U+0020／U+00A0 結尾或後者以其開頭時不插入；空隙不足或為負時直接相接。
- **文字內容**：UTF-8 原樣輸出（含 NBSP、soft hyphen、U+FFFD），不 normalize、不刪行尾空白。
- **Tr=3**：不可見文字照常輸出。
- **非水平文字**：任一 item `horizontal=0`（旋轉、鏡像、負 Tz）即整份 unsupported、exit 4，stderr 指出頁碼與 decoded offset，無任何文字輸出。
- **U+FFFD**：照常輸出；stderr 另印一行 `pdftext: warning: N undecodable bytes replaced with U+FFFD`，exit 0。
- **無文字**：沒有任何 item 時 stdout（或 `-o` 檔案）為空，stderr 印 `pdftext: no extractable text layer`，exit 0。
- **輸出格式**：每行後接 `\n`；頁與頁之間一個空白行；無文字的頁不輸出；整份以恰好一個 `\n` 結尾。整份組好後才寫出，大小受 `max_total_decoded_size` 限制。

### M11 完成狀態

`src/reading_order.[ch]` 依上述政策分行、排序、補空白並組成純文字，另提供行分群的 debug dump；`src/main.c` 改為表驅動的選項解析，加入 `-o` 原子寫入、`--help`、`--version`、`--dump-header`、`--dump-trailer`、`--dump-object N`，舊的無選項模式改名 `--dump-objects`。輸入不是一般檔案（例如目錄）時回報 io。所有模式成功後若 stdout 寫入失敗一律 exit 2。

驗收：GCC 與 clang 的 `make -B test`、`make -B asan` 全部通過，零 warning。CLI fixtures 由 75 筆增至 87 筆（12 筆 `text-*`），新增 reading order 單元測試（容差與門檻邊界、錨點不串接、同座標、rise、空白保留、重疊、多頁與空頁、輸出上限、非水平文字）與 CLI 行為測試（usage、help／version、`-o` 成功／失敗／長檔名／目錄／`-` 開頭、U+FFFD 警告、`/dev/full`、三種新 dump 模式）。[視覺驗收](docs/pdf-visual-comparison.md#m11-完成驗收) 以手寫預期文字核對閱讀順序，並與 Poppler `pdftotext` 參考並列。兩個獨立 subagent：code review 無 blocker，minor 項目已修正；對抗式驗證以 3,000 份隨機 PDF（約 148,600 items）對照獨立 Python 實作逐 byte 一致，另有 6,600 份損壞輸入與約 55 種 CLI 參數組合，零 sanitizer 報告。

已知限制：不偵測多欄；上下標超過行容差會自成一行；剛好等於 ¼ em 的間距不補空白；排序依浮點座標，極小的數值誤差可能改變邊界上的分行。舊的除錯模式（`--dump-xref`、`--dump-objects` 等）是串流輸出，失敗前可能已印出部分內容；全有或全無只保證於純文字、`--dump-content` 與 `--dump-text-items`。

## 12. v1.0 發布範圍

v1.0 必須完成 M0–M11，並支援：

- C11。
- Linux。
- GCC 與 Clang。
- libc（含 C 標準 math library libm）與 zlib；qpdf 不列入正式 dependency。
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

額外可觀察驗收須保存實際 PDF 渲染截圖與本專案實際解析 trace／輸出的對照，區分來源 bytes、座標、Unicode 與閱讀順序，unsupported PDF 記錄真實拒絕點。現有基線見 [PDF 畫面與解析對照](docs/pdf-visual-comparison.md)，M8 的詳細判定與座標慣例見 [M8 計畫](docs/m8-plan.md#pdf-渲染截圖與真實解析對照驗收)。Poppler／Python 等工具只用於額外視覺報告，不成為核心 make test／make asan 的必需依賴。

編譯 flags：

    -std=c11 -Wall -Wextra -Wpedantic -g

Makefile 必須允許 CC、CFLAGS、LDFLAGS、LDLIBS 覆寫。測試使用 POSIX shell runner，不依賴 Python、qpdf 或其他外部 runtime dependency。Binary PDF fixtures 與 expected output 提交至 repository；malformed fixtures 也必須涵蓋。

## 15. Architecture Rules

1. 每層明定 input、output、ownership 與 error contract；高層透過低層 API 交接，不繞過其抽象直接取用內部資料。
2. Pages module 不直接處理 xref，一律透過 pdf_resolve()。
3. Content parser 不知道 WinAnsi、CID 或 CMap 細節。
4. Tj/TJ 不直接 printf；M8 先產生 raw geometry event，M10 才建立 TextItem。
5. Stream 以 /Length 為主，不使用 strstr 找 endstream。
6. Parser 與 layout 分層。
7. 所有 malloc 都要能回答誰負責 free。
8. 每新增 object type 都同步更新 recursive free、dump、test。
9. 每層都提供可觀察的 debug dump。
10. 所有 parser error 都要能定位 byte offset。
11. 每個 milestone 先完成 fixture，再完成 implementation，再完成 golden/ASan 驗收。

## 16. Release Gate

只有下列條件全部滿足才算 v1.0：

- [x] M0–M11 的 milestone acceptance 全部通過。
- [x] legal fixtures 與 golden outputs 全部通過。
- [x] malformed 與 unsupported fixtures 的 exit code 正確。
- [x] make 零 warning。
- [x] make test 通過。
- [x] make asan 通過。
- [x] v1.0 out-of-scope feature 不會被靜默當成成功。
- [x] README、CLI help、error/exit-code 文件與本 roadmap 一致。

2026-09-28 全部勾選：M0–M2 見 commit `d7913a5` 的 M2 驗收記錄，M3–M11 見各節完成狀態；87 筆 CLI fixtures 與全部單元／golden 測試通過；malformed／unsupported 的 exit code 由 fixtures 驗證；GCC 與 clang 零 warning；`make test`、`make asan` 通過；範圍外功能（xref stream、非零 Rotate、非水平文字、`/ToUnicode` 等）回報 unsupported 而非成功；[README](README.md)、`--help` 與本 roadmap 一致。`--version` 為 `pdftext 1.0.0`。
