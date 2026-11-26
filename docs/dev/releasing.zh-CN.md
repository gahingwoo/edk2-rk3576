# 发布

[English](releasing.md) | 简体中文

## 发布一个正式版

正式版用 `gh release create` 手动发布；没有工作流负责发布。

1. 在 `main` 上选定提交，取它的构建：下载这个提交的构建运行的工件，或者自己构建每块板子（对 `rock4d`、`cm5io`、`tspi3m` 运行 `scripts/build.sh`、`scripts/build-spl.sh`、`scripts/package.sh` 和 `scripts/features.py board`）。

   ```sh
   gh run download <run-id> -R gahingwoo/edk2-rk3576 -D artifacts
   scripts/features.py release --tag 0.5.0 --out rel artifacts   # 或者：... rel out
   ```

   `rel/` 里就有命名为 `<Board>-<medium>-edk2-<tag>.img` 的映像、`.sfdisk` 分区表、`FEATURES.json` 和 `SHA256SUMS.txt`；旁边的 `features-notes.md` 是功能表。再加上 `OFL-1.1.txt`（内置中文字体的许可证），加了文件就重新运行 `sha256sum`。
2. 写发布说明：改了什么、文件表、`features-notes.md` 里的功能表，以及每块板子的 "How it was tested"（怎样测试的），写明哪块板子跑了哪个映像。
3. 给提交打标签并发布：`gh release create <tag> rel/* ...`。

`FEATURES.json` 是 Maskrom Studio 用来知道每个映像能做什么的文件（[features-json.md](features-json.md)）。每块板子都要有同一个提交的 `features.json`，所以不能沿用旧正式版的映像。

每个正式版必须正好包含一个 `ROCK4D-spi-edk2-*.img`、一个 `CM5IO-emmc-edk2-*.img` 和一个 `TaishanPi3M-emmc-edk2-*.img`。网页刷写工具（gahingwoo/edk2-webflash）每次部署时都按这些文件名模式选取最新正式版的映像，否则它的每日部署会失败。

flash.gahingwoo.com 在下一次每日运行时拿到新正式版，或者手动运行它的部署工作流立即拿到。

## 滚动的 ci 构建

`main` 每次构建成功后，`build.yml` 都会替换标签为 `ci` 的预发布版（见[刷写固件](../flash.zh-CN.md#滚动的-ci-构建)）。Maskrom Studio 的 CI 档位就从它写入。它带有 `FEATURES.json`，发布说明末尾是功能表。不在 `main` 上的运行会在 "Release files (dry run)" 任务里生成同样的文件（工件 `release-dry-run`），但不发布。
