---
title: 本 repo 與 Poppler 的現況差異
subtitle: test_git.pdf 已可提取，但功能範圍與排版仍有差距
theme: shadcn
template: sheet
cols: 2
---

## A 功能與整合方式 {span=2}

| 面向 | 本 repo 的 pdftext | Poppler |
|---|---|---|
| 核心用途 | 以 C11 實作 PDF 原生文字提取 | PDF 函式庫，另提供命令列工具 |
| 輸出 | UTF-8 文字、TextItem JSON 與解析診斷 | 文字提取、頁面渲染及多種文件 API |
| 字型 | Simple Font、Identity-H 與指定 ToUnicode 子集 | 提供更廣的字型與編碼處理 |
| 排版 | 單欄、水平，依基線與 x 座標排序 | 提供預設、layout 與 raw 等模式 |
| 旋轉文字 | 非零頁面 Rotate、旋轉或鏡像文字會拒絕 | 提供頁面與文字處理、渲染介面 |
| 影像與圖形 | 驗證指定指令及 Image XObject，不渲染像素 | 可渲染頁面、讀取影像與圖形內容 |
| API | 有內部 C 模組介面，尚無對外穩定 SDK 承諾 | 提供 C++、GLib、Qt 等 API |
| 依賴與修改 | 核心依賴 libc、libm、zlib，可直接修改規則 | 整合既有函式庫，依所選 frontend 配置 |

本 repo 的現行範圍依 [README](../../../README.md) 與原始碼。
Poppler 的功能與 API 依 [官方文件](https://poppler.freedesktop.org/api/glib/)。
各 API 有不同依賴，不能把所有 frontend 的依賴視為相同。

## B 同一份 PDF 的速度

| 版本 | 耗時中位數 | 每秒頁數 |
|---|---:|---:|
| 本 repo 預設，未最佳化 | 1.659 秒 | 289 |
| 本 repo 使用 -O2 | 0.861 秒 | 556 |
| Poppler pdftotext 24.02.0 | 0.611 秒 | 784 |

同一台機器、479 頁、36.96 MB 的 test_git.pdf。
每個版本先暖機，再各跑 7 次。
所有版本依序執行，stdout 導向 /dev/null。
不計編譯時間，不測冷快取或其他文件。

本 repo 開啟 -O2 後，耗時仍為 Poppler 的 1.41 倍。
最佳化前後的輸出逐 byte 相同。
來源：[完整實測紀錄](speed/result.json)。

## C 提取結果有多接近

兩者都完成全部 479 頁的提取。
本 repo 輸出 325,374 bytes，Poppler 輸出 325,654 bytes。

逐頁去除空白後，字元種類與數量幾乎一致。
唯一差異在第 161 頁，本 repo 多保留一個連字號。
來源碼的 `# ---` 在 Poppler 合併換行後成為 `# --`。

```callout warn 字元完整不代表順序正確
目前兩者的閱讀順序、空白與換行並不完全相同。
字元計數不能區分 ABC 與 CBA。
這次驗收不能證明整本書逐行排版都正確。
```

來源：[驗收報告](README.md) 與 [逐頁比較結果](result.json)。

## D 本 repo 目前的主要缺口 {span=2}

- 尚不支援加密、xref stream、object stream 與 incremental update。
- 尚不支援 Form XObject、直書與旋轉文字。
- 尚不支援非空 ActualText、完整 CJK Encoding CMap 與多欄偵測。
- 裁切指令不過濾來源文字，影像也不執行 OCR。
- 本次的 33 種指令覆蓋，只代表 test_git.pdf 使用的指令集合。

通用文件的相容性仍是本 repo 接下來要補的部分。
目前沒有證據顯示本 repo 在通用提取品質或速度上優於 Poppler。
它提供的控制點是解析流程、錯誤政策、資源上限與診斷資料。

Poppler 本身也可修改與擴充。
差異在於本 repo 可直接沿現有 C 模組修改自己的規則。

官方來源：[Poppler 與多種 API](https://poppler.freedesktop.org/)、
[C++ 介面](https://poppler.freedesktop.org/api/cpp/)、
[頁面文字與渲染 API](https://poppler.freedesktop.org/api/glib/poppler-Poppler-Page.html)。
實測使用 24.02.0，官方網站目前文件的版本與它不同。
