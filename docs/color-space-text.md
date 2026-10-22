# 色彩空間指令的文字提取政策

`cs` 與 `CS` 各接受一個 name 參數，BT／ET 內外均可使用。M7 驗證個數與型別，
將操作計入 OPS；M8 略過色彩效果，正式 font bridge 沿用 M8 行為。
不查詢 `/Resources /ColorSpace`，因此 resource name 是否存在、色彩空間定義是否合法
均不在此驗證範圍。沒有新增色彩狀態或配置資源，參數只在 callback 期間借用。

缺參數、多參數或非 name 參數回報 malformed，保留 Page／decoded byte 定位。
名稱沿用 length-aware lexer，支援 NUL 與空名稱；既有 token／container 上限仍適用。
`scn`／`SCN` 的語法支援見下節。`sc`／`SC` 等其他色彩指令及 path、clipping、
XObject 仍回報 unsupported。

此政策提取文字內容與座標，不判斷其最終色彩可見性。專案既有契約也會提取 `Tr=3`
的不可見文字；`cs`／`CS` 不改變字型、字元解碼、advance 或文字矩陣。
規格依據為 [Adobe PDF Reference 1.7 §4.5.7、表 4.24](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf)。

## 測試

先新增 M7 與裸 M8 測試，兩者均在舊程式的 unsupported 拒絕點失敗，再新增實作。

- M7 測試核對兩種操作的分派、參數個數／型別、BT／ET 內外、NUL／空名稱、
  OPS 與錯誤位置；其他色彩指令仍拒絕。
- 裸 M8 可接受語法正確的指令，且不因此建立字型或改變初始矩陣。
- bridge 測試核對 q/Q 巢狀、跨 Contents 的 name／operator、Tj／TJ 前後的文字、
  字寬、origin、CTM、文字／行矩陣、rendering matrix、字級、文字參數與 source order。
- 中文與英文 fixture 和不含 cs／CS 的對照結果一致，UTF-8／event／CMap counters
  也一致。沒有 Tf 的文字顯示仍失敗。
- 新增 5 份小型 PDF 與 8 筆 manifest cases，總數由 107 增為 115。
  錯型、錯參數數量與後頁失敗均無部分文字或 TextItem 輸出，既有 `-o` 檔案保留。

可用 `python3 tests/make-cs-fixtures.py` 重建 PDF、golden 與 manifest；核心測試不依賴
Python、Poppler 或真實書籍。

完整驗證已通過：

- `make -B test`，115 筆 fixtures 與全部單元／整合測試。
- `make -B asan`，ASan／UBSan／LeakSanitizer 無錯誤。
- `make -B CC=clang test`，115 筆 fixtures 與全部單元／整合測試。
- `git diff --check`；fixture 產生器重跑後，5 份 PDF、5 份 golden 與 manifest
  共 11 個檔案逐 byte 一致。

編譯器無 warning。Make 提示環境既有的 clock skew，已以 `-B` 強制重建；
逗號小數 locale 未安裝，既有 locale 檢查明示略過。

## 原始 PDF 復測

原始 `test_git.pdf` 未修改，SHA-256 仍為
`d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70`。
純文字與 `--dump-text-items` 均通過先前的 cs／CS，改在 decoded byte 125 的 `scn`
停止，exit 4、stdout 0 bytes。實測輸出及 JSON 見
[test_git-cs-results](../output/pdf/chinese-comparison/test_git-cs-results/README.md)。

這次使用程式與數值對照驗證，未重新渲染 PDF 或執行圖片視覺比對。

## scn／SCN 文字提取支援

`scn` 與 `SCN` 接受至少一個參數：純數值序列、數值序列加上尾端 pattern name，
或單獨的 pattern name。M7 驗證型別與順序並計入 OPS；BT／ET 內外均可使用。
參數為空，或 name 出現在數值之前／有多個 name，均為 malformed。
參數沿用 lexer、token 與 operand 數量限制；名稱保留長度，可含 NUL 或為空。

這兩個指令只改變色彩。M8 與正式 bridge 略過其效果，不建立色彩狀態、不查詢
ColorSpace 或 Pattern 資源、不執行 pattern stream，也不檢查色彩空間對應的
分量數或值域。數值個數不限於 RGB 的三個，支援 DeviceN 等較多分量的語法。
此為文字提取子集，不是完整的色彩資源驗證或渲染。
參數形式依據 [PDF Association 的運算子參考表](https://pdfa.org/download-area/cheat-sheets/OperatorsAndOperands.pdf)。

新增測試先在舊版 M7 與裸 M8 的 unsupported 處失敗，再加入實作通過：

- M7 核對 scn／SCN 分派、1／3／4／7 個數值、純 name、尾端 name、NUL／空名稱、
  BT／ET 內外、OPS、錯誤位置與 operand 上限。
- bridge 比對中英文的 UTF-8、event／CMap counters、字寬、origin、文字參數、
  CTM、文字／行矩陣與 rendering matrix。涵蓋 q／Q、Tj／TJ 與跨 Contents 參數。
- 5 份新增 PDF、8 筆 manifest cases，後頁錯誤時不輸出部分文字或 TextItem，
  既有 `-o` 檔案保持原內容。

`python3 tests/make-scn-fixtures.py` 可重建新增 PDF、golden 與 manifest。
`make test` 與沙箱外的 `make asan` 通過，共 141 筆 fixtures 及所有單元／整合測試。
沙箱內 LeakSanitizer 受 ptrace 限制，已在沙箱外完成 ASan／UBSan／LeakSanitizer 驗證。

原始 `test_git.pdf` 的 SHA-256 不變。純文字、`--dump-text-items` 與 `--dump-content`
都已通過 decoded byte 125 的 `scn` 與後續 `SCN`，目前停在 byte 149 的 `w`，
exit 4、stdout 0 bytes。尚未完成整份書籍文字提取；本次沒有執行真實渲染。
實際輸出與第一段解碼內容見 [scn 復測紀錄](../output/pdf/chinese-comparison/test_git-scn-results/README.md)。
