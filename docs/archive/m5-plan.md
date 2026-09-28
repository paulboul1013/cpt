# M5 實作計畫：Document、Resolver、Pages Tree

本計畫依 [架構與 Roadmap](../../pdftext_roadmap.md#5-m5documentresolver-與-pages-tree) 執行。目標是從整份 PDF 的 trailer `/Root` 取得依來源順序排列的頁面，以及每頁有效的 `/Resources`、`/MediaBox`、`/Contents` 參照。M5 不解碼內容流、不擷取文字。

## 依賴與共同契約

`reader → xref → document/resolver → pages → CLI`。`pdf_document` 擁有 reader、xref 和 object cache；resolver 回傳的物件指標由 document 擁有，呼叫端不得釋放，關閉 document 後失效。Pages 模組只透過 resolver 取間接物件，不直接讀 xref。所有失敗都經 `pdf_error` 回報類別與位元組位置；沿用 `pdf_limits` 的 cache 與 page 上限。

## 任務

### 1. 建立 Document 生命週期

- 新增 `src/document.[ch]`，以檔名開啟 reader、驗證 `%PDF-` header、解析 xref，並在失敗或 close 時統一釋放資源。
- 定義 cache 狀態 `UNLOADED → LOADING → READY/FAILED`、所有權與對外 API；保留既有 CLI 模式。
- **驗收**：正常文件可 open/close；壞 header、壞 xref、配置失敗均回報錯誤且無洩漏。以 `tests/document_test.c` 驗證，執行 `make test`。

### 2. 解析直接／間接參照

- 實作 `pdf_resolve()`：核對物件編號、generation、xref in-use 狀態與 offset；seek 後以 M3 parser 讀取並再次核對物件身分，完成後恢復 reader 游標。
- READY 重複查詢回傳同一 borrowed pointer；LOADING 重入報 cycle；FAILED 不當成成功。串接 M3 的 indirect `/Length` callback，並限制 cache 數量與解析鏈深度。
- **驗收**：成功、重複查詢、free/不存在/錯 generation、錯 offset、循環 `/Length`、超過上限皆有單元測試；`make test` 通過。
- **依賴**：任務 1。主要檔案：`src/document.[ch]`、`tests/document_test.c`。

### 3. 從 trailer 走到 Catalog 與頁面樹

- 新增 `src/pages.[ch]`；解析 `/Root` 為 `/Type /Catalog`，要求 `/Pages` 指向 `/Type /Pages`；依 `/Kids` 陣列來源順序遞迴，收集 `/Type /Page`。
- 驗證節點型別、`/Kids`、`/Count` 與實際葉頁數；辨識 Pages Tree cycle，限制深度與頁數。父子關係需核對 `/Parent`，避免把錯誤圖誤當頁樹。
- **驗收**：單頁、多頁、多層樹與非法型別、缺欄位、Count 不符、cycle、page limit 均有 fixture；頁序正確。`make test` 通過。
- **依賴**：任務 2。主要檔案：`src/pages.[ch]`、`tests/pages_test.c`、fixtures。

### 4. 計算每頁有效屬性

- 對每個 Page 向父 Pages 節點繼承 `/Resources` 與 `/MediaBox`；子節點設定優先。記錄 `/Contents` 原始物件供 M6 使用；缺少時視為空白頁。
- 全鏈缺 `/MediaBox`、屬性型別錯誤報 malformed；沒有 `/Resources` 的空白頁允許，真正使用字型時才由後續階段判錯。
- **驗收**：父層繼承、子層覆蓋、空白頁、缺 MediaBox 與錯型別有測試；輸出頁面的屬性可供 M6 使用。
- **依賴**：任務 3。主要檔案：`src/pages.[ch]`、`tests/pages_test.c`、fixtures。

### 5. 加入可觀察的整份 PDF 模式與驗收

- 增加 `--dump-pages document.pdf`，列出頁數、來源順序、頁面物件號與有效 MediaBox／Resources／Contents 概況；錯誤只寫 stderr，保留既有模式輸出契約。
- 更新 `Makefile`、fixture/golden runner 與 roadmap 狀態；用 `tests/hello.pdf` 和專用多頁 fixture 跑端到端測試。
- **驗收**：`make`、`make test`、`make asan` 全通過；合法與 malformed fixtures 的 exit code、stdout、stderr 符合 golden；M0–M4 回歸不變。
- **依賴**：任務 4。主要檔案：`src/main.c`、`Makefile`、`tests/fixtures.tsv`、`tests/golden/`、roadmap。

## 檢查點

- 任務 1–2 後：以一份實際 PDF resolve Catalog，確認 ownership、游標恢復與 `/Length` callback；跑 `make test`。
- 任務 3–4 後：多層頁樹與繼承 fixture 通過，malformed 輸入均有明確錯誤；跑 `make test`。
- 任務 5 後：跑完整 build、fixture、ASan/UBSan 驗收，再進行程式碼審查。

## 先處理的設計風險

- M3 parser 對 indirect `/Length` 用 callback，resolver 又會呼叫 parser；需明確保存／恢復 reader 游標，並以 LOADING 偵測跨物件循環。
- `pdf_object` 不包含 stream bytes；Document cache 應擁有完整 `pdf_indirect_object`，讓 M6 可以取得 raw stream。
- `/Count` 是頁樹宣告值；驗收以實際走訪結果核對，不以它決定要分配或輸出的頁數。
