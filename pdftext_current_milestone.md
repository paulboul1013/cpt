# pdftext：當前 Milestone 規格（M2 Object Parser）

> 本文件是目前可以直接執行的工作規格。它只描述 M2，不提前實作 xref、Pages Tree、stream decode、font decode 或 layout。

## 1. 文件定位

pdftext_implementation_plan.md 是入口索引；長期功能與版本邊界見 pdftext_roadmap.md。本文件回答三個問題：

1. repo 目前實際完成了什麼？
2. M2 必須完成什麼才算結束？
3. M2 完成後，下一個 milestone 從哪裡開始？

本 milestone 的輸入是獨立的 PDF object fixture，不是完整 PDF 文件。M2 完成前不得開始 xref、indirect object resolver 或 CMap。

## 2. Repo 實際狀態（2026-08-24，M2 release acceptance）

M2 已完成並由 commit `20ca626` 收斂。CLI 現在保留兩種明確模式：

- `pdftext input`：逐一 dump stream 中的 standalone objects，供相容既有 fixture 使用。
- `pdftext --object input`：解析一個 standalone object，並要求後面只能是 EOF、whitespace 或 comment；trailing token 會回報 malformed error。

### 已完成的實作

- `src/reader.c`、`src/reader.h`：whole-file reader、peek/get/seek/tell/eof、input-size limit 與 buffer cleanup。
- `src/lexer.c`、`src/lexer.h`：PDF whitespace、comments、integer、real、bool、null、Name `#xx` escape、literal string、hex string、array/dictionary delimiters 與 generic keyword。
- `src/object.c`、`src/object.h`：primitive、raw bytes、array、dictionary、reference object；recursive free、dictionary lookup、duplicate-key source order 與 deterministic dump。
- `src/parser.c`、`src/parser.h`：primitive object grammar、nested array/dictionary、dictionary key/value validation、`INT INT R` reference lookahead、standalone EOF validation 與 partial-tree cleanup。
- `src/error.c`、`src/error.h`：I/O、malformed、unsupported、out-of-memory、resource-limit category；每個診斷包含 module、byte offset、訊息與 CLI exit-code mapping。
- `src/limits.c`、`src/limits.h`：集中管理 input、token、nesting、container、object-cache、stream 與 page limit；M2 已實際使用 input/token/nesting/container 限制。
- `Makefile`：strict C11、`pdftext`、`make test`、`make asan`，並建置 ownership、error、lexer、object、limits unit tests。
- `tests/run-fixtures.sh` 與 `tests/fixtures.tsv`：POSIX-only fixture/golden runner，逐一驗證 exit code、stdout、stderr 與 byte-offset diagnostics。

### M3 之前刻意保留的邊界

- 尚未驗證 `%PDF-` header，也尚未解析 object number、`obj/endobj` 或 stream raw bytes。
- 尚未實作 xref、trailer、resolver、Catalog、Pages Tree、content stream、font decode、layout 或 reading order。
- `object-cache`、stream-size、page-count limits 已定義為共用設定，但要等對應 M3+ module 使用；M2 不宣稱已實作那些 module。

M2 的輸入是 standalone PDF object fixtures，不是完整 PDF 文件；進入 M3 前不擴張這個邊界。

## 3. M2 目標

M2 結束時，parser 必須能把 standalone PDF object grammar 轉成可遞迴釋放的 object tree，並能對合法與非法輸入給出可定位的結果。

### 3.1 Lexer 必須支援

- PDF whitespace：NUL、tab、newline、form feed、carriage return、space。
- % comment：從 % 到 line ending 的內容全部跳過。
- Integer：123、-42、+88。
- Real：.5、1.、3.14、-0.5；不支援 exponent notation，例如 1e3。
- Name：/Type、/MediaBox，以及 Name #xx escape。
- Literal string：nested parentheses、\n、\r、\t、\b、\f、\(、\)、\\。
- Hex string：忽略 whitespace；奇數 hex digit 的最後 nibble 補 0。
- [、]、<<、>>。
- true、false、null。
- generic keyword，供 R 與後續 indirect object grammar 使用。

每個 token 都必須能被明確銷毀；未知或無法完成的 token 不得讓 reader 停在同一 byte 造成 infinite loop。

### 3.2 Object model 必須支援

    typedef enum {
        PDF_OBJECT_NULL,
        PDF_OBJECT_BOOL,
        PDF_OBJECT_INT,
        PDF_OBJECT_REAL,
        PDF_OBJECT_NAME,
        PDF_OBJECT_STRING,
        PDF_OBJECT_HEX_STRING,
        PDF_OBJECT_ARRAY,
        PDF_OBJECT_DICT,
        PDF_OBJECT_REF
    } pdf_object_type;

具體規則：

- integer payload 使用 int64_t。
- real payload 使用 double。
- raw string 使用 pdf_bytes，不能依賴 NUL 結尾的 C string。
- Name 是 decoded name payload，不保留 / 前綴。
- Reference 保存 object number 與 generation。
- Array 擁有所有 child object。
- Dictionary 擁有 key copy 與 value object。
- Dictionary duplicate key 保留 source order；pdf_dict_get() 回傳最後一個同名 entry。

### 3.3 Parser grammar

    object       := null | bool | int | real | name | string | hex_string
                  | array | dict | ref
    array        := '[' object* ']'
    dict         := '<<' (name object)* '>>'
    ref          := int int keyword('R')

Parser 必須支援 nested array、nested dictionary 與 dictionary value 是 reference 的情況。ref 只在完整的 INT INT R 形態出現時合併；否則依目前 token 序列回報合法的 primitive 或 malformed input。

`pdftext --object` 的合法 fixture 結尾只允許 EOF、whitespace 或 comment；其他 trailing token 必須報錯。相容的 stream mode 則允許連續 dump 多個 standalone objects。

