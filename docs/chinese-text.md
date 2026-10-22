# 中文 PDF 文字提取

## 範圍與驗收

新增水平 `/Type0`、`/Identity-H`、一個 CIDFontType0 或 CIDFontType2
descendant，且有 ToUnicode stream 的文字提取。支援 `begincodespacerange`、
`beginbfchar`、`beginbfrange` 的字串與陣列形式，UTF-16BE 代理對及一對多映射。
CID 字寬支援 `/W` 兩種形式及 `/DW`，省略 DW 時依 PDF 規格使用 1000。
Simple Font 也可使用單 byte ToUnicode。維持既有單欄水平排列與全部成功才輸出。

不新增 OCR、預定義 CJK Encoding CMap、Identity-V、UseCMap、xref stream、
圖形指令或多欄重建。無映射的完整字元輸出一個 U+FFFD；截斷的雙 byte
字元、錯誤 UTF-16 與損壞 CMap 回報格式錯誤。

## how：目前執行流程與所有權

以下是修改前程式可直接確認的事實：

- `src/main.c::extract_text()` 開啟 document，呼叫
  `pdf_text_items_extract()`、`pdf_plain_text_build()`，全部成功後寫 stdout 或檔案。
- `src/text_items.c::pdf_text_items_extract()` 載入頁樹、解碼 Contents，逐頁呼叫
  `pdf_font_text_page_interpret()`。document 擁有 reader、xref 與 resolved object cache。
- `src/font_text.c::visit()` 在 Tf／Q 後選取 active font；每頁 font context
  擁有 immutable font handles，text state 擁有文字矩陣、字距與 q/Q stack。
- `src/text_state.c::show_string()` 修改前逐 byte 呼叫 metrics、更新矩陣，
  產生借用的 raw event。`font_text.c::consume()` 產生暫時 UTF-8 並在 callback 後釋放。
- `src/text_items.c::consume()` 將內容複製進 caller-owned arena，document 關閉後仍有效。
  `reading_order.c` 只讀 TextItem，不應知道 CID 或 CMap。

## why：原有限制的證據

commit `a7d37d6` 明示 M9 限定單 byte Type1／TrueType，拒絕 Type0、ToUnicode
及未知字寬，避免猜測。roadmap §9 記載字寬必須依原始 code 查找，不能依 UTF-8。
commit `1a88c8d` 將 metrics 留給 M9 注入，沒有產品用的固定字寬 fallback。
這是已記錄的 v1.0 範圍限制；沒有找到其他必須永久維持單 byte 介面的理由。

## architect：介面與替代方案

選擇讓 metrics 接收剩餘 bytes，回傳消耗的 byte 數與一個字元的字寬。
font module 負責 `pdf_font_read_code()`；metrics、UTF-8 解碼與 debug trace
共用它。text state 不解析 CMap，Tw 只套用單 byte 0x20，Tc 每個字元套用一次。

另一方案是在 text state 加 Type0 專用分支，或讓逐 byte callback 暫存半個字元。
兩者都讓幾何層知道編碼或跨 callback 隱藏狀態，因此不採用。

`cmap.[ch]` 擁有有界且排序的 Unicode 映射；font handle 擁有 CMap、CID 字寬。
CMap stream 借用 document 物件，解壓 bytes 在解析後立即釋放。所有失敗路徑
由 context destroy 清理，不發布半完成字型。沿用既有 token、container、stream
與 total decoded size 限制。

## blast-radius：影響與安全條件

- metrics callback 簽名影響 `font_text.c`、`text_state_test.c`、`geometry_test.c`。
  必須一次遷移所有呼叫者；既有幾何 golden 必須逐 byte 不變。
- font subtype／encoding 診斷增加新值，TextItem JSON 與 font trace 需正確命名。
- 字元寬度若仍逐 byte，Tc、Tw、TJ 後位置及閱讀順序都會錯誤。
  新測試直接核對 TextItem 座標與寬度，不能只驗證 UTF-8。
- 原先以 ToUnicode 當 unsupported 的案例會改為解析其內容，須更新錯誤分類。
  後頁失敗時仍須清空先前文字，保留原子寫檔行為。
- CMap range 展開與解壓必須有界；不支援 UseCMap 時明確拒絕，不能部分解碼。
- `pdf_font_context_create()` 新增借用的累積 CMap 解壓計數器；所有呼叫者一起遷移。
  bridge 透過 `pdf_font_text_totals.cmap_bytes` 跨頁計數，獨立 font context 可傳 NULL。

