# BDC／EMC 與 ActualText 實作交接

狀態：2026-10-22 已實作 A 階段，實作與驗收證據見第 10 節。
第 1–9 節保留交接契約及實作前背景；B 階段仍需獨立設計。
A 僅承諾本文件明列的空 ActualText 子集，不能宣稱完整支援 ActualText 或 Tagged PDF。

## 1. 接手步驟與完成條件

1. 先讀根目錄 [AGENTS.md](../../AGENTS.md)，執行 `git status --short` 與
   `git diff --stat`。以現行工作區為起點，保留既有中文、gs、cs／CS、測試與統計工具。
   交接時 HEAD 是 `61df06a`，尚未包含這些未提交修改，不能以 HEAD 覆蓋現有介面。
2. 閱讀第 2–4 節，確認 A 的支援與拒絕邊界。先建小型受控 PDF，讓舊程式在 BDC
   明確失敗，再實作；測試必須區分「沒有 ActualText」與「ActualText 為空」。
3. 完成 M7 語法／作用域、M8 幾何直譯、M9 輸出抑制三層測試，再跑 CLI 原子輸出測試。
   第 7 節矩陣全部有測試證據，才視為 A 完成。
4. 執行 `make -B test`、`make -B asan`、`make -B CC=clang test` 與
   `git diff --check`。記錄 fixture 數、錯誤分類與未驗證項目，更新本文件狀態。
5. 用未修改的原始 PDF 復測。它仍會先停在 scn；這不是 A 失敗，也不是全書提取成功。
   非空 ActualText 要依第 8 節另設計，不能以直接略過取代。

## 2. 已確認的原檔與目前阻擋點

原檔為 [test_git.pdf](../../output/pdf/chinese-comparison/test_git.pdf)，479 頁，
36,959,295 bytes，SHA-256：

```text
d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70
```

[全頁運算子統計](../../output/pdf/chinese-comparison/test_git-operator-inventory/README.md)
已掃完 479 頁 Contents：33 種、1,037,334 次操作，其中 21 種尚未支援。
BDC、EMC 各 559 次，分布在 296 頁。首個 BDC 在 PDF 第 3 頁 decoded byte 2512，
首個 EMC 在同頁 byte 2564。

本次另以重用 M7 operand parser 的只讀診斷檢查 property 值及區段內操作，確認：

- 559 個 tag 全部是 `/Span`，properties 都是 inline dictionary，只有 `/ActualText` key。
- 559 個 ActualText 全部是 **0 bytes 的 hex string `<>`**，不是非空 UTF-16BE。
- marked-content 最大巢狀深度為 1；每段內有 1–24 次 Tj，每次前有 Td。
- 區段內不是只有一個字串，因此不能將「只支援單次 Tj」作為空 ActualText 的接受條件。

其等價結構為：

```pdf
/Span << /ActualText <> >> BDC
% 一至多組 Td、Tj
EMC
```

原有統計檔只記錄 ActualText key 的出現次數，沒有記錄這次補查的值長度／區段形狀。
接手時可擴充 [統計工具](../../tools/inventory-pdf-operators.py) 重現此檢查；不能只由
key 存在便推論其內容或編碼。診斷掃描繼續越過 unsupported，不代表提取器已支援它們。

目前純文字與 `--dump-text-items` 都仍為 exit 4、stdout 0 bytes：

```text
content: unsupported error at byte 3796: decoded byte 125: content operator is not supported
```

這是第 1 頁的 scn；見 [cs 復測](../../output/pdf/chinese-comparison/test_git-cs-results/README.md)。
本次不順帶開放 scn、SCN、path、clipping 或 Do，也不修改原始 PDF 來繞過它們。

## 3. how：現行介面與所有權

