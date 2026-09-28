# M11 實作計畫：Reading Order、純文字輸出與正式 CLI

> 已完成（v1.0.0，2026-09-28）。下方「建議政策」是計畫原文，其中 `1.0.0-dev` 等過渡描述已由結果取代；現行行為以 [roadmap M11](../pdftext_roadmap.md#11-m11reading-order-與-cli) 與程式為準。

## 目標與授權範圍

使用者已確認下方全部建議政策（特別確認 D2 非水平文字整份失敗、A5 dump 寫 stdout），並追加 `-o FILE` 原子寫入。範圍以 [roadmap M11](../pdftext_roadmap.md#11-m11reading-order-與-cli)、[v1.0 發布範圍](../pdftext_roadmap.md#12-v10-發布範圍)、[Common Contracts](../pdftext_roadmap.md#14-common-contracts) 與 [Release Gate](../pdftext_roadmap.md#16-release-gate) 為準。上游介面是 [text_items.h](../src/text_items.h)（M10，`989a986`）。

M11 讓 `pdftext input.pdf` 輸出可閱讀的純文字：消費 `pdf_text_items`，排成單欄、水平的閱讀順序，分行、補空白，整份成功後寫 stdout。同時整理正式 CLI，並對照 Release Gate 判斷能否宣告 v1.0。M11 不重新解析 PDF，也不改 M6–M10 的行為。

驗證延續既有授權：建置、單元與整合測試、sanitizer、PDF 渲染與對照，可用 subagent 審查與驗證。開工前讀 AGENTS.md、檢查 `git status --short`，保留既有未追蹤檔案。

## 建議政策（待確認）

每項都附建議與理由。確認後，任務 1 先把它們寫回 roadmap，不在程式裡暗中決定。

### A. CLI 與相容性

| # | 項目 | 建議 | 理由 |
|---|---|---|---|
| A1 | 目前不帶選項的舊模式 | 改名為 `--dump-objects`（行為不變：印 `file size` 並逐一 dump 物件）。`pdftext input.pdf` 改為純文字模式。Makefile 的 build gate 與 19 筆舊 `.txt` fixtures 改用 `--dump-objects`，golden 內容不變。 | roadmap 規定無選項就是正常文字輸出；舊行為仍是有用的 M2 除錯工具，改名保留即可。 |
| A2 | 程式有、roadmap 沒列的模式 | 保留 `--object`、`--indirect`、`--dump-contents`，寫進 roadmap 的 CLI 清單作為 debug mode。 | 已有 fixtures 與使用者；移除沒有好處。 |
| A3 | roadmap 有、程式沒有的模式 | 新增 `--help`（usage 寫 stdout、exit 0）、`--version`（`pdftext 1.0.0-dev`，Release Gate 通過後才改 `1.0.0`）、`--dump-header`（PDF header 版本）、`--dump-trailer`（trailer dictionary）、`--dump-object N`（以 xref 的 generation 解析 object N 並 dump）。 | 補齊 roadmap 清單，全部沿用既有 reader／xref／document 介面。 |
| A4 | 參數錯誤 | 未知選項、缺檔名、多個模式、`--dump-object` 的 N 非正整數：usage 寫 stderr、exit 1。 | 對應 exit code 表的「CLI usage error」。 |
| A5 | debug dump 的輸出位置 | **修改 roadmap**：`--dump-*` 的內容就是該模式要求的輸出，寫 stdout；錯誤、警告與提示寫 stderr。 | 現有 dump 與 75 筆 fixtures 都用 stdout，也方便接 pipe。roadmap 原文「debug dump 輸出 stderr」和現況衝突。 |

### B. 閱讀順序

| # | 項目 | 建議 | 理由 |
|---|---|---|---|
| B1 | 字級基準 | 所有容差與門檻使用 M10 的 `effective_size`（含 cm 縮放），不用 Tf 名目字級。 | 座標在 user space，字級也要在同一尺度；cm 放大 2 倍的文字才不會被誤判。 |
| B2 | 分行演算法 | 每頁內先依 y 由大到小排序（同 y 依 sequence），逐一分群：item 與目前這行的**錨點**（該行第一個 item 的 y）相差不超過 `max(1.5, min(錨點字級, item 字級) × 0.25)` 就併入，否則開新行。行內依 x 由小到大，x 相同依 sequence。 | 兩兩比較的容差不具遞移性，直接用在排序比較函式會得到不穩定結果。固定錨點的分群是確定的，也符合 roadmap 的公式。 |
| B3 | rise（上下標） | 不特別處理，y 已含 rise。小於容差就留在同一行，否則自成一行。 | v1.0 不做排版推測。 |
| B4 | 多欄 | 不偵測欄位。多欄頁面會依 y 交錯輸出，寫進已知限制。 | v1.0 明列不支援 multi-column reconstruction。 |

### C. 空白與文字內容

| # | 項目 | 建議 | 理由 |
|---|---|---|---|
| C1 | 補空白門檻 | 同一行相鄰兩個 item，空隙 = 後者 x −（前者 x + 前者 width）。空隙 > 前者 `effective_size × 0.25` 時補一個 U+0020。 | 空白屬於前一段文字的字型；roadmap 門檻是 `font_size × 0.25`。 |
| C2 | 不重複補空白 | 前者以 U+0020 或 U+00A0 結尾，或後者以其開頭時，不補。 | roadmap：原有空白必須保留，也不應再多一個。 |
| C3 | 重疊（空隙 ≤ 門檻或為負） | 直接相接，不補空白、不刪字。 | 例如 TJ 字距微調或粗體疊印；v1.0 不去重。 |
| C4 | 原文字元 | UTF-8 原樣輸出，包括 NBSP、soft hyphen 與 U+FFFD；不 normalize、不去掉行尾空白。 | 與 M9 政策一致；輸出忠於原文。 |

### D. 邊界政策（M10 留下的問題）

| # | 項目 | 建議 | 理由 |
|---|---|---|---|
| D1 | Tr=3 不可見文字 | **輸出**，和可見文字一樣排序。 | 掃描 PDF 的 OCR 文字層就是 Tr=3，這通常正是使用者想擷取的文字；Poppler 的 pdftotext 也會輸出。 |
| D2 | 非水平文字（`horizontal=0`：旋轉、鏡像、負 Tz） | 整份文件失敗：unsupported、exit 4，stderr 指出頁碼與 decoded offset，stdout 空白。 | v1.0 不支援 rotation／vertical writing；Release Gate 要求範圍外功能不可被靜默當成成功。略過會讓人以為文字完整。 |
| D3 | U+FFFD 提示 | 照樣輸出 U+FFFD；另在 stderr 印一行 `pdftext: warning: N undecodable bytes replaced with U+FFFD`，exit 0。 | roadmap M9：「debug mode 可提示」。只加一行摘要，不中斷輸出。 |
| D4 | 沒有任何文字 | stdout 空白，stderr 印 `pdftext: no extractable text layer`，exit 0。Tr=3 的 item 也算有文字。 | roadmap 要求輸出這句話，同時規定正常文字只走 stdout；它是提示，不是文字內容。 |

### E. 輸出格式

| # | 項目 | 建議 |
|---|---|---|
| E1 | 行與頁 | 每行後接 `\n`；頁與頁之間多一個 `\n`（一個空白行）；沒有文字的頁不輸出任何東西、也不產生空白行。 |
| E2 | 結尾 | 整份輸出以恰好一個 `\n` 結尾。 |
| E3 | 暫存與失敗 | 整份在記憶體組好後才一次寫 stdout；任何失敗 stdout 為空。輸出大小受 `max_total_decoded_size` 限制（UTF-8 加補空白與換行）。`--best-effort` 不做。 |

### F. v1.0 Release Gate

| # | 項目 | 建議 |
|---|---|---|
| F1 | README | 新增 `README.md`：建置、用法、全部 CLI 模式、exit code 表、支援與不支援的 PDF 特性、已知限制。與 `--help` 及 roadmap 一致。 |
| F2 | 編譯器 | 除 GCC 外用 `make CC=clang` 跑 `make -B test`（本機有 clang），零 warning。 |
| F3 | 宣告 v1.0 | 逐項勾 Release Gate；全部通過後，`--version` 才改為 `1.0.0`。若有項目未過，記錄缺口，不宣告。 |

## 介面與模組（名稱在任務 1 固定）

- 新增 `src/reading_order.[ch]`：輸入借用的 `pdf_text_items`，輸出 caller-owned 的純文字 bytes（`{data,len}`）與統計（行數、頁數、replacement 總數）。只做排序、分行、補空白與格式化，不讀 PDF。另提供行分群結果的 debug dump，以符合「每層都有可觀察的 debug dump」（CLI 是否要加 `--dump-lines` 由任務 1 決定，預設不加）。
- `src/main.c` 整理成表驅動的選項解析：一個模式、一個檔名，其餘情況 usage error。

## 任務與驗收順序

1. **固定政策與介面**：把確認後的政策寫回 roadmap；定義 `reading_order.h`；跑基線 `make -B test`。
2. **CLI 遷移**：`--dump-objects` 改名、build gate 與 19 筆舊 fixtures 遷移、A4 參數錯誤 fixtures；確認 75 筆既有 fixtures 全部維持。
3. **分行與排序**：單元測試涵蓋同一行不同字級、容差邊界（剛好等於／超過）、source order 反序繪製的頁、y 相同 x 相同、rise、多頁、空頁。
4. **空白與輸出**：C1–C4、E1–E3、D1–D4 的單元與 CLI golden；非水平文字 exit 4；無文字與 U+FFFD 的 stderr 訊息。
5. **補齊 CLI**：`--help`、`--version`、`--dump-header`、`--dump-trailer`、`--dump-object N` 與 fixtures。
6. **真實 PDF 對照**：`output/pdf/m11-comparison/`，把 Poppler 渲染、`pdftotext -raw` 參考與本專案 stdout 並排；含一份刻意打亂繪製順序的頁面，證明輸出是閱讀順序而非 source order。記錄 hello.pdf、compilerbook.pdf 的實際拒絕點。
7. **Release Gate**：README、clang 建置、`make -B test`／`make -B asan`、subagent 審查與 fuzz 驗證；逐項勾選，決定是否宣告 v1.0。

## 必須能獨立核對的案例

1. 同一行三段 `Hello`、`PDF`、`World` 以 source order 反序繪製（x 由大到小）：輸出 `Hello PDF World`。
2. 兩個 item 基線相差 3、字級 12（容差 3）：同一行；相差 3.01：兩行。
3. `(Hello ) Tj (World) Tj` 緊接：原文已有空白，不再補，輸出 `Hello World`。
4. 空隙 2.9 與 3.1、字級 12（門檻 3）：前者直接相接，後者補一個空白。
5. 三頁，第二頁沒有文字：輸出 `第一頁\n\n第三頁\n`，只有一個空白行、一個結尾換行。
6. 只含 Tr=3 文字的頁：照常輸出；只含空頁的 PDF：stdout 空、stderr `no extractable text layer`、exit 0。
7. 旋轉 90° 的文字：exit 4、stdout 空。

## 實作紀錄

任務 1–7 已完成，契約見 [roadmap M11 政策](../pdftext_roadmap.md#m11-政策實作契約)，驗收與已知限制見 roadmap「M11 完成狀態」，Release Gate 於 2026-09-28 全部勾選，版本 1.0.0。與計畫的差異：新增 `-o FILE`（同目錄隱藏暫存檔、fsync、rename；檔名不可以 `-` 開頭）；所有模式成功後檢查 stdout 寫入（失敗 exit 2）；輸入不是一般檔案時回報 io；修正 clang 對一個 M9 測試字串串接的 warning。CLI 未加入 `--dump-lines`，行分群 dump 只在 library 層（`pdf_text_lines_dump`）。
