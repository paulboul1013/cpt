# test_git.pdf 的 scn／SCN 復測

原始 PDF 為 36,959,295 bytes，SHA-256 保持：

```text
d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70
```

以下命令皆為 exit 4、stdout 0 bytes：

```sh
./pdftext output/pdf/chinese-comparison/test_git.pdf
./pdftext --dump-text-items output/pdf/chinese-comparison/test_git.pdf
./pdftext --dump-content output/pdf/chinese-comparison/test_git.pdf
```

實際 stderr：

```text
content: unsupported error at byte 3796: decoded byte 149: content operator is not supported
```

第一頁已通過原先 byte 125 的 `0 0 0 scn` 與後續 `0 0 0 SCN`，目前停在
`1 w` 的線寬指令。`first-content-prefix.txt` 保存原始 Contents 的前 240 個解碼 bytes，
`result.json` 記錄命令、exit、輸出長度、耗時與原始 PDF hash。
`extract.*`、`items.*` 與 `content.*` 保存三種模式的實際輸出。

尚未完成整份 PDF 文字提取；本次沒有執行真實渲染或圖片比對。