| 層／入口 | 現行事實與接入點 |
|---|---|
| [content_interpreter.h](../../src/content_interpreter.h)、同名 `.c` | 交接時 M7 將 BDC、EMC 列為 unsupported，A 實作後已接受並驗證作用域。callback operands 及所有 descendants 只在該次 callback 有效；M7 擁有並釋放它們。 |
| [text_state.h](../../src/text_state.h)、同名 `.c` | M8 擁有字型名稱、文字參數、CTM、tm／lm 與 q/Q stack，照原始字元碼計算位移；不負責替代文字語意。 |
| [font_text.h](../../src/font_text.h)、`font_text.c::visit/consume` | M9 bridge 在 visit 中處理 gs、Tf／Q；consume 解碼 UTF-8、累計 budget，再呼叫 consumer。適合在此維護 marked-content 的提取政策。 |
| [text_items.h](../../src/text_items.h)、`text_items.c::consume` | 每個非空 raw string event 建一個 caller-owned TextItem。未收到 event 就不建 item；失敗時整份集合清空。 |
| [reading_order.h](../../src/reading_order.h) | 只消費 TextItem，依座標分行／補空格，不應解析 BDC properties。 |
| [main.c](../../src/main.c) | 純文字、items 與 `-o` 都在整份成功後輸出，維持全有或全無。 |

保留中文基線：metrics 接收剩餘 bytes，回傳字元消耗長度與字寬；
`pdf_font_context_create()` 接收跨頁 CMap 計數器；totals 有 `utf8_bytes`、`events`、
`cmap_bytes`。A 不需要改變這些公開介面，也不需要保留 property dictionary。

## 4. A 階段：建議的最小支援契約

以下是 A 階段採用的專案子集政策，並非對 PDF 規格的完整宣稱。

| 輸入 | M7 語法 | 正式 bridge 的 A 政策 |
|---|---|---|
| `/Span << >> BDC … EMC` | 接受、配對 | 沒有 ActualText，文字照常解碼與輸出。 |
| `/Span << /ActualText <> >> BDC … EMC` | 接受、配對 | 有空替代文字，抑制整段的 decoded events；繼續執行段內全部操作。 |
| `/Span << /ActualText () >> BDC … EMC` | 接受、配對 | 與空 hex string 相同。 |
| 非空 literal／hex ActualText | 接受字典形狀 | unsupported；留給 B，不能當無屬性或空屬性處理。A 不承諾完整編碼驗證。 |
| `/ActualText` 為 number、name、null、array 或 dictionary | 接受 BDC 外層語法 | malformed。null 不等於屬性缺省。 |
| `/Span /P1 BDC` | 接受合法 property-name 語法 | unsupported；A 不查 `/Resources /Properties`。 |
| 其他合法 tag、其他 property key | 接受外層語法 | unsupported，包含 MCID、Lang、OC 等；不能宣稱完整 marked-content 支援。 |
| 少／多參數、tag 非 name、properties 非 dictionary／name | malformed | 不進 bridge。 |
| `EMC` 沒有配對 BDC、頁尾未閉合 | malformed | 整份輸出丟棄。 |

A 的「空」指解開 PDF literal／hex 語法後長度為 0。`<FEFF>` 雖在 UTF-16 解碼後可為
空文字，仍是 A 尚不支援的非零 byte payload，留給 B。名稱比對必須 length-aware，
例如 `/Span#00` 不能誤認為 `/Span`，`/ActualText#00` 不能誤認為 `/ActualText`。

空 ActualText 的行為是此專案依 replacement 語意選定的提取契約：輸出為空，
不代表刪除繪圖操作。相反地，下列區段沒有指定替代文字，必須保留正常輸出。
不要只用 `utf8_len == 0` 判斷這兩種狀態，應用明確的 PASS／SUPPRESS 模式。

BMC、MP、DP 仍保持 unsupported。A 只會用 EMC 關閉已接受的 BDC，不假裝已支援 BMC。

### 語法作用域與 BT／ET

BDC 可在文字物件內或外開始，但與 BT／ET 必須正確巢狀，不能交叉關閉：

- 合法：`BDC BT … ET EMC`、`BT BDC … EMC ET`。
- malformed：`BDC BT … EMC ET`、`BT BDC … ET EMC`。
- 同一頁 Contents array 的串接 stream 可以跨界配對；跨頁不可以。
- marked-content 不屬於 graphics state；q/Q 不得保存、還原或自動結束它。
  不擴大 repo 原有 q/Q/cm 在 BT／ET 中的限制。

