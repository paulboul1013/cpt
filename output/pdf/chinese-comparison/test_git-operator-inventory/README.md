# test_git.pdf 運算子統計

掃描目前工作區的 `output/pdf/chinese-comparison/test_git.pdf` 全部 479 頁。
本次只新增診斷工具與報告，沒有修改提取器的支援政策。

- 原檔大小：36,959,295 bytes。
- SHA-256：`d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70`。
- 全部頁面的 Contents 已解碼並掃描，合計 11,058,457 bytes。
- 共 33 種、1,037,334 次運算子。
- M7 接受：12 種、715,364 次。
- 目前回報 unsupported：21 種、321,970 次。
- 沒有未知運算子、M7 格式錯誤或 dispatch 資源錯誤；沒有 inline image。

## 尚未支援

依第一次出現順序排列。頁數為 PDF 頁碼；offset 是該頁串接及解碼後 Contents 的 byte offset。

| 指令 | 用途 | 出現次數 | 涵蓋頁數 | 首次頁碼 | 首次 decoded byte |
|---|---|---:|---:|---:|---:|
| `scn` | 填色顏色 | 50,509 | 479 | 1 | 125 |
| `SCN` | 描邊顏色 | 18,817 | 479 | 1 | 143 |
| `w` | 線寬 | 18,817 | 479 | 1 | 149 |
| `J` | 線端樣式 | 18,817 | 479 | 1 | 153 |
| `M` | 斜接上限 | 18,817 | 479 | 1 | 157 |
| `j` | 線段接合樣式 | 18,817 | 479 | 1 | 161 |
| `d` | 虛線樣式 | 18,817 | 479 | 1 | 168 |
| `m` | 移動路徑起點 | 18,397 | 479 | 1 | 180 |
| `l` | 直線路徑 | 73,261 | 479 | 1 | 195 |
| `h` | 封閉路徑 | 18,320 | 479 | 1 | 242 |
| `W*` | 奇偶規則裁切 | 18,219 | 479 | 1 | 244 |
| `n` | 結束路徑但不繪製 | 18,296 | 479 | 1 | 247 |
| `re` | 矩形路徑 | 5,211 | 479 | 1 | 420 |
| `f` | 填滿路徑 | 5,211 | 479 | 1 | 423 |
| `Do` | 呼叫 XObject | 252 | 180 | 1 | 946 |
| `BDC` | 帶屬性的 marked content 開始 | 559 | 296 | 3 | 2512 |
| `EMC` | marked content 結束 | 559 | 296 | 3 | 2564 |
| `W` | 非零繞數規則裁切 | 77 | 13 | 4 | 1197 |
| `S` | 描邊路徑 | 77 | 13 | 4 | 1790 |
| `c` | 三次貝茲曲線 | 96 | 5 | 22 | 3274 |
| `B*` | 奇偶規則填色並描邊 | 24 | 5 | 22 | 3482 |

## 已接受的指令

目前文件使用且 M7 接受的指令為 `gs`、`cs`、`CS`、`cm`、`q`、`Q`、`BT`、`Tf`、
`Tm`、`Td`、`Tj`、`ET`。`gs` 僅支援先前完成的 ExtGState 子集，`cs`／`CS` 僅驗證
語法後略過色彩效果。各指令的完整次數見 `summary.tsv`。

額外檢查：

- 51,240 次 `gs` 的資源驗證全部通過目前 ExtGState 子集。
- 252 次 `Do` 分布在 180 頁，指向 248 個不同的 Image XObject；解析資源無錯誤。
  沒有呼叫 Form XObject，因此這份文件沒有需遞迴掃描的被呼叫 Form stream。
  本工具未解碼影像像素，也未做圖片視覺驗證。
- 559 次 `BDC` 的 property dictionary 全部帶有 `/ActualText`。
  後續處理這類指令時，需要另外評估其文字語意。
- 目前正式 CLI 仍在第 1 頁 decoded byte 125 的 `scn` 停止。

## 方法與範圍

`tools/pdf_operator_inventory.c` 重用 repo 的 M7 operand parser 與 dispatch 分類，
能區分 name、literal／hex string、array 與 dictionary，避免把 operand 內容誤算為指令。
診斷掃描在 unsupported 之後繼續計數，沒有讓產品提取器略過指令。
表中的支援狀態依目前工作區程式，不依尚未包含中文／gs／cs 修改的 HEAD。

這是運算子與呼叫資源清單，不等同整份文件已可提取。尚未完整驗證字型解碼、
文字幾何、裁切或 ActualText 的提取語意，未修改原始 PDF。

重跑：

```sh
python3 tools/inventory-pdf-operators.py \
  output/pdf/chinese-comparison/test_git.pdf \
  --out-dir output/pdf/chinese-comparison/test_git-operator-inventory
```

需要 C11 編譯器、Python 3、zlib 與 libm；不需要第三方 Python 套件。
工具先驗證字串／名稱／容器中的指令樣文字不會被誤算，並確認遇到 inline image
會明確回報掃描不完整，然後才掃描輸入 PDF。

`result.json` 保存完整每頁分布、XObject 物件編號、ExtGState 驗證、原檔與支援清單
來源 hash；`summary.tsv` 為摘要表。`operators.tsv.gz` 是完整掃描紀錄，其中 operator
與 resource name 使用 hex；`scan.stderr` 為實際錯誤輸出。

驗證：GCC 與 Clang 各自重建工具並掃描，`result.json` 完全一致；壓縮原始紀錄
中的 operator 筆數也等於 1,037,334。`git diff --check` 通過。
