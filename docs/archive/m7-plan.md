# M7 實作計畫：Content Stream Interpreter

## 目標與邊界

依 [roadmap 的 M7 與 Common Contracts](pdftext_roadmap.md#7-m7content-stream-interpreter)，把 M6 每頁回傳的 decoded bytes 解讀為依來源順序出現的 PDF operands 與 operators，驗證已支援指令的參數及文字／圖形狀態邊界，供 M8 計算座標。M7 不解碼字型字元碼、不產生 UTF-8 TextItem，也不推估閱讀順序。`Tj`、`TJ` 等操作保留原始 byte string；文字矩陣與 glyph width 的數值計算留給 M8–M10。

入口沿用 [Contents API](../../src/contents.h) 的 caller-owned `{data, len}`，每頁獨立建立 interpreter 狀態；M6 的 Contents array 已在 stream 之間插入 newline。M7 的 parser 與現有 [PDF Object Parser](../../src/parser.h) 分開，但可重用既有 token／object 表示法和詞法規則，不能把 decoded buffer 假裝成由 `reader_open()` 擁有的整份 PDF。

## 開工前固定契約

1. **校正 BT/ET 範圍。** Roadmap 目前寫「BT/ET 外的文字 operator：報錯」，但 PDF 允許 `Tf` 等 text-state operators 在文字物件外設定，且其值會跨文字物件保留；`BT` 重設的是 text matrix 與 line matrix。先將權威條款改為「text-positioning／text-showing operators 必須位於 BT/ET 內；text-state operators 可在外」，並以 `Tf` 在 `BT` 前的 fixture 驗證。[Adobe PDF Reference 1.7 §5.2–5.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)
2. **明定 operator 政策。** M7 必須支援 `BT`、`ET`、`Tf`、`Tm`、`Td`、`TD`、`T*`、`Tj`、`TJ`、`'`、`"`、`q`、`Q`、`cm`。完全未知的 operator 清掉本次 operands，debug mode 發警告；已知卻未支援且可能改變文字、座標或藏有文字的功能，明確回報 `PDF_ERROR_UNSUPPORTED`，例如 inline image `BI`、Form XObject `Do`、`gs` 及未實作的 text-state operators。`ID`／`EI` 單獨出現屬 malformed；不可用一般 token 掃描假裝跳過 inline image 的任意資料。[Adobe PDF Reference 1.7 §3.7、§4.8.6](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)
3. **明定 M7→M8 交接。** 採逐個操作的 visitor/callback：M7 在回呼期間提供 borrowed、已驗證的 typed operands 與 decoded-content offset；回呼返回後釋放該操作的暫存資料。M8 後續可消耗相同操作序列；M7 不累積整頁操作樹。`Tf` 的字型名稱、`Tj`／`TJ` 的 string bytes 不做 Unicode 解碼。
4. **明定錯誤位置與上限。** 解壓資料不能可靠映回原始檔的每個 byte。`pdf_error.offset` 使用可取得的 Page／Contents reference 檔案 offset，訊息包含 decoded-content offset；單元測試另核對精確 decoded offset。詞法 token、array entries、巢狀深度與 `q` stack 分別受 `max_token_size`、`max_container_entries`、`max_nesting_depth` 約束；所有計數及容量成長先檢查 overflow。

## 實作順序

### 1. 建立可獨立測試的 content 語法入口

先新增最小合法與 malformed 靜態 PDF fixture，固定單頁／多 stream 的操作順序及 `BT` 不平衡案例。設計 `src/content_lexer.[ch]` 對 borrowed `{data, len}` 的掃描介面與 token 所有權；`src/content_interpreter.h` 定義逐操作 visitor 和錯誤契約。詞法器應處理 PDF whitespace、comment、number、name、literal／hex string（含 NUL）、array 與 direct dictionary，保留 decoded offset；不接受 indirect reference 或 stream operand。

**驗收**：以不需整份 PDF 的單元測試驗證 token 順序、逃脫字元、二進位 string、截斷字串、錯誤 delimiter、深度與 token 上限；新增測試先失敗再通過。`make test` 保持通過。

### 2. 完成操作分派與狀態邊界

在 `src/content_interpreter.[ch]` 收集單一 operator 前的 operands，遇 operator 時依名稱分派、驗證個數及型別、呼叫 visitor，然後釋放本次 operands。先支援 `BT`、`ET`、`q`、`Q`；拒絕 nested `BT`、多餘／缺少 `ET`、`Q` underflow、頁面結束時未還原的 `q`。將未知、已知未支援與 malformed 的政策寫入表驅動測試，尤其 `BI` 與 `12 0 R`。

**驗收**：visitor 只看得到完整且合法的操作；回呼失敗時停止並清理；空頁成功、錯誤頁無部分輸出。`make test` 通過。

### 3. 加入文字與矩陣操作的 typed operands

完成 `Tf`、`Tm`、`Td`、`TD`、`T*`、`Tj`、`TJ`、`'`、`"`、`cm` 的參數表和狀態位置檢查。數值接受 PDF integer／real，矩陣恰好六個數；`TJ` array 只接受 byte string 與 number，保留相鄰 string、負數調整值及來源順序。`Tf` 可在 `BT` 外；位置與顯示操作只可在 `BT` 內。M8 才套用矩陣運算、font size 與 leading 的語意，但 M7 不得丟失會影響 M8 的參數。

**驗收**：單元測試逐一覆蓋每類操作、operand 不足／過多／錯型、`TJ` 混合陣列、`'` 與 `"`、跨兩個 Contents stream 的操作序列。`make test` 通過。

### 4. 接入整份 PDF 的可觀察模式

從 `src/main.c` 依頁序呼叫 `pdf_contents_read()` 與 interpreter，新增 `--dump-content` 摘要模式；每頁記錄操作數與 text-show 操作數，所有頁成功後才輸出 `PAGES N`、`PAGE i OPS n TEXT_SHOWS n`。未知 operator 僅在此 debug mode 輸出含位置的警告；不輸出任意 binary string。更新 `Makefile`、fixture runner、靜態 PDF fixtures 和 golden，含 raw、Flate、Contents array、malformed、unsupported 及後頁失敗無部分 stdout。

**驗收**：`--dump-content` 的頁序、操作數、exit code、stdout／stderr 與 golden 一致，既有 M0–M6 模式回歸不變。`make test` 通過。

### 5. 完成限制、清理與整體驗收

針對大量 operands、深層 array、過長 token、`q` 深度、callback 中止及 parser 失敗檢查錯誤碼、所有權、overflow 與不洩漏。審查未知 operator 不會把殘留 operands 交給下一個 operator；已知未支援功能不會被當成安全忽略。補齊必要 fixture 後更新 roadmap 的 M7 完成狀態。

**驗收**：`make` 零 warning，`make test`、`make asan` 全通過；code review 對照 M7 契約、錯誤路徑與 M8 交接無阻擋問題。

## 檢查點與風險

- 任務 1–2 後確認 content parser 確實只借用 M6 buffer，tokens／operands 的釋放責任單一且明確；跑完整 `make test`。
- 任務 3 後用真實 PDF 操作序列核對 `TJ` byte strings 和跨 stream 邊界；不可把 PDF 字元碼當 UTF-8。
- 任務 4–5 後檢查未知與已知未支援 operator 的分流，並跑 `make asan`。對 `TL` 等 M8 將需要的 text-state operators，M7 先回報 unsupported；M8 增加相應 handler 後才能輸出可靠位置。
- 測試執行只依賴 C 工具鏈、zlib 和 POSIX shell；fixture 必須是 repository 內的靜態檔。
