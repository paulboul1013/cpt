# M10 實作計畫：TextItem

## 目標與授權範圍

本文件只做規劃，還沒有開始寫 C 程式，也不 commit。範圍以 [roadmap M10](../pdftext_roadmap.md#10-m10textitem)、[Common Contracts](../pdftext_roadmap.md#14-common-contracts) 與 Architecture Rules 為準。現行介面看 [font_text.h](../src/font_text.h)、[text_state.h](../src/text_state.h)、[font.h](../src/font.h)。M9 已提交（`a7d37d6`）。

M10 把 M9 的 **borrowed decoded event** 轉成 **caller-owned、整份文件存活的 TextItem 集合**，並提供 roadmap 已列出的 `--dump-text-items` debug CLI。M10 不做新的 PDF 解析、字型解碼或矩陣運算：幾何沿用 M8，字寬與 UTF-8 沿用 M9。

M11 才做以下幾件事：閱讀順序排序、同行判定、synthetic space，以及純文字輸出與正式 CLI（`pdftext input.pdf`）。M10 只保存 M11 需要的欄位，不預先排序，也不合併 item。

驗證延續既有授權：建置、單元／整合測試、sanitizer，以及 PDF 渲染與對照。開工前先讀 AGENTS.md 並檢查 `git status --short`，保留既有的未追蹤檔案。本計畫不授權 commit 或 push。

## 實作前固定的政策

以下是 roadmap 尚未定義的 **建議政策**。任務 1 先把它們寫回 roadmap M10，不要在程式裡暗中決定。

| 項目 | 建議政策 | 理由 |
|---|---|---|
| 粒度 | 每個 M9 decoded event 產生一個 item：一個 Tj／`'`／`"` 一個，TJ 每個 string segment 一個。TJ 數字只影響後續 origin，不產生 item。 | 與 roadmap 相同；M8 已依此切分事件。 |
| 空字串 | raw bytes 長度為 0 的 string **不產生 item**，但 M9 仍會驗證其 Tf。source order 因此可能不連續。 | 沒有可提取文字；M11 的 tie-break 只需要單調遞增，不需要連續。 |
| 不可見文字 Tr=3 | **產生 item**，並保存 `rendering_mode`。M10 不過濾。 | 掃描 PDF 的 OCR 文字層通常是 Tr=3；排除與否屬於 M11 的輸出政策。 |
| 起始 x/y | M8 的 `origin`：default user space、已套用 CTM 與 rise 的 baseline 起點。 | 已有獨立驗收；M10 不再轉換。 |
| width | 保存帶正負號的 advance 向量 `(dx, dy)`（user space），另存純量 `width = dx`。 | 水平文字時 `dx` 就是寬度。advance 可能為負（負 Tc、負 Tz），不取絕對值、不捏造。 |
| height | `height = hypot(c, d)`，也就是 rendering matrix 第二欄（font 的 y 單位向量）的長度。等於 user space 中的有效字級，含 CTM 縮放，不含 rise。 | 專案不讀 ascent／descent 或 FontBBox，不能宣稱是 glyph bbox。這個值是「有效 em 高度」，名稱與文件都要寫清楚。 |
| font size | 同時保存 Tf 的名目字級 `font_size`，以及上述有效字級 `effective_size`（即 height）。 | roadmap 要 font size；M11 的 line tolerance 需要 user-space 尺度。cm 放大 2 倍時兩者不同。 |
| 方向 | 保存 `horizontal` 旗標：rendering matrix 的 `b`、`c` 在容差內為 0，且 `a > 0`、`d > 0` 時為 1。M10 不拒絕非水平 item。 | v1.0 不支援 page rotation 與直書，但 CTM 仍可能旋轉文字。是拒絕還是略過，由 M11 在排序前決定，M10 只提供事實。 |
| font resource name | 複製 length-aware bytes（可含 NUL）。同一頁連續相同名稱可共用 storage；不以 BaseFont 取代。 | resource name 是頁內 identity。 |
| replacement | 保存 replacement 次數。debug dump 顯示；正常文字模式交給 M11（例如只在 stderr 提示）。 | roadmap M9：「debug mode 可提示」。 |
| 順序 | 保存 `page`（1-based）、頁內 `source_order`（M8 值），以及全文件遞增的 `sequence`。 | M11 排序的最後 tie-breaker 是 source order；sequence 讓 dump 與測試可以穩定引用。 |
| encoding／subtype | 保存 M9 的 subtype 與 encoding 標記（值型別 enum），不保存 font handle。 | font context 每頁結束就銷毀，item 不能借用它。 |

以下不在 M10：glyph ink bbox、ascent／descent、行合併、空白插入、dehyphenation、ligature 展開，以及任何 text normalization。

## Interface 與所有權

建議新增 `src/text_items.[ch]`，在小 interface 後集中「整份文件 → items」的流程。main 與測試不重做 M6→M9 的串接。

### 最小對外 surface（名稱在任務 1 固定）

- **`pdf_text_item`**：值型別。`text`／`text_len` 與 `font`／`font_len` 指向集合擁有的 storage；`text` 另有結尾 NUL，但長度不含它。其餘欄位包括 page、source_order、sequence、x／y、advance、width、height、font_size、effective_size、rendering_mode、horizontal、replacements、subtype、encoding，以及 decoded offset。
- **`pdf_text_items`**：caller-owned 集合，必須以零值初始化。擁有 item 陣列與一塊（或分段的）byte arena。`free` 可接受已清空的集合，執行後回到零值。
- **Extract（整份文件）**：借用已開啟的 document，自己 load pages、讀 Contents，再逐頁呼叫 `pdf_font_text_page_interpret`。成功時轉交整份集合；任何失敗時集合為空，error 保留原始 cause。**全有或全無**：不回傳部分頁面。
- **Dump**：對單一 item 輸出一行 JSON。text 用 hex 加上只含 ASCII 的 escaped preview，font 用長度加 hex；數字使用 thread-local C numeric locale 與固定小數位。只在 extract 成功後呼叫。

### 所有權細節

- Arena 內指標在 growth 時會失效：先以 offset 暫存，extract 成功後才轉成指標；或改用不搬移的分段 chunk。任務 1 擇一並寫進 `.h`。
- item 陣列與 arena 的每次 growth 都要做 checked arithmetic。容量上限：item 數用 `max_container_entries`；arena 內 UTF-8 總量已由 M9 totals 限制在 `max_total_decoded_size`；font name bytes 另外計數，也受同一上限約束。
- Consumer 是 M9 callback：複製失敗時設定 error，交給 M9／M8／M7 保留 first-error-wins。已收集的資料在最外層統一釋放。

### 錯誤

沿用現有 error codes 與定位，不新增分類。

| 情況 | Error |
|---|---|
| M6–M9 任一層失敗 | 原樣保留（Page 或 Contents offset，訊息含 decoded／font byte） |
| item 數或 arena 超過上限、size overflow | resource-limit |
| allocation failure | out-of-memory |
| dump I/O 失敗 | io |

## CLI：`--dump-text-items`

- `pdftext --dump-text-items input.pdf`：整份成功後才寫 stdout，每個 item 一行 JSON，依 page、source order 排列（**不是**閱讀順序）。失敗時 stdout 為空、exit code 依 error，stderr 輸出一則錯誤。
- 沒有任何 item（例如只有空頁）時 stdout 為空、exit 0。「no extractable text layer」是 M11 純文字模式的訊息，M10 不輸出。
- 其他 dump mode 維持不變。usage 字串加入新選項；一次只能選一個 mode。
- 這是 debug mode：可列出 hex、U+FFFD 次數與不可見文字，不做文字 normalization。

## 任務與驗收順序

### 任務 1：固定 contracts 與 fixture

**工作**：把上面的政策寫回 roadmap M10；定義 `text_items.h`（item 欄位、集合所有權、arena 指標策略、extract／free／dump）；在 fixtures.tsv 規劃 `--dump-text-items` 的正常與錯誤列。先跑基線 `make -B test`。

**驗收**：每個欄位都有明確定義與單位；height 的文件寫明「不是 glyph bbox」；每個 allocation 都有 owner。

### 任務 2：集合與單頁轉換

**工作**：實作 item 陣列與 arena、M9 consumer 轉換（含空字串略過、height／effective_size／horizontal 計算）、free。

**驗收**：
- 單元測試：Tj、`'`、`"`、TJ 多 segment、空字串、Tr=3、名稱含 NUL、cm 放大 2 倍（effective_size = 2×font_size）、cm 旋轉 90°（horizontal=0）、負 Tc 產生負 width。
- growth 跨越容量邊界後，text 與 font 指標仍然正確。

### 任務 3：整份文件 extract 與全有或全無

**工作**：extract 串接 pages → contents → M9 bridge；跨頁計算 sequence；限制與 cleanup。

**驗收**：
- 多頁的 page／sequence 正確；兩頁同名 `/F1` 各自保留自己的 encoding。
- 後頁失敗、item 上限、arena 上限、consumer 複製失敗時，集合為空，error 保留原因；sanitizer 無洩漏。
- M8／M9 的 geometry fixtures 轉成 items 後，x／y／width 等於現有 goldens 的 origin／advance。

### 任務 4：Dump 與 CLI

**工作**：item JSON dump；main 加入 `--dump-text-items`；fixtures.tsv 與 goldens。

**驗收**：
- font-winansi、font-pages、geometry-raw、visual-m7-text 的 stdout golden。
- font-later-failure、font-missing-resource、hello.pdf（`w` unsupported）的 exit code，stdout 為空。
- dump 不輸出 raw 控制字元；locale 不影響數字格式。
- 既有 65 筆 CLI fixtures 全部維持。

### 任務 5：真實 PDF 對照與 release acceptance

**工作**：新增 `output/pdf/m10-comparison/`，更新 [視覺驗收文件](pdf-visual-comparison.md)。在 Poppler 截圖上畫出每個 item 的 origin、advance 與 `height` 框（標示為 em 高度，不是 ink bbox）。

**驗收**：
- 專案 CLI 實際輸出的 item 欄位，與獨立計算的 expected（沿用 M9 capture 的字型寬度來源）逐項比較 expected／actual／delta。
- 人工檢查 overlay。
- `make`／`make -B test`／`make -B asan` 全部通過，零 warning；subagent code review 無 blocker 後，才標示 M10 完成並寫 M11 交接。

## 必須能獨立核對的案例

1. `BT /F1 12 Tf 1 0 0 1 10 20 Tm (ABC) Tj ET`，Widths 600／650／700：產生一個 item，text=`ABC`、x=10、y=20、width=23.4、height=12、font_size=12、horizontal=1。
2. `2 0 0 2 0 0 cm` 後以 12pt 顯示：font_size=12、effective_size=height=24，x／y／width 都在 user space 放大。
3. `0 1 -1 0 0 0 cm` 旋轉：horizontal=0，advance 為 `(0, w)`、width=0；item 仍保留，由 M11 決定如何處理。
4. `[(A) -500 (B)] TJ`：兩個 item，第二個的 x 等於第一個的 x + width + 0.5×size；不產生數字 item。
5. `() Tj (X) Tj` 搭配 Tr 3：只產生 X 一個 item，rendering_mode=3，source_order=1（第一個空字串占 0）。
6. 兩頁、第二頁 `/ToUnicode`：extract 失敗、集合為空、CLI stdout 為空、exit 4。

## 主要風險與處理

| 風險 | 處理 |
|---|---|
| height 被誤解為 glyph bbox | 欄位名與 dump 使用 `em_height`（或類似）並寫文件；overlay 標註。 |
| 旋轉或縮放的 CTM 讓 M11 比較錯尺度 | 保存 effective_size 與 horizontal 旗標；在 M11 計畫中決定非水平文字的政策。 |
| Arena growth 使舊指標失效 | 任務 1 先固定 offset 或分段策略，並用跨越容量邊界的測試驗證。 |
| 部分頁面結果外洩 | extract 全有或全無；CLI 成功後才寫 stdout。 |
| 把 debug dump 當正式輸出 | `--dump-text-items` 標示為 debug；正式純文字留給 M11。 |

完成 M10 後，M11 的交接重點：M11 消費 `pdf_text_items`，決定 Tr=3、非水平與 replacement 的輸出政策，再依 roadmap 的 line tolerance 排序並插入 synthetic space。

## 實作紀錄

任務 1–5 已完成，契約見 [roadmap M10 政策](../pdftext_roadmap.md#m10-textitem-政策實作契約) 與 `src/text_items.h`，驗收與交接見 roadmap「M10 完成狀態與交接」。與計畫的差異：arena 採 offset 暫存、成功後轉指標；em 高度欄位命名為 `em_height`（另存 `effective_size`）；item 數與 UTF-8 上限實際先由 M9 bridge 的共用 totals 觸發，M10 的同名檢查保留為防禦；`--dump-text-items` 先在記憶體格式化再一次寫出；新增 libm 連結。