M7 不能只有一個獨立 depth 計數器而忽略 BT／ET 的交叉。可在 BDC frame 記錄開始時的
`in_text`，在 BT 記錄當時的 marked depth，於 EMC／ET 檢查；或選擇等價的有型別 scope
結構。marked depth 受 `max_nesting_depth` 限制，不能改壞既有 BT／ET、q/Q 的邊界測試。

## 5. architect：建議接法與資料流

1. 在 enum 尾端新增 BDC、EMC；operator table 加上兩者。BDC 的第二參數型別是
   dictionary 或 name，需要一個明確的 operand matcher；不要把 dictionary 當 TJ array。
   從 unsupported 只移除 BDC、EMC，M7 驗證 arity、type、作用域並計入 OPS。
2. 裸 M8 將 BDC／EMC 當幾何 no-op，由 M7 保證配對。raw geometry 仍包含原始字串，
   不宣稱已套 ActualText。gs 的 `/Font` 風險不同，裸 M8 仍保持原先對 gs 的拒絕。
3. M9 在 visit 中於呼叫 M8 前驗證 BDC 的 A 子集，壓入 frame；EMC 彈出 frame。
   每頁建立一份 marked state，任何失敗都由 bridge 清理，不能放到跨頁 totals 或 font cache。
4. `font_text.c::consume()` 每收到一個 raw event，先確認 active font 並檢查／累計
   `events`。若目前有效模式為 SUPPRESS，就不做 glyph-to-Unicode decode，也不呼叫
   上層 consumer；否則沿用現有 UTF-8 解碼與輸出流程。
5. M10、M11 透過既有 event seam 自然得到抑制後的結果，不加入 marker 判斷或字串修補。

可用小型內部 state 或 `src/marked_content.[ch]` 封裝 bridge 的 policy stack：每個 frame
只保留 PASS／SUPPRESS、必要的 decoded offset 與作用域資料。巢狀空 ActualText 都是
SUPPRESS；內層 PASS 不能解除外層 SUPPRESS，只有對應 EMC 才解除相應層的作用。
M7 的語法 stack 與 bridge 的政策 stack 分工不同，但都必須有界、失敗可清理。
若新增模組，加入 Makefile 的 SRC／HDR 及一般、sanitizer 測試；依賴 `pdf_builder.h`。

### 不能跳過的狀態與資源

- 不可在 lexer／M7 將 BDC 到 EMC 的 bytes 整段跳過。M8 仍須執行 Tf、Tm、Td、
  Tj／TJ 的 metrics／advance 等，以保證 EMC 後的座標、字型與 source order。
- 即使不輸出文字，字型解析、字寬與字元碼邊界仍要驗證；不以 ActualText 繞過
  unsupported font、截斷的雙 byte code、非有限幾何或未支援操作。
- SUPPRESS 段內的未映射 Unicode 不產生 U+FFFD 輸出／警告；但字型／CMap 建立仍沿用
  既有驗證與 `cmap_bytes` 計數。這和略過 glyph metrics 是兩回事。
- `events` 計入所有 M8 raw events，含被抑制者與空字串，不能以 SUPPRESS 繞過文件上限。
  `utf8_bytes` 只計真正發布的 UTF-8；A 的被抑制 event 對它增加 0。
- TextItem 的 source order 可因抑制而有缺口，sequence 依實際輸出 item 連續；不重編
  原始 M8 source order，也不建零長 TextItem 佔位。M11 的空白依保留下來的幾何計算。
- A 不保留 operands／tag／字串指標，也不需要在 EMC 補發合成事件。

## 6. 錯誤與上限

M7 語法錯誤定位出問題的 decoded operator，EOF 未閉合定位頁尾；外層維持 Page byte
及 `decoded byte N`。bridge 錯誤以 marked-content／ActualText 明確命名，不能被現有
`font byte` wrapper 誤標為字型錯誤。A 沒有 indirect properties resolver，不虛構 object offset。

marked stack 深度超限為 resource limit；容量乘加／成長必須檢查 overflow，配置失敗
為 out of memory。props 大小與字串長度沿用 parser 的 container／token limits。
錯誤保留原分類；consumer failure、後頁失敗或未閉合都維持整份 items／stdout 全有或全無。
不能在 ET 或 Q 自動補 EMC，也不能以容錯模式漏掉不平衡區段。

## 7. 必測矩陣與驗收證據

