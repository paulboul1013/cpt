# 路徑與影像的來源文字提取

`test_git.pdf` 使用的 33 種指令均已支援文字提取。這次新增剩餘的 17 種指令，
沿用工作區已有的中文、ExtGState、色彩與空 ActualText 支援。

## 指令契約

| 指令 | 驗證 | 提取行為 |
|---|---|---|
| `w` | 一個有限 number，至少 0 | 不保留線寬 |
| `J j` | 一個 integer，0–2 | 不保留端點或接合樣式 |
| `M` | 一個有限 number，至少 1 | 不保留斜接上限 |
| `d` | array＋有限 phase；array 為非負 number、非空時不能全零；空 array 要求 phase=0 | 不保留虛線樣式，非空 array 可搭配負 phase |
| `m l c h re` | 固定參數數量與型別；`l c h` 要求 current point | 路徑不改變文字矩陣或 CTM |
| `W W*` | 零參數 | 不依裁切區域刪除來源文字 |
| `n S f B*` | 零參數；清除 current point | 不繪製、不產生文字 |
| `Do` | 一個 name；正式 bridge 另查找並驗證 Image XObject | 不解碼影像像素，不做 OCR |

path、paint、clip 與 Do 不得出現在 BT／ET 內。線條樣式可在 BT／ET 內外設定。
current path 不屬於 q/Q 保存的 graphics state，跨同頁 Contents stream 延續，換頁重設。
這是來源文字提取政策：即使文字在裁切區外，仍會輸出，與既有保留 Tr=3 文字的政策一致。
不宣稱提供繪圖、可見性判斷或完整 PDF renderer。

參數與路徑依 Adobe PDF 32000-1:2008 §8.4–8.5；負 dash phase 的處理補充見
[PDF Association 對規格澄清的說明](https://pdfa.org/why-pdf-2-0-is-the-new-pdf-bible/)。
提取器只驗證樣式，不計算虛線，因此不需要 phase 正規化。

## 模組與資源

`content_interpreter.[ch]` 負責語法、值域、BT／ET 與 current point。
`text_state.c` 接受已驗證的繪圖指令，不儲存與提取無關的樣式與路徑座標。
它仍拒絕沒有資源解析器的 Do。

`font_text.c` 在進入 M8 前呼叫 `pdf_xobject_check_image()`。
`xobject.[ch]` 從有效的 Resources／XObject 字典依 length-aware name 查找資源，
支援名稱中的 NUL。所有物件由 document 擁有，驗證器只借用。

目標必須是 stream dictionary。可選 Type 必須為 XObject，Subtype 必須為 name。
Image 的 Width／Height 必須為正整數，允許間接欄位。資源表與欄位不得是 stream。
參照鏈採常數記憶體的循環偵測，並受 max_nesting_depth 限制；字典受 max_container_entries 限制。
錯型、缺資源、自循環為 malformed；深度或容器上限為 resource limit；Form 與其他 subtype 為 unsupported。
錯誤同時保存頁面 offset、decoded 指令 offset 與最近的物件 offset。

不檢查影像像素、色彩空間、filter 與 mask 的渲染有效性，也不解壓它們。
Form、PS、inline image 與本次子集以外的繪圖指令維持拒絕，避免遺失 Form 內的文字。
未新增快取或字型身分，既有 q/Q、中文解碼與空 ActualText 語意由原層負責。

設計採 M7 語法、M8 文字幾何、M9 資源驗證的既有分層。
把所有 Do 當 no-op 會遺失 Form 文字；為 Image 引入完整 renderer 則不符合文字提取需求。

## 測試與實際檔案

`tests/drawing_test.c` 驗證全部新增指令、錯型、錯誤參數數量、範圍、current point、
q/Q 不恢復路徑、裁切區外文字，以及繪圖前後文字與座標不變。
Image 測試包含資源表、Type／Subtype、缺資源、非 stream、尺寸、自循環、多節循環、
合法與超限參照鏈、間接尺寸、scalar 指向 stream，以及帶 NUL 的名稱。

`tests/make-drawing-fixtures.py` 可重建 9 份靜態 PDF 與 15 筆 golden cases。
`tests/run-cli-tests.sh` 驗證後頁失敗時 stdout 無部分內容，既有 `-o` 檔案保持不變。
核心 `make test`／`make asan` 不依賴 Python、Poppler 或真實書籍。

`tools/verify-test-git.py` 另驗證未修改的原檔 hash、479 頁、全部 33 種指令、文字與座標，
並逐頁對照 Poppler 字元計數。實測輸出與畫面見 [全書驗收](../output/pdf/test-git-support/README.md)。
它檢查字元完整性，不把字元計數相同當成閱讀順序相同。
