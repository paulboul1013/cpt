# M9 實作計畫：Font Decode 與真實 Glyph Metrics

## 目標與授權範圍

本文件是下一輪實作計畫，本輪只寫計畫與交接，不開始 C 程式實作、不 commit。權威範圍是 [roadmap M9](../pdftext_roadmap.md#9-m9font-decode)、[Common Contracts](../pdftext_roadmap.md#14-common-contracts) 與 Architecture Rules；目前 M8 已提交，現行介面以 [text_state.h](../src/text_state.h)、[pages.h](../src/pages.h)、[document.h](../src/document.h) 為準。

M9 要把頁面的 font resource name 解析成真正的 Simple Font，取得字形寬度給 M8，並把同一個字型下的原始 byte strings 轉成 caller-owned UTF-8。字寬查找與 Unicode 解碼是兩個獨立功能：用原始 code 查 `/Widths`，不能依解碼後的 Unicode 或 UTF-8 byte count 計算 advance。

本次交付範圍是 font library、M8 的正式 metrics adapter、staged 整頁診斷 pipeline 與可重跑驗收。M10 TextItem、M11 阅读順序／純文字 CLI 延後。M7 `--dump-content` 的摘要及拒絕政策保持；本階段以 library debug dump 與 integration probe 觀察 M9，不新增產品純文字模式。現有 M8 Courier-600 測試 adapter 保留作獨立回歸，新增 M9 pipeline 必須讀實際 font dictionaries。

驗證延續已授權的建置、單元／整合測試、sanitizer 與 PDF 渲染／對照；平台要求權限核准時按工具流程處理。開工先讀 AGENTS.md 並檢查 `git status --short`，保留既有未追蹤檔案。計畫不授權 push 或新的 commit；若下一輪使用者要求實作，按下列順序推進。

## 實作前固定的支援政策

以下是 roadmap 尚未充分定義的 **建議實作政策**。任務 1 先將其寫回 roadmap M9；不要在程式內暗中決定。M9 在全套數值、字型與視覺驗收完成前不標示完成。

| 項目 | 本次計畫的政策 |
|---|---|
| Font subtype | 水平、單 byte 的 `/Type1` 與 `/TrueType`；`/Type3`、`/MMType1`、Type0/CIDFont 先 unsupported。Type3 的 FontMatrix／CharProcs 不適用目前固定 width_1000 seam。這是把「Simple Font」範圍具體化，需同步 roadmap。 |
| Font dictionary | `/Type /Font`、合法 name `/Subtype`、name `/BaseFont`；缺必要欄位、錯型或把 stream 當 dictionary 回 malformed。未知 subtype 回 unsupported。 |
| 字型名稱 | `/BaseFont` 不當作 page resource name；`/F1` 一律查有效 `/Resources /Font`。Symbol／ZapfDingbats（含合法六個大寫英文字母加 `+` 的 subset prefix）明確 unsupported。 |
| `/Encoding /WinAnsiEncoding` | 支援固定、可核對的 256-code Unicode 表，不用 locale、iconv 或系統字型推測。 |
| `/Encoding` dictionary | 支援有 `/BaseEncoding /WinAnsiEncoding` 且沒有 `/Differences` 的 dictionary；其 `/Type` 若存在須為 `/Encoding`。有 Differences 即使空 array 也 unsupported。缺 BaseEncoding 的 builtin 推導先 unsupported。 |
| 沒有 `/Encoding` | 保留 roadmap 的有限 ASCII 政策：可列印 codes 0x20–0x7E 直接映射，其餘 U+FFFD。這是 v1 專案 fallback，**不是判定任意 font builtin encoding 等於 ASCII**；診斷列出 `ascii-fallback`，不能宣稱完整 builtin 支援。 |
| 其他 Encoding | MacRoman、MacExpert、StandardEncoding、未知 predefined encoding unsupported；錯型 malformed。明示的 unsupported encoding 不退回 ASCII。 |
| `/ToUnicode` | Font 上出現此欄位即 unsupported，不能忽略後產生貌似成功的替代文字；本次不讀 CMap stream。 |
| `/FontDescriptor /Flags` | 欄位若存在須是 integer；Symbolic bit 設定的字型先 unsupported。不要把已明示 symbolic 的字型走 Latin fallback。若需額外一致性檢查，範圍先寫回 roadmap，不引入 font-program parser。 |
| Embedded font program | 不解析 FontFile/FontFile2/FontFile3、outline、cmap 或 OS 字型。字典內的可用 encoding／widths 足夠時可處理，不能因 embedded stream 存在就宣稱讀過它。 |
| 合法零／負字寬 | 所有提供的 widths 必須 finite；本計畫不新增正值限制。零寬度是合法值，與「沒有 metrics」分開。 |
| Standard 14 metrics | 本次不內建 AFM table、不按 BaseFont 猜 width；只有 font name 沒有寬度資料仍 unsupported font metrics。既有受控 Courier fixtures 有明示 Widths，須由真實 adapter 讀取。 |

官方參考僅用來校對規格細節；以上 Type3、builtin encoding、缺 metrics 等支援取捨是專案政策。參考 [Adobe PDF Reference §5.5、§5.7、Appendix D](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.5_v6.pdf)；glyph names 的 Unicode 校對可用 [Adobe Glyph List](https://github.com/adobe-type-tools/agl-aglfn/blob/master/glyphlist.txt)。若採用或衍生 Adobe table 資料，保留來源版本與其授權 notice；不用 runtime 外部依賴。

### WinAnsi 的 Unicode 政策

可列印 ASCII 0x20–0x7E 直接映射；0x80–0x9F 依 PDF WinAnsi 的字符表處理，包含 Euro、smart quotes、dash、ligatures 等。0x7F、0x81、0x8D、0x8F、0x90、0x9D 依 PDF 的未分配 code 規則映成 U+2022 bullet，不能沿用一般 CP1252 decoder 的 undefined 結果。0x00–0x1F 在此 supported encoding 中視為無法解碼，每個 byte 產生一個 U+FFFD，不把 content string 內的 LF 自動變成排版換行。

重複 glyph codes 保留其 Windows 文字語意：0xA0 → U+00A0（nonbreaking space）、0xAD → U+00AD（soft hyphen），不在 M9 正規化成普通 space 或刪除；其 width 仍按原始 code 查找。M8 的 Tw 仍只施於原始 0x20，不因解碼成空白或 NBSP 而改變。不要套用 PDFDocEncoding，它與 content font encoding 是不同用途。

UTF-8 output 是 `{data,len}`，可有 caller-friendly 結尾 NUL，但 length 不含 terminator；不得用 strlen 當文字長度。每 byte 映射至一個 Unicode scalar 或 replacement；不做 ligature 展開、normalization、kerning、閱讀順序或 synthetic space。

### 字寬與 fallback

`FirstChar`／`LastChar` 為 integer，驗證 `0 <= FirstChar <= LastChar <= 255`。完整 Widths array 必須恰好 `LastChar-FirstChar+1` 個有限 integer／real；驗證範圍後才轉成 size_t、做減法或 index。code 在範圍內：`Widths[code-FirstChar]`；width 使用 1000 units，交給 M8 原有推進公式。

- 三個欄位部分缺少、錯型、array 長度不合或元素非有限 → malformed。短 Widths 不是可拿 MissingWidth 修補的合法個別缺值。
- 三個欄位全部缺少 → unsupported font metrics；即使 BaseFont 是 Courier，也不提供固定寬度 fallback。不能只靠一個 MissingWidth 將整個缺 metrics 的 font 誤當成已支援。
- 有完整 Widths、code 在範圍外 → 使用解析後 FontDescriptor 的 MissingWidth。
- Descriptor 存在而 MissingWidth 省略：使用規格的 default 0，debug provenance 為 `descriptor-default-zero`。這個特定 fallback 不等於無條件補零；Widths 本身仍要完整且合法。
- Descriptor 不存在且需要範圍外寬度 → unsupported font metrics；explicit MissingWidth 錯型／非有限 → malformed。explicit 0 必須保留並標示來源，不能用零值判斷 absent。

這些是提取需要的欄位驗證，不在 M9 實作整份 font dictionary 的完整規格相容性稽核。例如已有完整、可用 Widths 時，不因既有可渲染 fixture 未提供所有 descriptor 欄位而破壞 M8 回歸。

## Font module 的 interface 與所有權

建議新增 `src/font.[ch]`，整頁串接若需要獨立模組則新增 `src/font_text.[ch]`；內部 Unicode table 可用私有 `font_encoding.h`，避免對外暴露兩套解碼 interface。

Font module 在小 interface 後集中 resource lookup、reference resolution、欄位驗證、parsed-font cache、width lookup 與 UTF-8 decoding。M7 不讀 encoding；M8 不讀 font dictionaries、不重算矩陣；main／test consumer 不重複解析字型。

### 最小對外 surface（名稱在任務 1 固定）

- **Page font context create/destroy**：借用 document 和有效 page Resources、複製 limits、保存 page reference offset；document 必須活到 context destroy。每頁不同 context，避免跨頁 `/F1` 別名碰撞。
- **Resolve resource name → opaque immutable font handle**：輸入 length-aware bytes；handle 借用到 context destroy。第一次成功才發布 cache entry；失敗不留半初始化 font。資源名可含 NUL，使用 `pdf_dict_get_bytes()`，不得 C-string lookup。
- **Width(handle, raw code) → finite width_1000**：常數時間查已解析的最多 256 項表；沒有 fallback 資料才 unsupported。可用只讀診斷 enum 表示來源 `widths`／`missing-width`／`descriptor-default-zero`。
- **Decode(handle, raw bytes) → caller-owned UTF-8 result**：result 初始須為空，成功轉交 ownership，失敗為空；另保存 replacement count，destroy/free 接受空 result。
- **Read-only font diagnostic dump／snapshot**：列 font name length+hex、subtype／encoding policy、range、width provenance 與 replacement count；不輸出未 escaping 的 BaseFont、任意 raw bytes 或 terminal control bytes。

Source offsets 用獨立 metadata 保存，不留 parser token 指標。Font field 的 indirect reference 透過 `pdf_resolve()`；至少覆蓋 `/Font` dictionary、font dictionary、Encoding dictionary／name、Widths array、FontDescriptor 的 direct／ref；數值欄位若為 ref 也由同一個受限 helper 解到所需 leaf type。Ref-to-ref traversal 用 bounded depth + cycle 檢查，因目前 `pdf_resolve()` 返回 body 而不替呼叫者追完任意 reference chain。避免重寫 xref 或接觸 document cache 內部。

Parsed font handle 存固定大小 width／mapping 資料和需診斷的 borrowed document objects 或 owned names，二者契約分清；不用為 256 widths 建 generic graph。Binding cache 依 name bytes，容量受 `max_container_entries`（且 checked growth）；不同 aliases 指向同一 indirect font 時可共用 parsed handle，但不可僅用 BaseFont 當 cache key。Cache 的 lookup 只在字型切換／解碼入口發生，glyph hot path 不反覆線性掃 resource dictionary 或 resolve。

### 頁面串接與 Tf 的驗證時機

新增 page bridge 消耗 M7 的同一個 typed operation sequence，將 operation 交給 `pdf_text_state_visit()`。**每次成功 Tf 都解析／驗證對應 font**，即使後續只顯示空字串、空 TJ 或 numbers-only TJ，unsupported 字型也不能漏過；未使用的 Resources font entries 不主動載入。

在 Tf 成功後與 Q 成功恢復後，讀取 M8 snapshot 的當前 font name，選取該 page context 的 font handle；font 未設定時 active handle 為空。q 不另做第二份 text state。Metrics adapter 把當前 active handle 的 width 交回 M8；font name 是 M8 seam 的輸入，但 bridge 保證 handle 與 state 一致，包含 Q、跨 BT、跨 Contents streams。不得保存 snapshot 內借用名稱到下一次操作，resolve 時需要的資料由 font context 自行複製。

M8 raw event consumer 在 callback 期間用相同 active handle 解碼，包成 borrowed decoded event（原 raw geometry event + UTF-8 bytes/len + replacement count）再交給下游；callback 後釋放 UTF-8 暫存。需保存的 consumer 複製；失敗不發布該 string 的 decoded event，先前事件不回滾。這仍是 raw geometry + decode 診斷 event，不能命名成 M10 TextItem 或宣稱計算 glyph ink bbox。

Bridge 自行 create/destroy M8 state，呼叫 `pdf_content_interpret()`；其合理來源是已串接的 M6 Contents。若需共用 Rotate 檢查，將 M8 page entry 的既有政策抽成小 helper 或等效可測驗證，不能繞過非零 Rotate 拒絕；不改 M7 grammar 狀態模型。

### Error 與 failure lifecycle

沿用 first-error-wins 與現有 error codes。Font library 單獨使用時，錯誤定位最接近的 indirect field／font reference；direct 欄位退回其所在 font／Resources object，沒有可用位置時用 Page reference，不捏造精確 token offset。

M7 現行會把 visitor error.offset 改成 operation decoded offset，再於外層包成 Page／Contents file offset。因此 bridge 用 **local temporary pdf_error** 接 font 的檔案錯誤，向 visitor error 轉交相同 code，訊息包含原始 `font byte N`／原因；decoded operation 與 Page file offset 仍由 M7 唯一轉換。不能把 font file offset 當 decoded offset。測試 standalone 與 integrated 兩種定位、resolver errors 及 message 截斷後的可理解性。

| 條件 | Error |
|---|---|
| 缺 Resources/Font 或 Tf 找不到 binding、必要 field 錯型、壞 ref／cycle、壞 Widths | malformed |
| unsupported subtype/encoding、Differences/ToUnicode、沒有可用 metrics | unsupported |
| decode 中個別無對應 raw byte | 成功，U+FFFD + replacement count |
| length/capacity/count overflow、configured budget | resource-limit |
| allocation failure | out-of-memory |
| callback 中止 | 保留 callback 首個 error；未設 error 則 malformed |

Page bridge 任何失敗後只可 destroy；UTF-8 結果失敗清空，cache 更新 transactional。Standalone font context 失敗後的使用契約亦在 `.h` 固定，建議保守採 destroy-only，避免 failed resolver cache 狀態改寫原錯誤分類。

### 資源限制

- 不新增分散常數：context/cache、event collector 使用 `max_container_entries`；name/token 用既有 `max_token_size`；reference chains 用 `max_nesting_depth`，resolver cache 用既有限制。
- UTF-8 單一 string output length 上限沿用 `max_token_size`（是 decoded UTF-8 長度，與 input byte 長度分別檢查）。最壞每 code 四個 bytes、實際支援表最多三個；乘法、加 terminator、capacity growth 必須 checked，不因目前表小就省略。
- 整份文件暫存的 UTF-8 payload 累積上限沿用 `max_total_decoded_size`，有獨立 counter，不與 M6 context 的原始 decoded content counter互相挪用。這是 M9 輸出 budget 的新增契約，任務 1 寫回 roadmap；event 總數也跨頁限制。
- 空 decode 成功，不呼叫 widths；page bridge 仍已在 Tf 驗證 font。Mid-string OOM／limit、consumer abort、後頁失敗必須 free 所有 owned bytes，沒有部分 stdout trace。

## 任務與驗收順序

### 任務 1：固定 contracts 與第一份真實 font fixture

**工作**：將上面的 subtype、Encoding、Width fallback、UTF-8 budget 與診斷輸出政策寫回 roadmap；定義 font／decoded-event `.h` interface。建立最小可渲染 Type1 ASCII PDF，含有效 Resources、explicit Widths，測試 font resource lookup 與 width/decode seam。先跑基線，新增行為測試先失敗再實作。

**驗收**：每個 allocation 有 owner、每個 failure 有 offset；不依賴測試 Courier adapter也能定義成功路徑；新 snapshot/dump 格式避免 terminal injection。baseline `make -B test` 通過。

**依賴**：無。**檔案**：roadmap、`font.h`、`font_test.c`、首批静態 fixtures／Makefile；中等。

### 任務 2：Resource resolution 與 Type1 explicit Widths 垂直切片

**工作**：Page font context create/destroy、byte-aware binding 查找、解析 Type1 font 與 complete width table；支援必要 direct/ref leaf resolution，配置成功後才入 cache；以 ASCII printable mapping 完成第一個 font 的 width/decode。

**驗收**：FirstChar 非零、邊界 code、合法 width=0、name 含 NUL；missing binding／錯型／ref-cycle／range-overflow／length mismatch 分類與 offset 正確；repeated failure/destroy 無洩漏。相同 `/F1` 在兩頁可指向不同 widths。

**驗證**：font 單元／fixture 測試與 `make -B test`。**依賴**：1。**檔案**：`font.[ch]`、`font_test.c`、fixtures；中等。

### 任務 3：MissingWidth 與不可用 metrics

**工作**：Descriptor resolution、explicit MissingWidth／default 0 的 provenance，FirstChar/LastChar 外的 lookup；保守區分全部 absent、partial／malformed 和 unsupported。

**驗收**：in-range table width 優先，out-of-range fallback；descriptor default-zero 與 explicit-zero 可辨；沒有 descriptor 的 out-of-range unsupported；短 array 不能補，非有限值 malformed，standard14 無資料 unsupported。

**驗證**：font 邊界測試與 `make -B test`。**依賴**：2。**檔案**：font module、測試與小 fixtures；小／中等。

**檢查點 A**：評估 font module 是否集中 resolution／metrics／所有權，沒有 glyph hot-path 的重複 parse；覆核每種缺資料與錯誤位置，不擴充成完整字型引擎。

### 任務 4：WinAnsi／UTF-8 與 TrueType 字典支持

**工作**：固定可追溯 WinAnsi table、UTF-8 encoder、replacement count、bounded output；加入 Encoding dict/direct/ref 與 TrueType explicit encoding 路徑。Type3／MMType1、Differences、ToUnicode、Symbol 等拒絕有明確測試。

**驗收**：256 codes 的分類全覆蓋；Euro、smart quotes、é、Œ、bullet aliases、NBSP／soft hyphen 的 UTF-8 bytes 正確；raw NUL／控制 byte 依政策 replacement，空字串有效。Decode 的 UTF-8 byte length 增加不影響 metrics 查找。

**驗證**：font 解碼／budget 測試與 `make -B test`。**依賴**：3。**檔案**：font module、私有 table（若必要）、font tests、來源 notice；中等。完整字元表採靜態測試資料，不在 runtime 下載。

### 任務 5：真實 metrics adapter 與 M8 page bridge

**工作**：接 M6 → M7 → page bridge → M8；成功 Tf 驗證 font、Q 重新選取 handle；width callback 使用真正 table，raw event consumer 使用同一 handle 解碼。Bridge owned state/context 單一路徑 cleanup，輸出借用 decoded event。

**驗收**：變寬字型、q/Q 切換 font、font size 改變、BT 保留、跨 Contents stream、raw／Flate／array 幾何一致；空 Tj／空 TJ／numbers-only TJ 仍不能漏掉 unsupported Tf；未用的 unsupported resource 不妨礙成功。非 ASCII 和 replacement 用 raw code widths 推進，沒有 Unicode count 推進或假寬度。

**驗證**：`font_text_test.c` 整頁測試、M8 goldens 回歸與 `make -B test`。**依賴**：4。**檔案**：`font_text.[ch]`、其 `.h`、整頁 test、Makefile；中等。

**檢查點 B**：font dictionaries、Unicode decoding 與矩陣推進責任清楚；standalone font error 與 wrapped decoded error 兩種定位均有測試；沒有第二套 q stack／content parser。

### 任務 6：Staged diagnostics 與整份文件限制

**工作**：library font／decoded-event 的安全 snapshot/dump，新增 integration probe 與 POSIX golden runner；collector 暫存整份文件，所有頁成功才輸出 source-order diagnostic trace。把原 raw geometry、UTF-8 hex、escaped 預覽、replacement count／encoding／width provenance 分開標記。

**驗收**：後頁 invalid font／unsupported encoding、mid-string decode allocation/budget failure、metrics failure、consumer abort 都清空 trace；跨頁累積 bytes/events 上限確實生效；不同 locale、binary names、control bytes 輸出安全且格式穩定。既有 --dump-content／--dump-pages／--dump-contents goldens 保持。

**驗證**：probe goldens、限制與 cleanup tests、`make -B test`，單項 sanitizer。**依賴**：5。**檔案**：font/bridge debug、integration probe、runner／goldens、Makefile；拆成 debug 與 collector 兩小步。

### 任務 7：實際 PDF 畫面／Unicode／Geometry 對照與 release acceptance

**工作**：沿用 [M8 report workflow](../output/pdf/m8-comparison/README.md)，新增 `output/pdf/m9-comparison/`，更新 [視覺驗收文件](pdf-visual-comparison.md)。用真實 font adapter重跑既有 Courier fixtures，新增 renderable 非等寬 font 的 ASCII 與 WinAnsi 特殊字元頁。靜態 fonts fixture 的 widths 必須与其實際渲染字型資料一致（可挑標準 Helvetica 並以可追溯官方 AFM 值明示 Widths，或提交可合法分發的 embedded fixture）；不要把測試預期寬度隨意寫進 `/Widths` 冒充視覺證據。

**驗收**：保存來源 hashes／page attributes／renderer versions／DPI／原始 PNG，專案實際 UTF-8 trace／origin overlay 與獨立 expected/actual/delta；字距／方向／Unicode 分項通過。Renderer/reference command 失敗時停止，不能用舊 PNG。現有 hello／compilerbook 的空 stdout／exit／停止階段再次記錄，不因 M9 改為假成功。

**驗證**：`make`／`make -B test`／`make -B asan` 全通過、編譯器零 warning，必要 code review 覆核 contracts／ownership／M10 seam；人工查看實際 PNG 與 overlay後才標示 M9 完成。**依賴**：6。**檔案**：小 static fixtures／goldens、報告 scripts/evidence、視覺文件、roadmap；把 fixtures、report 與 acceptance 分次驗證。

## 必須能獨立核對的案例

1. FirstChar=65、LastChar=67、Widths=[600 650 700]：raw codes 65/66/67 查到 600/650/700；size=12、spacing=0、scale=1 時 advance 為 7.2/7.8/8.4，字串總 advance=23.4。這組用於数值單元測試，不直接當未知字型的視覺 expected。
2. 完整 range 外的 0x20 使用 explicit MissingWidth=250，size=12 加 Tw=3 時該 byte advance=6；若是 0xA0，其 fallback width 相同但不加 Tw，advance=3。兩者解碼結果也保留不同 scalar。
3. WinAnsi raw `41 80 E9` → UTF-8 `41 E2 82 AC C3 A9`，raw code count=3、UTF-8 len=6；width 不依 output len 改變。
4. WinAnsi `7F 81 8D 8F 90 9D` 每個產生 U+2022；raw `00` 產生 U+FFFD。每個 width 仍由原始 byte 查表。
5. 同 page q/Q 的 F1/F2 切換與兩頁 `/F1` 指向不同 font dictionaries：UTF-8 encoding與各 string origins 都依各自 state/resources；不以 BaseFont/name 單獨做全文件 cache key。
6. 先成功幾個 decoded events，後頁遇 `/ToUnicode` 或 missing font、或總 UTF-8 budget 超限：整份診斷 stdout 必須空，且 error 保留原始 cause，不因 cleanup 或 repeated resolve 改寫。

## 視覺驗收的判定與限制

- 完整 M9 實際資料須來自正式 font adapter；保留 M8 test-adapter trace 作回歸，但報告清楚標示兩者身份。
- UTF-8 逐 byte 比較，含 NBSP/soft hyphen/replacement；不能只 whitespace normalization 就宣稱 Unicode 或空白保真。Poppler 對 Unicode 的提取可有不同政策，差異依本計畫的 encoding 規則及原始 bytes 分析，不用外部結果覆寫專案 output。
- 數值容差沿用 absolute 1e-8 + relative 1e-9，人工 PNG overlay 2 px；origin/advance 不是 ink bbox。字寬至少有多個不同值，真正證明查 Widths 而非固定值。
- 不可見 Tr=3 raw/decoded event 仍保留，畫面無 glyph；不能視為 extraction failure。Reading order 尚未實作，source-order trace與 renderer 的排版順序不強行相等。
- Poppler/Python／font fixture 生成工具只用於額外 evidence；核心測試仍只依賴 C11 工具鏈、libc、zlib、POSIX shell。所有核心 PDF/expected bytes 都是 repo 靜態檔。

## 主要風險與處理

| 風險 | 处理 |
|---|---|
| Encoding 缺省與 builtin/custom font 被誤當 WinAnsi | ASCII fallback 明列限制、symbolic 拒絕，擴充 built-in/StandardEncoding另排；本計畫不宣稱完整 font program 支援。 |
| 把 PDF WinAnsi 當通用 CP1252 | 固定全部256 codes 的策略，bullet aliases 與 control codes 有獨立測試，保存來源／notice。 |
| MissingWidth=0 與 absent 混淆 | 顯式 presence/provenance；合法 default-zero只用於有效 descriptor及完整 Widths 之外。 |
| 同字型名跨頁 collision、Q 後 adapter handle 不一致 | Context 每頁、Q 後 snapshot refresh、兩頁同名不同字型與巢狀 q/Q 測試。 |
| Resolver錯誤的 file offset 被當成 decoded offset | temporary font error + adapter wrap，保留 font byte in message，由 M7 唯一包 Page/decoded定位。 |
| UTF-8 擴張導致過量 allocation／部分輸出 | checked growth、單 string及整份文件 budgets、transactional decode與整份 staging、sanitizer cleanup。 |
| 為現有 hello/book 提早擴充其他層 | w/marked content/xref stream等限制仍報 unsupported，保存真實截圖與錯誤，不偏離 M9。 |

本輪不產生 M9 passed 記錄。開始實作時重新確認基線、依任務1校正 roadmap、分段驗證；完成後才補 M9 完成狀態與 M10 handoff。

規劃 review：獨立 subagent 唯讀核對 roadmap 與現有 M8／document／pages 介面，未發現阻擋實作的問題；Type3/MMType1 等支援界線與新增 UTF-8 budget 仍須依任務 1 同步 roadmap。這是計畫 review，不是 M9 程式驗收。

## 實作紀錄

任務 1–7 已依序完成，現行契約寫在 [roadmap M9](../pdftext_roadmap.md#m9-支援政策實作契約) 與 `src/font.h`、`src/font_text.h`；完成狀態、驗收與剩餘缺口見 roadmap「M9 完成狀態與交接」。與計畫的差異：名稱依計畫建議固定為 `pdf_font_context_*`／`pdf_font_width`／`pdf_font_decode`／`pdf_font_text_page_interpret`；WinAnsi table 放在 `font.c` 內（無私有 header）；單元測試以 `tests/pdf_builder.h` 由測試原始碼產生小 PDF，整合 goldens 用 `tests/make-font-fixtures.py` 產生並提交的靜態 fixtures。