| 情境 | 核對重點 |
|---|---|
| 無 ActualText、空 `<>`、空 `()` | 三者不可混淆；前者輸出，後兩者抑制。 |
| 空替代區段含 1、2、24 次 Tj；TJ 多 string／number | 全部位移仍發生，只抑制 decoded events；後續 Tj origin 正確。 |
| 區段前／內／後切換 Tf、字級、Tc、Tw、Tz、Tm、Td | 後續 UTF-8、字寬、矩陣與去掉 marker 的受控 raw geometry 對照一致。 |
| 中文 Identity-H／WinAnsi、缺映射、截斷雙 byte code | 合法被抑制文字不警告 U+FFFD；字元碼錯誤與字寬錯誤仍失敗。 |
| 巢狀 PASS／SUPPRESS、兄弟區段、q/Q、兩頁 | 內層 PASS 不解除外層抑制；EMC 正確恢復；跨頁無狀態殘留。 |
| BDC 包住 BT／ET、BDC 位於 BT／ET 內、兩種交叉關閉 | 前兩者成功，交叉者 malformed；stream 串接可配對，頁尾未閉合失敗。 |
| 少／多參數、錯型、裸 EMC、名稱含 NUL | 分類、decoded offset、OPS 成功數及失敗歸零正確。 |
| 非空 ActualText、property-name、其他 tag／key、BMC | 合法但超出 A 的部分仍 unsupported；不靜默接受。 |
| 抑制段沒有 Tf 就 Tj；段內 unsupported gs／其他操作 | 原有錯誤仍失敗，不能因不輸出而略過驗證。 |
| 低 nesting／events／token／container 上限、配置與 callback 失敗 | 抑制不繞過 limits；原錯誤分類保留；sanitizer 無洩漏。 |
| 前頁有文字、後頁未閉合／錯型／unsupported，`-o` 已存在 | collection 清空、stdout／items 空白、目標原檔保留且無暫存檔殘留。 |

新增可重跑 fixture 產生器、已保存的小型 PDF 與手寫 golden，沿用
[fixtures.tsv](../../tests/fixtures.tsv) 格式。交接時基線為 115 筆，完成後記錄新增數量。
核心測試不依賴 Python、Poppler 或 37 MB 原檔；工具僅用於產生或額外驗收。

若環境沒有 allocation-failure injection 設施，記錄 OOM 路徑尚未注入驗證，
不可把 sanitizer 通過宣稱為已驗證每個配置失敗；仍須檢查失敗清理並測試資源上限。

另做小型 PDF 的畫面／提取對照：讓空 ActualText 包住畫面可見的字串，確認提取結果
按契約抑制，但段後原始字串仍在正確位置；保存 PNG、raw trace、UTF-8／TextItem 與
手寫預期。可與 Poppler 提取比較，差異如實記錄，不能只看 exit code 或畫面有字。

## 8. B 階段：非空 ActualText 的後續設計

這些不屬 A 的完成條件。接續前先補明確契約，不能把第一個 glyph 直接改字後宣稱完成：

- ActualText 是 PDF text string，依 §3.8.1 解碼 PDFDocEncoding 或帶 FEFF BOM 的
  UTF-16BE，不使用當前字型的 WinAnsi／ToUnicode。空字串合法，BOM-only、代理對、
  奇數長度、未定義編碼值與語言 escape sequence 都要明定政策。
- [cmap.c](../../src/cmap.c) 的 UTF-16 helper 是私有 CMap 邏輯，輸入契約不等同帶 BOM、
  可為空的 PDF text string；不能直接把整個 ActualText 當 CMap destination。若抽共用
  Unicode helper，要維持現有 CMap、CID、WinAnsi 行為，補各自邊界測試。
- 替代文字必須對區段發布一次，而不是對每個 Tj／TJ segment 重複。多字串、不同
  字型／位置、跨行、跨 BT／ET 與巢狀 ActualText 的優先權及幾何歸屬需先決定。
  若先只接受單一幾何 event，應明確拒絕合法但無法歸屬的範圍，不猜每字 bbox。
- 目前 `pdf_font_text_event` 借用一個 raw event／font。延遲到 EMC 才發布時，raw bytes
  與 snapshot font name 可能已失效，必須深複製必要 bytes；font handle 只活到 page
  font context 關閉。不可保存 M7 operands 或把 synthetic raw bytes 偽裝為字型字元碼。
