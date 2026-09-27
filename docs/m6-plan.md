# M6 實作計畫：Contents 與 Stream Decode

## 目標與邊界

依 [roadmap 的 M6 與 Common Contracts](../pdftext_roadmap.md#6-m6contents-與-stream-decode)，把 M5 每頁保留的 `/Contents` 轉成依來源順序排列、已解碼的位元組，供 M7 content interpreter 使用。M6 不解析 PDF 內容指令、不做字型解碼或文字輸出，也不新增 xref stream／object stream 支援。

目前 [Pages API](../src/pages.h) 提供 borrowed `/Contents` 物件，[Document API](../src/document.h) 的 resolver 保留含 raw stream bytes 的完整 indirect object；`/Length` 的 direct integer 與 indirect reference 已由 parser/resolver 處理。M6 應在其上新增擁有輸出 buffer 的 `contents` 模組，讓呼叫端明確釋放，不改動 M5 cache 的所有權。

## 開工前校正兩項契約

1. Roadmap 的「direct stream」與 PDF 格式規則衝突。[Adobe 的 CosStream 文件](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdflsdk/apireference/COS_Layer/CosStream.html) 明確指出 stream 必須是 indirect object；現有 `pdf_object` 也無 direct stream 表示法。建議把該項改為「`/Contents` 可直接是 array，或是指向 stream／array 的 reference；stream 本身必須是 indirect」，並以 malformed fixture 驗證錯誤型別。先更新權威 roadmap，再依修正後的契約實作。
2. Roadmap 要求單一解碼上限與跨頁 total decoded budget，但 [現有 limits](../src/limits.h) 只有 raw stream 上限。新增獨立的 `max_decoded_stream_size` 與 `max_total_decoded_size`；預設各 256 MiB，並把 array 串接時插入的 newline 算進總輸出量。保留 `max_stream_size` 的 raw bytes 語意。

## 資料與錯誤契約

- 每頁讀取結果是 caller-owned `{data, len}`；空白頁為空結果。建立跨頁讀取 context 記錄已產生的 decoded bytes，避免每頁重設 total budget。
- `/Contents` 缺少時是空白頁；存在時可為 stream reference、直接 array，或指向 array 的 reference。Array 元素依 source order 指向 indirect stream；每個 stream 先獨立解碼，再在相鄰 stream 間補一個 newline。Reference 必須由 `pdf_resolve()` 取得。
- 每個 stream 的 `/Filter` 缺少時使用 raw bytes；M6b 僅支援單一 `/FlateDecode`。未知 filter 與 filter array／chain 回報 `PDF_ERROR_UNSUPPORTED`；錯誤型別、非 stream 目標、損壞的 Flate 資料回報 `PDF_ERROR_MALFORMED`。
- `/DecodeParms` 缺少、null 或預設 predictor 1 可處理；其他 predictor 與未支援參數明確回報 unsupported，避免輸出錯誤 bytes。所有容量增長與 `size_t` 加法先檢查 overflow；超過任一 decoded budget 回報 resource limit。錯誤位置使用可得的參照 offset，並在失敗時釋放部分輸出。

## 實作順序

### 1. 固定契約與測試入口

更新 roadmap 的 direct stream 描述和總預算預設值；在 `src/limits.[ch]` 定義新上限，為 `src/contents.[ch]` 訂出 context、owned result 與 free API。加入最小 unfiltered 單頁與空白頁 fixture，先寫會失敗的測試。

**驗收**：兩種頁面都有明確位元組輸出、長度、所有權與上限契約；`make test` 的既有基線仍通過。

### 2. 完成 M6a 的單一 stream 路徑

從 Page `/Contents` reference 取得 stream，沿用 M3 的 raw bytes 與 `/Length`，回傳獨立 owned buffer；缺少 `/Filter` 時逐位元組保留原值。測試 direct／indirect `/Length`、零長度、非 stream 目標、錯誤 reference、NUL bytes 與 reader 游標不變。

**驗收**：單一 unfiltered stream 的輸出與 fixture 完全相同；錯誤路徑沒有洩漏，`make test` 通過。

### 3. 完成 M6a 的 Contents array 路徑

支援直接 array 及指向 array 的 reference；依陣列順序逐一解碼 stream，間隔補 newline，空 array 輸出空結果。拒絕 nested array、非 reference 元素及非 stream 目標；測試跨多頁的 total budget 與串接長度 overflow。

**驗收**：包含空 stream 的多 stream fixture 仍保留正確順序與分隔；limit 和 malformed cases 有明確 error code，`make test` 通過。

### 4. 完成 M6b 的 FlateDecode

新增可重用的 filter 解碼層，使用 zlib 的增量 inflate，並在每次輸出增長前檢查單 stream 與 total budget。先測單一 `/FlateDecode`，再接入 Contents；涵蓋有效資料、空資料、截斷／損壞資料、解壓膨脹超限、未知 filter、filter array 與非預設 `/DecodeParms`。

**驗收**：[tests/hello.pdf](../tests/hello.pdf) 的 Contents 可解碼；每個失敗類別都有 fixture／單元測試，`make test` 通過。

### 5. 增加可觀察的整份 PDF 驗收

新增不輸出任意 binary bytes 的 `--dump-contents document.pdf` 摘要模式，顯示頁序與 decoded byte length；逐頁讀取與釋放。更新 Makefile 的 `-lz`、單元測試、[fixture runner](../tests/run-fixtures.sh)、golden 和 roadmap 狀態。

**驗收**：合法及 malformed fixture 的 exit code、stdout、stderr 符合 golden；`make`、`make test`、`make asan` 全通過，既有 M0–M5 模式回歸不變。

## 檢查點與風險

- 任務 2、3 後：檢查 borrowed input 與 owned output 的生命週期、array 邊界和重複呼叫；跑完整 `make test`。
- 任務 4、5 後：檢查 zlib 中途失敗、壓縮炸彈與累計預算；跑 `make asan`，再審查 M6 是否符合 roadmap。
- 測試 PDF fixture 以靜態檔提交；測試執行時只需 C 工具鏈、zlib 和 POSIX shell，不依賴 fixture 產生器。