## 修改前基線

工作區乾淨，未找到 CI 設定檔。`make` 成功。
`./pdftext tests/fixtures/visual-m7-text.pdf` 成功輸出：

```text
PDF visual comparison
Line 2: Hello PDF
AB
SCALED
Restored after Q
```

既有 `tests/hello.pdf` 提取失敗，exit 4，stdout 空白，原因是 byte 7437
的 unsupported content operator，與中文字型無關，本次不擴大處理範圍。

新增 `chinese-raw.pdf` 後，修改前的 CLI exit 4、stdout 空白，stderr 為：

```text
content: unsupported error at byte 145: decoded byte 10: font byte 352 (font): unsupported font subtype /Type0
```

新增的 `chinese_test.c` 也在同一個 Type0 拒絕點失敗，確認測試能抓到尚未實作的功能。

## 驗證方法

先建立 Chinese fixtures，確認修改前因 Type0 失敗，再加入 CMap／CID／幾何測試。
執行 `make test`、`make asan`、clang 編譯與端到端 CLI，檢查最終 diff。
fixtures 產生器可重跑，核心測試直接讀取已提交的 PDF，不依賴 Python。

規格來源：[Adobe PDF Reference 1.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)，
§5.2.2、§5.6.3–5.6.4、§5.9.2。

實作時另核對 [PDF 32000-1:2008](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf)
§9.3.3、§9.7.4–9.7.6、§9.10.3：Tw 不適用多 byte code，Identity-H 使用
big-endian 雙 byte CID，ToUnicode 的 bfrange 字串目的值只能遞增最後一個 byte。

## 驗證結果

- 修改前 `make test` 的 87 筆 fixtures 與其餘測試通過。
- 修改後 `make -B test`、`make -B CC=clang test` 與沙箱外 `make -B asan` 通過。
  fixtures 增為 97 筆；ASan、UBSan、LeakSanitizer 均啟用。
- 新 CMap 單元測試涵蓋 3000 個固定種子的變異／截斷輸入，包含失敗清理。
- 中文整合測試核對 raw／Flate、CIDFontType0／2、Simple Font ToUnicode、
  UTF-16BE 代理對、一對多映射、DW／W、Tc／Tw／TJ、q/Q 字型切換、
  後頁失敗丟棄前頁輸出，以及 stream／儲存空間上限。
- 同一字型的 CMap 在第二頁重複解壓，仍累計到文件上限；超限時清空前頁輸出。
- `tests/make-chinese-fixtures.py` 重跑後，22 個 PDF、golden 與 manifest 檔內容一致。
- [真實嵌入字型視覺驗收](../output/pdf/chinese-comparison/README.md) 中，本工具與
  Poppler 均輸出「中文提取測試」，TextItem advance 與真實字寬的獨立計算同為 144。
- 既有視覺範例輸出保持一致，`hello.pdf` 仍在原本的圖形指令處 exit 4。
- `git diff --check` 通過。編譯器沒有 warning；環境檔案時間有差異，最終以 `-B`
  強制重建，避免 Make 沿用未更新的執行檔。

未驗證項目：環境未安裝逗號小數點 locale，原有 locale 測試略過；沒有 CI 設定檔可執行。
真實嵌入字型畫面驗證使用 CIDFontType2；CIDFontType0 目前只有受控 fixture 的提取驗證。
未進行大量第三方中文 PDF 相容性測試，支援範圍仍以上述 Identity-H 子集為準。

## 相容性與資源歸屬

原先空白 ToUnicode stream 的後頁失敗案例，現在由 unsupported／exit 4 改為
malformed／exit 3，所有 golden 與原子寫檔測試同步更新。U+FFFD 警告改以
character 為單位，避免將雙 byte CID 的一個未映射字元誤稱為一個 byte。

每份文件以 `max_total_decoded_size` 限制所有 ToUnicode stream 的累積解壓大小，
包含同一 stream 在不同頁的重複解壓。每頁 font context 另以同一上限限制所有
CMap／CID 字寬的保留空間。單一 source domain 最多
65536 個 code，range 展開仍受 `max_container_entries` 限制。文件級 UTF-8 與
event 計數沿用原有 bridge counters，CMap 解壓量也在 bridge totals 跨頁累計；
context 和 TextItem 的所有權不變。
