# PDF 基礎：從檔案位元組走到可擷取的文字

這份筆記以 `pdftext` 的實作路徑為主線。PDF 首先是**以位元組儲存的物件圖**；頁面內容則是一串繪圖指令。看見文字、取得 Unicode 文字、還原閱讀順序，是三個不同問題。[來源：Adobe《PDF Reference 1.7》第 3、4、5 章](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)、[PDF Association：Tagged PDF FAQ](https://pdfa.org/resource/tagged-pdf-q-a/)

## 一張圖看整體

```text
PDF bytes
  ├─ Header：%PDF-1.x 或 %PDF-2.0
  ├─ Body：間接物件、stream
  └─ 檔尾：xref / xref stream → trailer → startxref → %%EOF
                         │
                         └─ /Root → Catalog → /Pages → Page
                                                    ├─ /Resources → Fonts、XObjects…
                                                    └─ /Contents → content stream
                                                                    ↓
                                                        繪製文字 → 字元解碼 → 閱讀順序
```

傳統 PDF 的基本檔案結構是 header、body、交叉參照表、trailer；PDF 1.5 起也能用 **xref stream** 取代傳統表格。`startxref` 是檔尾附近指向最新交叉參照資料的**位元組偏移量**。[來源：PDF Association《PDF Basics Cheat Sheet》第 2 頁](https://pdfa.org/wp-content/uploads/2023/08/PDF-Basics-CheatSheet.pdf)

## 1. 先認識物件語法

| 類型 | 例子 | 用途 |
|---|---|---|
| scalar | `true`、`null`、`42`、`3.14` | 基本值 |
| name | `/Pages`、`/MediaBox` | 字典鍵、類型名稱；以 `/` 起頭 |
| string | `(Hello)`、`<48656C6C6F>` | 位元組字串；不能預設為 UTF-8 |
| array | `[1 0 R /Fit]` | 有序值 |
| dictionary | `<< /Type /Page /Parent 2 0 R >>` | 以 name 為鍵的設定或結構 |
| indirect object | `7 0 obj ... endobj` | 可被其他物件參照的定義 |
| indirect reference | `7 0 R` | 物件編號 7、generation 0 的參照 |
| stream | `<< /Length 20 >> stream ... endstream` | 內容、影像、字型、xref 等位元組資料 |

**間接物件**讓物件形成圖；`7 0 R` 不是物件內容，需經 xref 找到其定義。stream 必有字典，`/Length` 指定 stream data 的長度；資料可能經 `/Filter` 壓縮或編碼，且 stream 本身必須是間接物件。解析時不能把 stream 內的任意位元組當作一般 PDF token。[來源：PDF Association《PDF Basics Cheat Sheet》第 2 頁](https://pdfa.org/wp-content/uploads/2023/08/PDF-Basics-CheatSheet.pdf)、[Adobe《PDF Reference 1.7》§3.2、§3.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)

## 2. 如何找到文件入口

從檔尾讀取 `startxref`，跳到最新 xref，取得物件位置與 trailer。trailer 的 `/Root` 指向 **Document Catalog**；Catalog 的 `/Pages` 指向 **page tree**。樹的中間節點是 `/Pages`，葉節點是 `/Page`。每個 Page 可透過 `/Contents` 指向一個 stream 或 stream 陣列，並從自身或父節點取得可繼承的頁面屬性（例如 `/Resources`、`/MediaBox`）。[來源：Adobe《PDF Reference 1.7》§3.4、§3.6](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)

```text
trailer << /Root 1 0 R ... >>
1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj
2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj
3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R ... >> endobj
```

這是**結構示意**，不是可直接開啟的完整 PDF：它省略了 header、xref、有效 offset 及 stream data。實作 resolver 時要檢查循環參照、錯誤型別、越界 offset 與不合法的 page tree，並設資源上限；這些是本專案讀取不可信輸入時的實作要求。

## 3. xref、更新與兩種儲存方式

- **傳統 xref table** 記錄物件編號、generation、offset，以及使用中／已釋放狀態；`trailer` 在表格之後。`pdftext` v1.0 只支援這種形式（M4 起）。
- **xref stream** 從 PDF 1.5 起可將交叉參照資訊放進 stream（`pdftext` v1.0 回報 unsupported）；不能假設每個有效 PDF 都有純文字 `xref` 關鍵字。PDF 1.5 起的 **object stream** 還能容納多個非 stream 物件。
- **增量更新**把新的物件、xref 與 trailer 加在檔尾；新 trailer 的 `/Prev` 指向前一版 xref。讀取時須從最新版本往前追，並以較新的物件定義為準。舊位元組仍可能存在於檔案中。

[來源：PDF Association《PDF Basics Cheat Sheet》第 1–2 頁](https://pdfa.org/wp-content/uploads/2023/08/PDF-Basics-CheatSheet.pdf)、[Adobe《PDF Reference 1.7》§3.4.5、§3.4.6、§3.4.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)

## 4. Content stream 並不是純文字

Page 的 `/Contents` 是**繪圖操作序列**。例如 `BT`/`ET` 開始／結束文字物件、`Tf` 選字型與大小、`Tm` 設文字矩陣、`Tj`/`TJ` 顯示字元碼；`q`/`Q` 保存與恢復圖形狀態。繪製位置由文字狀態、文字矩陣與目前轉換矩陣共同決定。頁面的 `/Resources` 將 `/F1` 之類的名稱連到實際字型；`Do` 等操作也可能引用 XObject。[來源：Adobe《PDF Reference 1.7》第 4、5 章，尤其 §5.3–§5.5](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)

```text
BT /F1 12 Tf 72 720 Td (Hello) Tj ET
```

這表示「用某字型在某位置畫出字元碼」，**不保證** `(Hello)` 可直接當作 Unicode 文字讀取。掃描頁甚至可能只有影像，沒有可擷取的原生文字；此時需要 OCR，已超出單純 PDF 文字解析的範圍。[來源：Adobe《PDF Reference 1.7》§5.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)

## 5. 從字元碼到文字，還要解決順序

1. **解 stream**：依 `/Filter` 取得內容位元組，例如 `FlateDecode`；解碼後仍是 PDF 繪圖語法。[來源：Adobe《PDF Reference 1.7》§3.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)
2. **執行文字操作**：解析 operands、文字狀態與座標，取得顯示時所用的字元碼與字型。[來源：Adobe《PDF Reference 1.7》§5.2–§5.5](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)
3. **映射 Unicode**：先看字型的 `/ToUnicode` CMap；必要時使用標準 encoding／字形名稱等規則。PDF 也可用 `/ActualText` 提供替代文字。字形能正確顯示，不代表一定能可靠轉成 Unicode。[來源：Adobe《PDF Reference 1.7》§5.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)
4. **還原閱讀順序**：content stream 的操作順序可能不同於人眼閱讀順序。Tagged PDF 的結構樹可提供語意與邏輯順序；缺少標記時，只能依位置等線索推估，表格、多欄與浮動內容特別容易出錯。[來源：PDF Association《Tagged PDF Best Practice Guide》§3.2](https://pdfa.org/wp-content/uploads/2023/07/Tagged-PDF-Best-Practice-Guide.pdf)、[Tagged PDF FAQ](https://pdfa.org/resource/tagged-pdf-q-a/)

**三種常被混淆的編碼**：PDF string 的文字編碼、content stream 中交給字型的 character code、輸出用的 UTF-8 是不同層次。`/ToUnicode` 解的是「字型 character code → Unicode」這一段。[來源：Adobe《PDF Reference 1.7》§3.8.1、§5.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)

## 6. 讀取時還會碰到什麼

| 主題 | 快速理解 |
|---|---|
| 壓縮與 filter | stream 可能有一個或多個 filter；要按規格解碼，並限制解碼後大小。[Adobe《PDF Reference 1.7》§3.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf) |
| 加密 | 文件可能由 trailer 的 `/Encrypt` 指向加密字典；字串與 stream data 可能加密。解析器需辨識並明確處理，不能把密文當成普通內容。[Adobe《PDF Reference 1.7》§3.5](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf) |
| 數位簽章 | 與加密不同，簽章用於驗證指定 PDF 位元組範圍及其變更；增量更新會影響簽章解讀。[Adobe《PDF Reference 1.7》§8.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf) |
| PDF/A、PDF/UA | 這些是針對保存與無障礙等用途的額外符合性標準，不是另一種完全不同的檔案語法。[PDF Association《PDF Basics Cheat Sheet》第 1 頁](https://pdfa.org/wp-content/uploads/2023/08/PDF-Basics-CheatSheet.pdf) |

## 對照本專案的學習與實作順序

| 階段 | 先回答的問題 | 對照 roadmap |
|---|---|---|
| 位元組與語法 | token、object、stream 如何界定？ | M0–M3 |
| 尋址 | `startxref`、trailer、xref 如何找到間接物件？ | M4–M5 |
| 文件圖 | `/Root` 如何走到各 Page；哪些屬性要繼承？ | M5 |
| 頁面內容 | `/Contents` 如何解壓並解讀文字操作？ | M6–M8 |
| 語意文字 | 字元碼如何變 Unicode；順序如何推估？ | M9–M11 |

建議先拿一份**未壓縮、單頁、未加密**的小 PDF，手動沿 `startxref → trailer /Root → Catalog /Pages → Page /Contents` 走一次；再加入壓縮、增量更新、xref stream 與複雜字型。完整規格以 [ISO 32000-2:2020（PDF 2.0，PDF Association 免費提供）](https://pdfa.org/resource/iso-32000-2/) 為準；[Adobe《PDF Reference 1.7》](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf) 對本專案目前實作的傳統 PDF 結構也很適合作為逐章參考。
