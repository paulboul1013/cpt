# test_git.pdf 的 gs 復測

2026-10-21，未修改原始 PDF；36,959,295 bytes，SHA-256：

```text
d87fdaa38f27e0a1f84601a83729506f065be9f2ce0990faee9b43642dd2ef70
```

執行：

```sh
./pdftext output/pdf/chinese-comparison/test_git.pdf
./pdftext --dump-text-items output/pdf/chinese-comparison/test_git.pdf
```

兩種模式均 exit 4、stdout 0 bytes。stderr：

```text
content: unsupported error at byte 3796: decoded byte 13: content operator is not supported
```

第一頁原始開頭為 `/GSa gs /CSp cs /CSp CS`。原本 decoded byte 5 的 `gs` 已通過，
現在停在 byte 13 的 `cs`。尚未支援的 color、path、XObject 等指令仍使整份文件失敗；
本次未驗證全書中文解碼品質。

`result.json` 保存命令、exit code、輸出長度、耗時與 hash。`extract.*`／`items.*`
保存實際輸出，前一版 gs 失敗基線保留於相鄰的 `test_git-results/`。
