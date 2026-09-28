# M8 實作計畫：Text State 與 Geometry

**驗證授權**：使用者已預先同意本計畫所有驗證指令（含建置、單元／整合測試、sanitizer、PDF 渲染與結果對照），代理應直接執行，不再要求使用者手動確認；若平台強制要求沙箱／權限核准，仍依工具規則處理，這份授權不會變更平台的核准設定。

## 目標與範圍

依 [roadmap 的 M8](../../pdftext_roadmap.md#8-m8text-state-與-geometry) 與 [Common Contracts](../../pdftext_roadmap.md#14-common-contracts)，消耗 M7 已驗證的操作序列，維護文字／圖形狀態，計算字串顯示前後的位置及座標變換。M8 不實作字型 Unicode 解碼、實際字型 metrics 查找、TextItem 與閱讀順序；這些分別屬於 M9–M11。

使用 [content_interpreter.h](../../src/content_interpreter.h) 的 visitor 接口接入，沿 `Contents decoded bytes → M7 → Text State → raw string geometry event` 流動。Content parser 不接觸字型編碼；Text State 不讀 xref、PDF 字型字典或整份檔案。字形寬度由明確的 metrics callback 提供：M8 測試使用可核對的測試 adapter，M9 再接入真正的 font module。

本階段優先完成可驗證的文字提取核心。`w`、path、color、clipping、marked content、Form XObject、inline image 與 xref stream 不因 M8 而擴充。`tests/hello.pdf`、`tests/compilerbook.pdf` 不作為 M8 成功驗收輸入；M8 完成也不表示它們已受支援；它們仍需納入截圖與拒絕點的對照報告，作為相容性缺口紀錄，而不是忽略。

## 開工前固定契約

### 1. 校正 roadmap 的狀態與 operator 範圍

- `BT` 只將 text matrix／text line matrix 重設為 identity；不清除字型、font size、spacing、leading、horizontal scale、rise、rendering mode 或 CTM。`ET` 結束文字物件，下一個 `BT` 再重設兩個文字矩陣。
- `q/Q` 保存／恢復 M8 所維護的 graphics state：CTM 及全部 text-state parameters。兩個文字矩陣不是 graphics state 的成員，不放進 q stack；沿用 M7 的 `q/Q/cm` 僅在文字物件外政策。
- M8 必須讓 M7 分派 `Tc`、`Tw`、`Tz`、`TL`、`Ts` 與 `Tr`，才能正確處理 spacing、換行、rise 及 `"` 的副作用。這些 text-state operators 與 `Tf` 一樣可以在 `BT/ET` 外設定。
- `Tr` 的 integer 範圍為 0–7；M8 接受 0–3 並保留 mode，4–7 涉及 glyph clipping，仍回 unsupported。0–3 的字串都交給 consumer，不在 M8 決定不可見文字是否應被提取。其餘未實作 operator 保持 M7 拒絕政策。
- `Tf` 的 font size 必須有限且大於 0，是本專案 v1.0 的支援限制；不是宣稱 PDF 規格禁止所有非正值。`Tc/Tw/TL/Ts/Tz` 接受有限數值；`Tz` 除以 100 儲存為比例，不自行新增正值限制。

實作前先將上述澄清寫回 roadmap 的 M8 與 M7 operator policy，M8 尚未通過完整驗收前不可標示完成。官方依據：[Adobe PDF Reference 第 4 章與 §5.2–5.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.4.pdf)、[PDF Association operators 表](https://pdfa.org/download-area/cheat-sheets/OperatorsAndOperands.pdf)。

### 2. 矩陣與座標慣例

統一使用 `double` 的六元素 affine matrix `[a b c d e f]`：

```text
x' = a*x + c*y + e
y' = b*x + d*y + f
identity = [1 0 0 1 0 0]
```

採 PDF row-vector 記法，定義 `compose(A, B)` 為先套用 A、再套用 B。不要只以模糊的「左乘／右乘」註解交接；函式及測試均寫明作用順序。新矩陣 M 的 `cm` 更新為 `CTM = M × CTM`，不是取代 CTM；`Tm` 則直接取代兩個文字矩陣。

- `Td(tx,ty)`：`Tlm = translate(tx,ty) × Tlm`，接著 `Tm = Tlm`；基準是行起點，不是上一個字串顯示後的位置。
- `TD(tx,ty)`：先設 `leading = -ty`，再做 `Td`。
- `T*`：執行 `Td(0,-leading)`。
- 顯示時 rendering matrix 為 `[font_size*hscale 0 0 font_size 0 rise] × Tm × CTM`。Rise 改變 glyph origin，不改變換行基準或字串的水平推進量。
- M8 輸出 default user-space 座標，初始 CTM 設 identity；不引入像素、DPI 或螢幕左上角座標。`/MediaBox` 的非零／負數原點不被自動扣除，也不當成 clipping 過濾文字。
- 所有 operand 轉換、矩陣乘法、推進量與結果均驗證 finite；數值運算溢位回 malformed，不能讓 NaN／Infinity 進入事件。允許 singular matrix，因本階段不需要矩陣反解。

官方矩陣依據：[Adobe PDF Reference §4.2.3、§5.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.4.pdf)。

### 3. 深模組與所有權

建議新增 `src/matrix.[ch]` 與 `src/text_state.[ch]`。前者提供小型、純函式的矩陣運算；後者在小型 interface 後集中狀態、q stack、字形推進與事件產生，不讓 main 或 font module 重做狀態運算。

Text State 的 interface 應包含 create/init、destroy、可直接交給 M7 的 operation visitor，以及 read-only snapshot／debug dump；具體 C 函式命名於任務 1 固定。每頁建立獨立 context，初始化如下：

| 狀態 | 初始值 |
|---|---|
| CTM | identity |
| text matrix、line matrix | BT 時 identity |
| font | 未設定；無預設字型 |
| font size | 未設定；第一次 Tf 成功後才可顯示 |
| character／word spacing | 0 |
| horizontal scale | 1，即 Tz 100 |
| leading、rise | 0 |
| rendering mode | 0 |
| q stack | 空 |

M7 operands 僅在 callback 期間有效。`Tf` 的 resource name 若需跨操作保留，必須複製 length-aware bytes；不可儲存 token/object 指標或假設名稱沒有 NUL。q stack 分享不可變、具清楚保留／釋放責任的 font-name 儲存，避免每次 q 複製一份大型名稱。Q、重新 Tf、callback 失敗與 destroy 都有單一釋放路徑。

q stack 受 `max_nesting_depth` 限制；所有容量乘法、font-name 長度加法、retain 計數與事件來源順序計數先檢查 overflow，資源／容量溢位回 resource-limit，配置失敗回 out-of-memory。integration 測試 collector 對整份文件累積的事件套用 `max_container_entries`，不能只限制每頁就無限累積。

Text State snapshot 及 raw string event 的借用有效期寫在 `.h`；需要保留的 consumer 必須複製。consumer 可以失敗中止；本頁先前事件不回滾，輸出 consumer 必須暫存。狀態運算失敗後 context 僅允許 destroy，不能繼續使用半更新狀態。

### 4. M8→M9 的 metrics seam 與 string events

採狹窄的 glyph-metrics callback：輸入為當前 font resource name 的 bytes 與一個原始 character-code byte，輸出為以 1000 units 正規化的水平 glyph advance，並可回傳既有 `pdf_error`。M9 adapter 自行持有 page resources／document 並處理字型解析；M8 不把 WinAnsi／Unicode mapping 放入此 callback。

這個 byte-wise interface 限定 v1.0 Simple Font 的單 byte、水平 writing；Type0／CIDFont／vertical writing 仍是範圍外，日後另行演進 interface。glyph width 必須 finite，不以 Unicode 字元數推進。對 code 0x20 套用 word spacing；不先將 code 解成 Unicode 空白。

每個 glyph 的 unscaled text-space 水平推進量：

```text
dx = (width_1000 / 1000 * font_size
      + char_spacing
      + (code == 0x20 ? word_spacing : 0)) * hscale
Tm = translate(dx, 0) × Tm
```

每個 TJ number 的推進量：

```text
dx = -adjustment / 1000 * font_size * hscale
```

TJ number 不額外套用 char／word spacing；正數通常讓下一字形往左，負數往右。`'` 是先 `T*` 再 Tj；`"` 先設定 word spacing、再 character spacing，然後 `'`，兩種 spacing 都會留給後續操作。

每個 Tj／引號操作中的 string 與每個 TJ string segment 產生一個 borrowed raw-string event：包含原始 bytes、font name、font size／spacing／mode、顯示前後 text matrix、rendering matrix、transformed origin／advance vector、operator decoded offset、segment index 與來源順序。TJ 相鄰字串不合併，number 只更新位置。不可將 glyph shape bounding box、font_size 或水平 dx 誤稱為已算出的實際頁面包圍盒。

所有 text-show operators（含空 Tj、空 TJ）均須先有合法 Tf。空字串不呼叫 metrics provider，事件保留零 advance；只有 numbers 的 TJ 依 font size 推進但不產生字串事件。非空字串若沒有 provider／沒有可用 metrics，回 unsupported font metrics，不使用固定寬度、零寬度或部分準確位置冒充成功。

M8 測試 provider 的寬度是明示的測試輸入，不能成為正式 CLI 的 fallback。真實字型 metrics 接入與文字解碼留給 M9。

### 5. 頁面屬性與錯誤

Pages 目前只保存有效 Resources／MediaBox，尚未保存 Rotate。擴充 `pdf_page` 與頁樹繼承：`/Rotate` 預設 0，可由 Page 覆寫父 Pages；透過 resolver 解 reference，型別必須 integer 且為 90 的倍數。避免 `abs(INT64_MIN)` 等溢位。

保留有效 rotation 的原始 integer；M8 沿用 roadmap 的嚴格限制，任何非 0 值（含 360）回 unsupported page rotation，不在本階段默默正規化或套用旋轉。型別錯誤／不是 90 的倍數回 malformed。由 geometry page 入口拒絕非零 rotation；既有 Pages／Contents debug 模式的成功輸出格式保持不變。

Text State 作為 M7 callback 時，以當前 operation 的 decoded offset 設 local error，交由既有 `pdf_content_interpret()` 統一轉成 Page／Contents 檔案 offset 與 decoded 訊息；不要重複轉換。頁面 Rotate 錯誤用 Page reference offset，不冒充 stream byte。錯誤碼、first-error-wins 與資源限制沿用 Common Contracts。

## 任務與驗收順序

### 任務 1：固定 contracts 與測試素材

**工作**：校正 roadmap，定義 matrix／Text State 的 public interface、metrics callback、事件借用與 failure lifecycle；先建立最小靜態 PDF fixtures，及通過 M7 seam 的 C 測試框架。不得引入正式字型 parser 或猜寬度。

**驗收**：interface 可回答每個 allocation 的 owner、每個錯誤的 offset 與 consumer 中止行為；metrics seam 可由一個明示測試 provider 使用；baseline `make test` 通過，新增行為測試先失敗再實作。

**依賴**：無。**檔案**：roadmap、`text_state.h`、測試與 fixtures；中等範圍。

### 任務 2：實作純矩陣運算

**工作**：identity、compose、point／vector transformation 與 finite 檢查，寫在 `matrix.[ch]`；不反解矩陣，不依賴 PDF parser。

**驗收**：identity／平移／縮放／旋轉／shear／singular 都可核對；非交換次序測試有已手算期望值；大數溢位不輸出非有限值。以明定 tolerance 比較 double。

**驗證**：單項 matrix 測試及 `make test`。**依賴**：1。**檔案**：`matrix.[ch]`、`matrix_test.c`、Makefile；中等範圍。

### 任務 3：狀態生命週期與 q/Q

**工作**：Text State 初始化／destroy、Tf 持有、BT／ET 矩陣重設、q/Q snapshots、cm 與 Tm/Td/TD/T* 運算；以 M7 visitor 串接。

**驗收**：font／leading 跨 BT 保留；文字顯示推進不影響 line matrix；nested q 中變更 font／CTM，Q 恢復全部已維護的 graphics state。font name 含 NUL、q 上限及失敗 destroy 不洩漏。

**驗證**：單項 Text State 測試與 `make test`；檢查點確認 M7 grammar validation 與 M8 numerical semantics 分工，沒有第二套 content parser。**依賴**：2。**檔案**：`text_state.[ch]`、`text_state_test.c`、Makefile；中等範圍。

### 任務 4：補齊文字設定 operators

**工作**：M7 enum／分派表加入 Tc/Tw/Tz/TL/Ts/Tr，從 unsupported 名單移除；M8 儲存並套用其狀態，完成 `"` 的兩項 spacing 副作用。更新原 `content-text-state.pdf`（12 TL）的 M7 golden 為成功，並以 clipping Tr 或 gs 等保留新的 unsupported fixture。

**驗收**：每個 operator 的 arity／type、BT 外使用與數值語意皆有測試；Tr 0–3 成功、4–7 unsupported、非整數與範圍外 malformed；Tf 非正值與數值 overflow 由 M8 拒絕。M7 `--dump-content` 保留語法摘要定位，不因是否有 metrics provider 而假造 geometry。

**驗證**：content／Text State 單項測試與全套 fixtures。**依賴**：3。**檔案**：M7 `.h/.c`、Text State 與對應測試；中等範圍，fixture 更新與程式分兩個可驗證步驟。

### 任務 5：glyph advance 與 raw-string geometry events

**工作**：透過測試 metrics provider 完成 Tj/TJ／引號操作、rendering matrix、origin／advance vector 及事件 visitor；以回呼拒絕、缺 metrics、mid-string metrics 失敗檢查 cleanup。

**驗收**：明示寬度與 spacing 推進公式正確；TJ 混合 strings／numbers、不合併相鄰 segments；同一個 string 的 metrics 驗證失敗時不發出該 string 的成功事件，先前事件可存在但 consumer 最終不可部分輸出。下一個 string 的 origin 使用更新後 Tm。

**驗證**：Text State 單項測試與 `make test`；檢查點確認沒有 Unicode decoding、font resource 查找或正式固定寬度 fallback。**依賴**：4。**檔案**：`text_state.[ch]` 與測試；小範圍。

### 任務 6：有效 Rotate 的頁樹繼承

**工作**：Pages API 保存 rotation；geometry page 入口接收 page 並驗證支援範圍。內容 state context 不負責解析 Page dictionary。

**驗收**：預設 0、父繼承、子覆寫 0、間接值、負數／360、bad type／非倍數都覆蓋；有效非零 rotation 使 geometry 頁面失敗；MediaBox 非零原點不改變原始座標。

**驗證**：Pages／Text State／整頁 integration 測試；既有 dump-pages／dump-contents／dump-content golden 回歸通過。**依賴**：5（Pages 屬性讀取可先完成）。**檔案**：`pages.[ch]`、`pages_test.c`、geometry 頁入口與測試；中等範圍。

### 任務 7：可觀察輸出、清理與 release acceptance

**工作**：新增 read-only snapshot／安全的 library debug dump，及 `Contents → M7 → M8` 的整頁測試 consumer。debug 格式固定數值精度／locale，font name 以長度與 hex 表示，不輸出任意 binary string。consumer 暫存事件且所有頁成功才產生 golden trace。

本階段不新增使用假 metrics 的產品 CLI。既有 `--dump-content` 保持摘要格式；geometry debug 透過 library snapshot/dump 與 integration 測試驗收。M9 接入真實 metrics 後，再由同一個 seam 接正式 debug CLI；M10 產生 TextItem。單元測試與整頁測試需要 metrics 時，皆明示它是測試 adapter。

**驗收**：raw／Flate／Contents array 得到一致 geometry trace；stream 之間保留狀態、頁與頁之間重置；後頁／metrics／event callback 失敗無部分 trace；q capacity overflow、numeric overflow、font-name lifetime、callback abort 與 repeated failure 不洩漏。補全 fixtures、執行下節的真實 PDF 畫面／geometry 對照並明列通過與限制後，才能將 roadmap M8 標示完成。

**驗證**：`make` 編譯器零 warning，`make test`、`make -B asan` 全通過，檢查 diff／所有權並以 code review 對照本計畫。測試執行只依賴 C 工具鏈、zlib、POSIX shell；PDF fixtures 與 goldens 是靜態檔。若 ASan 的 LeakSanitizer 明確受 sandbox ptrace 擋住，依權限流程在沙箱外重跑，不關閉 leak detection。**依賴**：6。**檔案**：Text State debug、integration 測試、Makefile、fixtures/golden 與 roadmap；分小步驟完成。

## PDF 渲染截圖與真實解析對照驗收

此項是使用者追加的必要可觀察驗收，不能只展示手寫預期值或測試通過訊息。現有基線見 [PDF 畫面與實際解析對照](../pdf-visual-comparison.md) 與其左右對照 HTML；三份 PDF 已有真實頁面 PNG、M7 CLI／visitor 輸出及外部參考，M8 geometry 欄目前明確標為待驗證。

- **來源與畫面**：為成功的受控 fixture 及現有 hello.pdf／compilerbook.pdf 保存來源 path、SHA-256、頁碼、MediaBox／Rotate、renderer 版本、DPI 及原始頁面 PNG。只用實際 PDF 渲染，不把預期字串重新排版成「原 PDF 截圖」。原始頁面與帶標记的 overlay 分別保存。
- **成功 geometry 對照**：任務 1／5 使用具有有效 `/Resources /Font` 且真實可渲染的 PDF；測試 metrics adapter 的值必須與該 PDF 的明示 widths／字型資料一致，記錄 adapter 身份。既有 [visual-m7-text.pdf](../../tests/fixtures/visual-m7-text.pdf) 使用 Courier、width=600，可作為第一份 fixture；不要把舊的缺字型資源語法 fixtures 當作畫面正確性測試。
- **實際資料**：M8 跑出原始 string bytes、font/state、origin、advance、rendering matrix、decoded offset 的 raw trace；報告並列 expected、actual、delta、tolerance、pass/fail，actual 必須來自本專案的執行結果。M9 之前只標示 raw bytes／ASCII 預覽，不宣稱 Unicode 解碼。
- **外部參考**：可使用 Poppler `pdftoppm` 的頁面 PNG 與 `pdftotext -bbox` 的 word boxes／文字作為獨立參考；標明這些不是本專案的輸出。先對照受控、明示字型資料的 fixtures，再觀察真實一般 PDF；unsupported case 顯示空 stdout、實際 stderr／exit code 與停止階段，不強行當成成功。
- **座標轉換與容差**：截圖通常左上角原點，M8 是 default user-space／左下角慣例。對 Rotate=0、CropBox=MediaBox、UserUnit=1 的受控頁面，使用 `px=(x-x0)*dpi/72`、`py=(y1-y)*dpi/72`，並保存實際 PNG 尺寸；其他 CropBox／UserUnit／Rotate 不套用這個簡式，需另列限制或使用 renderer 的實際 transform。圖上標出本專案的 glyph origin／advance vectors；Poppler word bbox 不是 baseline，也不是實際 glyph ink bbox，不能把兩者數值直接相減。
- **判定尺度**：矩陣／advance 的數值 tolerance 先固定（建議 absolute 1e-8、relative 1e-9），以手算或獨立參考核對；畫面標記允許 2 px 的人工觀察偏差，不能用 pixel tolerance 掩蓋數值測試失敗。PNG 的 byte-for-byte diff 受 anti-aliasing／替代字型影響，不作為唯一通過判準；字串 bytes、位置、間距與方向分别判定。
- **必要案例**：單／多行文字、Td 依 line matrix 移動、TJ 正／負調整、Tz/Tc/Tw、Ts、cm 的縮放／平移與非交換順序、q/Q 恢復，以及 Tr=3 不可見文字。Tr=3 的畫面沒有 glyph，但 raw event 可以存在，不能把這種差異判為錯誤；比較其狀態／位置與來源指令即可。
- **持續保存**：任務 7 的 release acceptance 更新 `docs/pdf-visual-comparison.md` 與 `output/pdf/` 報告，附左右對照、完整可重跑命令／原始 trace 與明確未支援項目。M9–M11 後續依序補 Unicode、TextItem、閱讀順序 actual 欄；不可使用外部 reference 填充本專案仍未產生的結果。

Poppler／Python／browser 只用於額外可選的視覺驗證與報告生成；核心 `make test`／`make asan` 仍只依賴 C、zlib 與 POSIX shell。新增 PDF 與核心 golden 是 repository 內的靜態檔，渲染工具缺席不阻擋單元測試；本次使用者要求的人工截圖驗收須另外完成並記錄，不因核心測試通過而省略。

## 必須能手算核對的案例

1. 初始 CTM identity，`1 0 0 1 72 720 Tm; 0 -18 TD; T*` 的三個行起點為 `(72,720)`、`(72,702)`、`(72,684)`。
2. 行起點 `(72,720)`，顯示測試字串推進 12 後目前 x 為 84；再 `10 0 Td` 得到 x=82，而不是 94。
3. `0 1 -1 0 100 200 Tm; 10 0 Td` 得到 `(100,210)`，證明 Td 的方向受文字矩陣影響。
4. 初始 identity，先 scale(2) cm 再 translate(10,0) cm：point `(1,0)` 得到 `(22,0)`；指令反序得到 `(12,0)`。
5. 測試 metrics 明示每個 code width=500，font_size=12、Tc=1、Tw=3、hscale=0.8：`(A B)` 的推進量為 `(7+10+7)*0.8 = 19.2`；接 TJ -120 再推進 1.152。rise 改變 origin，但不改變這些 dx。
6. `q; /F1 12 Tf; q; /F2 20 Tf; ...; Q; ...; Q` 驗證每層字型及 size 正確恢復，最後可回到 font 未設定狀態；隨後 text-show 應失敗。

## 風險與延後項目

- **矩陣慣例混用**：以上非交換、旋轉後 Td 案例必須先進測試；使用 composition contract，不靠記憶猜左右乘。
- **borrowed font name 懸空／q aliasing**：只有 `.h` 明示可借用的資料才保留指標；owned immutable names 的 retain/release 以 ASan 覆核。
- **尚無真實 glyph metrics 卻輸出「精確」位置**：非空 string 沒有 provider 必須 unsupported。測試 adapter 與正式 CLI 不共用 fallback。
- **scope 擴張**：hello.pdf 的一般 graphics／marked content 相容性另排工作；compilerbook.pdf 的 xref stream 保留後續 milestone 範圍。不為了這兩份檔案提前實作 M9／更多 PDF 特性。
- **已知缺口**：實際 glyph shape bbox、完整 clipping／graphics state、page rotation、Type0、Unicode、閱讀順序均延後。M8 的 transformed advance vector 不等於文字包圍盒。


## 實作交付紀錄

任務 1–7 的實作位於 `src/matrix.[ch]`、`src/text_state.[ch]`、M7 operator 分派與 Pages Rotate 繼承。公開入口為 `pdf_text_state_create/destroy/visit/snapshot`、`pdf_text_page_interpret`、`pdf_text_event_dump`；metrics／event callback 與借用生命週期以 `text_state.h` 為準。JSON dump 使用 libc POSIX thread-local numeric locale，固定九位小數，不改 process locale；正式 CLI 保持 M7 摘要。

驗收入口：`tests/matrix_test.c`、`tests/text_state_test.c`、`tests/geometry_test.c`、`tests/run-geometry-fixtures.sh` 及擴充的 Pages／M7 tests。靜態 fixtures 覆蓋 raw／Flate／array／多頁／後頁失敗與 Rotate；collector 跨整份文件限制事件，所有頁成功才 dump。視覺 actual、overlay 和重跑命令見 [M8 report](../../output/pdf/m8-comparison/README.md) 與 [視覺驗收](../pdf-visual-comparison.md#m8-完成驗收)。

M9 可在同一個 byte-wise metrics seam 接 page Resources／document adapter；仍不得使用測試 Courier-600 作正式 fallback。未加入 Unicode、TextItem、閱讀順序、glyph bbox、page rotation transform 或一般 graphics operators。

最終驗證：`make -B test` 與沙箱外 `make -B asan` 全通過，65 筆 CLI fixtures，另含 M8 geometry golden runner；編譯器零 warning。LeakSanitizer 在沙箱內因 ptrace 限制失敗，依授權流程於沙箱外重跑成功，未關閉 leak detection。文件／程式 subagent review 的必要修正已完成，視覺 report 的 16 個事件數值比較與截圖人工核對通過。