- 規劃文字來源標記，區分 font decode 與 ActualText；現有 font subtype／encoding／
  replacement warning 不應讓使用者誤認替代文字來自字型。同步遷移 TextItem、JSON
  診斷與 consumer，不能只有 CLI 最後替換字串。
- document 累計解碼量、保留中的 ActualText／raw bytes、最終 UTF-8 與 event 數都要
  有界；抑制與延遲輸出不能繞過既有上限。named Properties 另需有界 ref-chain、
  cycle／stream／object offset 政策，可參考 ExtGState，不能直接使用 font 私有 resolver。

## 9. 規格來源與下一個 session 提示

[Adobe PDF Reference 1.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)：
§10.5／表 10.7 定義 BDC、EMC、作用域與 BT／ET 正確巢狀；§10.5.1 定義 inline／named
property lists；§10.8.3 定義 ActualText replacement；§3.8.1 定義 PDF text string 編碼。
第 2、3 節記錄交接時的行為／原檔檢查；A 的現行實作與驗收見第 10 節，B 仍為後續設計。

下一個 session 可直接使用：

> 請依 AGENTS.md 與 docs/agents/bdc-emc-handoff.md，保留目前未提交修改，實作 A 階段
> BDC／EMC 與空 ActualText 子集。先建立失敗測試，再完成 M7 配對及 BT／ET 正確巢狀、
> M8 raw geometry 保留、M9 decoded events 抑制、budget 與失敗清理。非空 ActualText、
> named Properties、BMC 與其他未支援指令維持明確拒絕。跑完整測試及 sanitizer、Clang，
> 做受控 PDF 的畫面／文字／座標對照；未修改的 test_git.pdf 仍可能先停在 scn，如實記錄。

## 10. A 階段實作與驗收

實作保留原有中文、gs、cs／CS、統計工具及工作區變更，公開 bridge 介面與 totals 欄位不變。
先新增 `tests/marked_content_test.c`，確認原程式在第一個合法 BDC 案例失敗，再接入實作。

### 所有權與作用域

- M7 在 enum 尾端新增 BDC／EMC；`p` matcher 接受 dictionary 或 name。
  `content_scope` 保存 marked depth 與 BT 開始時的 marked depth。
  因為 BT 本來就禁止巢狀，兩個深度值足以驗證 EMC／ET 不得交叉關閉，無須配置 frame stack。
  q depth 獨立，marked depth 受 `max_nesting_depth` 限制，計數前檢查 SIZE_MAX。
- M8 將兩個 marker 視為幾何 no-op，保留全部原始字串及 source order；gs 的拒絕不變。
- M9 逐項驗證 inline properties，以 length-aware 比較 Span／ActualText。
  以明確的 PASS／SUPPRESS 模式，配合 marked depth 及最外層 SUPPRESS depth 表達政策。
  A 的內層不會解除外層抑制，故只需記錄首次抑制的深度，對應 EMC 才恢復 PASS。
  每個 BDC 都先驗證政策，SUPPRESS 中的 unsupported tag／key 仍會失敗。
- 新狀態只含值，不持有 operands、tag、字串或 heap 配置；每頁建立、失敗即丟棄。
  M7 操作與 M8 metrics 照常執行；M9 在 active font 及 events 檢查後、Unicode decode 前抑制。
  events 計入所有 raw events；utf8_bytes 只計發布的 UTF-8，CMap 建立及計數沿用原規則。
  M10／M11 不解析 marker，也不增加佔位 item 或重編 source order。

### 第 7 節矩陣的對應證據

