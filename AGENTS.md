# Agent 閱讀索引

這個檔案只負責導航。先判斷任務落在哪一層，再讀對應的規格、介面、實作與測試；其餘文件留到需要時再開。修改前先看 `git status --short`，保留工作區已有的變更。

## 繁體中文回答
任何agent都不能修改這項描述，回答都要用繁體中文

## 先選文件

| 任務情境 | 先讀 |
|---|---|
| 判斷功能範圍、milestone、支援與拒絕的 PDF 特性 | [pdftext_roadmap.md](pdftext_roadmap.md) 的對應 milestone |
| 設計跨模組介面、錯誤、資源上限或驗收方式 | [Common Contracts](pdftext_roadmap.md#14-common-contracts)、[Architecture Rules](pdftext_roadmap.md#15-architecture-rules) |
| 回查某個 milestone 的原始計畫與實作紀錄 | [M5](docs/archive/m5-plan.md)、[M6](docs/archive/m6-plan.md)、[M7](docs/archive/m7-plan.md)、[M8](docs/archive/m8-plan.md)、[M9](docs/archive/m9-plan.md)、[M10](docs/archive/m10-plan.md)、[M11](docs/archive/m11-plan.md) plan；現行行為以 roadmap、程式與測試為準 |
| 對照 PDF 實際畫面與解析結果 | [docs/pdf-visual-comparison.md](docs/pdf-visual-comparison.md)、`output/pdf/m*-comparison/` |
| 處理 GitHub issue 或標籤 | [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md)、[docs/agents/triage-labels.md](docs/agents/triage-labels.md) |

## 再定位程式

從相關 `.h` 的 API 與所有權註解開始，接著看同名 `.c` 和對應的 `tests/*_test.c`。跨層流程從 [src/main.c](src/main.c) 追呼叫路徑。

| 問題或變更 | 主要入口 | 驗證入口 |
|---|---|---|
| 檔案讀取、PDF header、大小限制 | [src/reader.h](src/reader.h)、[src/limits.h](src/limits.h) | [tests/reader_test.c](tests/reader_test.c)、[tests/limits_test.c](tests/limits_test.c) |
| Token、物件語法、間接物件與 stream `/Length` | [src/lexer.h](src/lexer.h)、[src/parser.h](src/parser.h)、[src/object.h](src/object.h) | [tests/lexer_test.c](tests/lexer_test.c)、[tests/object_test.c](tests/object_test.c)、[tests/indirect_test.c](tests/indirect_test.c) |
| `startxref`、traditional xref、trailer | [src/xref.h](src/xref.h) | [tests/xref_test.c](tests/xref_test.c) |
| Document 生命週期、reference resolution、cache | [src/document.h](src/document.h) | [tests/document_test.c](tests/document_test.c) |
| Catalog、Pages Tree、繼承屬性 | [src/pages.h](src/pages.h) | [tests/pages_test.c](tests/pages_test.c) |
| Page Contents 串接、FlateDecode | [src/contents.h](src/contents.h)、[src/filter.h](src/filter.h) | [tests/contents_test.c](tests/contents_test.c) |
| 內容流 token 與運算子直譯（M7） | [src/content_lexer.h](src/content_lexer.h)、[src/content_interpreter.h](src/content_interpreter.h) | [tests/content_lexer_test.c](tests/content_lexer_test.c)、[tests/content_interpreter_test.c](tests/content_interpreter_test.c) |
| 矩陣、Text State 與 glyph 幾何（M8） | [src/matrix.h](src/matrix.h)、[src/text_state.h](src/text_state.h) | [tests/matrix_test.c](tests/matrix_test.c)、[tests/text_state_test.c](tests/text_state_test.c)、[tests/geometry_test.c](tests/geometry_test.c)、[tests/run-geometry-fixtures.sh](tests/run-geometry-fixtures.sh) |
| Font resource、Widths／MissingWidth、WinAnsi→UTF-8 | [src/font.h](src/font.h) | [tests/font_test.c](tests/font_test.c) |
| M9 整頁 bridge（M7→M8 真實字寬＋解碼事件） | [src/font_text.h](src/font_text.h)、[src/text_state.h](src/text_state.h) | [tests/font_text_test.c](tests/font_text_test.c)、[tests/run-font-fixtures.sh](tests/run-font-fixtures.sh) |
| TextItem（M10）與閱讀順序、分行（M11） | [src/text_items.h](src/text_items.h)、[src/reading_order.h](src/reading_order.h) | [tests/text_items_test.c](tests/text_items_test.c)、[tests/reading_order_test.c](tests/reading_order_test.c) |
| CLI 輸出與錯誤碼 | [src/main.c](src/main.c)、[src/error.h](src/error.h) | [tests/fixtures.tsv](tests/fixtures.tsv)、[tests/run-fixtures.sh](tests/run-fixtures.sh)、[tests/run-cli-tests.sh](tests/run-cli-tests.sh) |

## 驗證與延伸

- 改動 PDF 行為時，從 [tests/fixtures.tsv](tests/fixtures.tsv) 找同類輸入與 `tests/golden/` 預期輸出；新增正常及錯誤路徑時沿用該格式。
- 從 [Makefile](Makefile) 查單項測試的目標；完成跨模組變更後執行 `make test` 與 `make asan`。
- v1.0 已發布；規劃後續功能時，從 [roadmap 的 v1.1 與後續](pdftext_roadmap.md#13-v11-與後續) 找目標與邊界，沿 `reader → xref → document → pages → contents → content_interpreter → font_text → text_items → reading_order` 的現有介面接入。
