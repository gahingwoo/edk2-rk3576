# 发布

[English](releasing.md) | 简体中文

## 发布一个正式版

正式版用 `gh release create` 手动发布；没有工作流负责发布。

1. 在 `main` 上选定提交，用它构建每块板子（对 `rock4d`、`cm5io`、`tspi3m` 运行 `scripts/build.sh` 和 `scripts/package.sh`），或者直接用这个提交的 `ci` 构建的映像。
2. 文件命名为 `<Board>-<medium>-edk2-<ver>.img`，并加上 `SHA256SUMS.txt`、`.sfdisk` 分区表和 `OFL-1.1.txt`（内置中文字体的许可证）。
3. 写发布说明：改了什么、文件表，以及每块板子的 "How it was tested"（怎样测试的），写明哪块板子跑了哪个映像。
4. 给提交打标签并发布。

每个正式版必须正好包含一个 `ROCK4D-spi-edk2-*.img`、一个 `CM5IO-emmc-edk2-*.img` 和一个 `TaishanPi3M-emmc-edk2-*.img`。网页刷写工具（gahingwoo/edk2-webflash）每次部署时都按这些文件名模式选取最新正式版的映像，否则它的每日部署会失败。某个正式版没有重新构建某块板子时，就带上那块板子上一版的映像。

flash.gahingwoo.com 在下一次每日运行时拿到新正式版，或者手动运行它的部署工作流立即拿到。

## 滚动的 ci 构建

`main` 每次构建成功后，`build.yml` 都会替换标签为 `ci` 的预发布版（见[刷写固件](../flash.zh-CN.md#滚动的-ci-构建)）。Maskrom Studio 的 CI 档位就从它写入。