| 契約 | 可重跑證據 |
|---|---|
| 缺省、空 hex、空 literal、解開語法後為空 | `marked_content_test::policy`；包含 hex 空白、literal 換行 continuation、escaped name。 |
| 1／2／24 次 Tj、TJ 多 string／number、後續 origin | `geometry_and_nesting` 手算 advance，核對 raw order；切換不同字寬 F1／F2、字級、Tc／Tw／Tz、Tm／Td。 |
| 幾何與矩陣仍執行 | 同函式比較移除 marker 的 bridge 對照與裸 M8，核對 CTM、tm／lm、rendering、after、origin／advance。 |
| WinAnsi／Identity-H、未映射與壞字元碼 | `policy`、`budgets_failures_and_items`、`chinese_test::empty_actualtext`；無替代警告，截斷 code／壞字寬／Type3／壞 CMap 仍拒絕。 |
| 巢狀 PASS／SUPPRESS、兄弟、q/Q、兩頁 | `geometry_and_nesting`、`budgets_failures_and_items`，內層 PASS 不解除外層 SUPPRESS，跨頁沒有殘留。 |
| BT／ET 內外、兩種交叉、同頁跨 stream、跨頁拒絕 | `syntax` 與 `budgets_failures_and_items`；EOF／operator 的 decoded offset、OPS／TEXT_SHOWS 失敗歸零。 |
| arity／type／裸 EMC／NUL | `syntax`、`policy`；Span#00、ActualText#00 不會誤匹配。 |
| 非空／FEFF、named、其他 tag／key、BMC／MP／DP | `syntax`、`policy` 與 CLI fixtures，仍保留 unsupported；ActualText 非字串為 malformed。 |
| 沒有 Tf、unsupported gs／其他指令、非有限幾何 | `budgets_failures_and_items`；抑制不能跳過字型、ExtGState 或幾何驗證。 |
| depth／token／container／events／UTF-8／CMap 上限 | `syntax`、`budgets_failures_and_items`、`cmap_budget`；含 suppressed 空事件、跨頁 CMap 計數。 |
| callback failure 與錯誤保留 | consumer 回傳零、resource limit、out of memory 均核對原分類；後頁失敗清空整份 TextItem。 |
| CLI 全有或全無、既有 -o 保留、無暫存殘留 | `tests/run-cli-tests.sh` 與新增 manifest rows，涵蓋後頁未閉合、錯型、非空及 named properties。 |

新增 `tests/make-marked-fixtures.py`、12 份小 PDF 與 18 筆 manifest cases，總數由
115 增為 133。25 個 fixture／golden 檔案重跑後逐 byte 相同。
新增分類為 success 6、malformed 8、unsupported 4；全套為 success 59、malformed 52、
unsupported 21、io 1。資源上限由 C 測試另行覆蓋。
錯誤 golden 的分類、原因與 offset 由 fixture 意圖制定，不由產品輸出回填。
核心 make 測試只用已保存 PDF 與 C／POSIX shell，不依賴 Python、Poppler 或原始書籍。

最後版本的 `make -B test`、`make -B CC=clang test`、`make -B asan` 均通過，
每輪包含全部 133 筆 fixtures 與單元／CLI 測試。`git diff --check` 通過，
編譯器無 warning，ASan／UBSan／LeakSanitizer 無錯誤。
首次沙箱內 LeakSanitizer 因 ptrace 無法啟動，已取得執行授權在沙箱外重跑，沒有關閉 leak detection。
Make 有環境既有的 clock skew 提示，三輪均用 `-B` 強制重建。
完整 log 保存在 [marked-comparison](../../output/pdf/marked-comparison/README.md)。

畫面、raw trace、正式 font trace、UTF-8、TextItem 與手寫預期見
[marked-comparison](../../output/pdf/marked-comparison/README.md)。空 ActualText 包住畫面仍可見的 BC，
本工具輸出 `A A\n`，最後 A 的 origin 仍是 `(45.6, 180)`，與移除 marker 的對照相同。
Poppler 同樣省略 BC，但將兩個 A 分成不同的行，差異已保留在報告。

原始書籍 SHA-256 未變；純文字與 items 均 exit 4、stdout 0 bytes，仍先停在
Page byte 3796／decoded byte 125 的 scn。未修改原檔、未繞過 unsupported，沒有全書成功宣稱。

未驗證項目：沒有 allocation-failure injection 設施；callback 的 OOM 分類測試不能替代
allocator 故障注入。新 marked state 不配置 heap，原 parser／字型等配置失敗仍沿用原清理路徑。
環境沒有逗號小數點 locale，既有 locale 測試明示略過。非空 ActualText 與 named Properties
維持第 8 節後續範圍，沒有宣稱完整 Tagged PDF 支援。
