# test_git.pdf 的 cs／CS 復測

原始 PDF 為 36,959,295 bytes，SHA-256 保持：

```text
d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70
```

執行以下兩種模式，皆為 exit 4、stdout 0 bytes：

```sh
./pdftext output/pdf/chinese-comparison/test_git.pdf
./pdftext --dump-text-items output/pdf/chinese-comparison/test_git.pdf
```

實際 stderr：

```text
content: unsupported error at byte 3796: decoded byte 125: content operator is not supported
```

原先 decoded byte 13 的 `cs` 與後續 `CS` 已通過。目前停在 `0 0 0 scn` 的 `scn`。
`scn` 及其他色彩、路徑、XObject 指令仍在本次範圍之外，尚未完成全書文字提取。

`result.json` 記錄命令、exit code、輸出長度、耗時與 hash。`extract.*`／`items.*`
保存實際輸出；相鄰的 `test_git-gs-results/` 保留先前停止在 cs 的基線。
本次未重新渲染 PDF 或檢視圖片。