## 4. Token 與 memory ownership 契約

採用 ownership move 模型：

- pdf_token 擁有自己的 dynamic payload。
- 提供 pdf_token_destroy(pdf_token *)。
- parser_peek() 回傳 borrowed const pdf_token *，不轉移 ownership。
- parser_next() 將 token ownership move 給呼叫者，並清空 lookahead slot。
- parser_destroy() 釋放尚未消耗的 lookahead。
- Object constructor 對外部 bytes/name 做 copy，除非 API 明確標示 take-ownership。
- Object tree 擁有所有 children；呼叫者不得釋放已插入 array/dict 的 child。
- 每個失敗路徑都必須能釋放已建立的部分 tree。

## 5. Error model

M2 開始不再使用只有 NULL 的無訊息失敗：

    typedef struct {
        int code;
        size_t offset;
        const char *module;
        char message[256];
    } pdf_error;

至少區分：

- I/O error
- malformed PDF/object grammar
- unsupported feature
- out of memory
- resource limit exceeded

錯誤訊息必須包含 module、byte offset（若可取得）與可讀原因。M2 fixture runner 以 exit code 與 exact stderr golden 同時檢查 category、module、offset 與原因，而不是只檢查 process 是否 crash。

## 6. Resource limits

所有容量計算、size_t 加法、reallocation 與 numeric conversion 都必須檢查 overflow。限制集中在 pdf_limits：

| 項目 | 預設上限 |
|---|---:|
| 整體 PDF | 256 MiB |
| 單一 token/string | 16 MiB |
| nested depth | 256 |
| 單一 array/dictionary entries | 1,000,000 |
| object cache | 1,000,000 objects |
| 單一 decoded stream | 256 MiB |
| page count | 100,000 |

M2 主要驗證 input、token/string、object nesting、array/dictionary entries、size/number overflow 與 allocation failure cleanup；stream/page/object-cache 限制由後續 module 使用同一份 pdf_limits。M2 的 limits unit test 會以可重現的小上限觸發 resource-limit error。

## 7. M2 實作切片

已完成的切片如下；每個切片都保持可編譯並納入後續 gate：

1. token/object error 與 ownership API 定型。
2. comment、Name `#xx`、real、bool、null。
3. literal string、hex string 與 `pdf_bytes`。
4. 可安全辨識 `INT INT R` 的 parser lookahead。
5. reference object、dictionary helper 與 duplicate-key 行為。
6. recursion、allocation、numeric overflow checks。
7. 完整合法與 malformed fixtures。
8. strict C11 Makefile、`pdftext`、`make test`、`make asan`。

M2 期間不得新增 xref、stream decode、Pages Tree、font、content interpreter 或 reading order 程式碼。

## 8. 測試與驗收

### 8.1 合法 fixture

tests/object.txt 至少包含：

    <<
        /Type /Page
        /Parent 2 0 R
        /MediaBox [0 0 612 792]
        /Count 5
        /Ratio 1.5
        /Enabled true
        /Title (Hello PDF)
        /Hex <48656C6C6F>
    >>

預期 dump 必須能辨識 DICT、NAME、REF、ARRAY、INT、REAL、BOOL、STRING 與 HEX STRING。另需測試 nested parentheses、escaped parentheses、Name escape、comments、duplicate dictionary key 與 nested dictionary。

### 8.2 malformed fixture

至少涵蓋：

- 未關閉 array。
- 未關閉 dictionary。
- dictionary key 不是 Name。
- 缺少 dictionary value。
- 奇怪或截斷的 string escape。
- 非法 hex digit。
- overflow integer/real。
- 不完整的 INT INT R。
- 超過 nesting 或 container limit。
- 合法 object 後出現 trailing token。

### 8.3 Build gate

    make
    make test
    make asan

編譯必須使用：

    -std=c11 -Wall -Wextra -Wpedantic -g

ASan/UBSan 測試不得出現 heap-buffer-overflow、use-after-free、double-free、明顯 leak 或 undefined behavior。make test 使用 POSIX shell runner，不依賴 Python、qpdf 或其他外部 runtime dependency。

2026-08-24 release acceptance 實際結果：GCC strict C11 的 `make test` 通過 22 個 fixture 與全部 unit tests；`make asan` 通過相同 fixture 與 ASan/UBSan unit tests；Clang strict C11 的 `make test` 亦通過。release review 確認 M2 變更沒有新增 xref、stream decode、Pages Tree、font、content interpreter 或 reading-order implementation。

## 9. M2 完成條件

- [x] 所有 M2 token/object grammar 通過合法 fixtures。
- [x] malformed fixtures 回傳正確錯誤類型與 byte offset。
- [x] token、parser lookahead、object tree ownership 有明確 free path。
- [x] Name #xx、comments、real、bool、null、literal/hex string、reference 全部可測試。
- [x] resource limits 與 overflow checks 已涵蓋。
- [x] make 使用嚴格 C11 且零 warning。
- [x] make test 通過。
- [x] make asan 通過。

## 10. M3 entry condition

下一個 milestone 是 roadmap 的 M3 Indirect Object Parser。開始 M3 前，必須維持本文件所有 M2 勾選項，並新增以下基線條件：

- 以 `%PDF-` header 驗證完整 PDF input。
- 定義 indirect object 的 object number、generation、`obj/endobj` grammar 與 error offsets。
- 以 `/Length` 精確讀取 stream raw bytes，先完成 `endstream` 再完成 `endobj` 驗證。
- 保留 M2 object parser 作為 stream dictionary 與 indirect object body 的下層 parser；不把 xref lookup、filter decode 或 object cache 偷塞進 M3。
