# EDK2 核心的本地补丁

[English](README.md) | 简体中文

`third_party/edk2/` 是上游 [tianocore/edk2](https://github.com/tianocore/edk2)
的 submodule，由 `scripts/setup-host.sh` 固定在 edk2-stable202602。在里面直接改的东西，
checkout 一重置就没了，所以对它的每一处修改都以补丁文件的形式保存在这里。一共 19 个：
几个修复（没有它们固件照样能编译，但运行不正常）；菜单依赖的简体中文字符串，以及首页、弹窗和鼠标方面的改动；
还有两个 UART 跟踪补丁，只在需要时才打。

这些补丁最初是基于 tianocore/edk2 `46548b1` 生成的，可以干净地打到 edk2-stable202602 上，
CI 就是基于这个版本编译的。

## 打补丁

`scripts/setup-host.sh` 会打上必需的那一组，所以正常流程不需要手动操作。手动打的方法：

```sh
patches/apply.sh third_party/edk2                 # required patches only
patches/apply.sh --with-debug third_party/edk2    # + UART tracing
patches/apply.sh --check third_party/edk2         # report, change nothing
```

设置了 `EDK2_CORE_DEBUG_PATCHES=1` 时，`setup-host.sh` 会加上 `--with-debug`。
重复运行是安全的：已经打过的补丁会被识别出来并跳过。

## 必需的补丁

### `0001-PeilessSec-flush-FD-before-DecompressFirstFv.patch`

`ArmConfigureMmu()` 打开 D-cache 之前没有先刷新它。在 RK3576 上，FD 由 TF-A 作为 BL33
加载到 DRAM，而 BL31 在校验 FIT 哈希时已经把同一块区域读进了 cache。`DecompressFirstFv()`
读取 LZMA section 头的时候，这些过时的 cache 行遮住了 DRAM 里的真实内容，解压就失败了。

这个补丁在调用解压之前，对 `PcdFdBaseAddress` … `+PcdFdSize` 这段范围做一次
`WriteBackInvalidateDataCacheRange()`（DC CIVAC）。

凡是 FD 由更早的启动阶段放进 DRAM 的平台都适用，所以它比较适合提交给上游。

### `0002-DefaultExceptionHandler-print-ESR-FAR-unconditionally.patch`

异常处理函数的寄存器转储走的是 `DEBUG()`，会被 `PcdDebugPrintErrorLevel` 过滤。在 RELEASE
构建里，真正能确定故障位置的四个值（ESR、FAR、SP、LR）恰恰最难通过这个过滤，结果只剩一句无从追查的
"Synchronous Exception at 0x…"，别的什么都没有。

这个补丁在原有横幅旁边用 `SerialPortWrite()` 输出 ESR / FAR / SP / LR / EC。有了它，
在正式发布的映像上也能走崩溃分析流程（解码 DFSC，对 `DxeCore.debug` 跑 `addr2line`）。

### `0005-SdMmcPciHcDxe-TRB-timeouts-are-wall-clock-microseconds.patch`

`SdMmcWaitTrbEnv()` 和 `SdMmcWaitTrbResult()` 把 `Timeout` 当作循环次数，每次循环一个
`Stall (1)`。但每次循环还要做好几次 MMIO 读和错误恢复，所以在 RK3576 上，一个"微秒"实际远不止一微秒。
2.5 s 的数据超时要花好几分钟。冷启动时 eMMC 对 EXT_CSD 读取返回 CRC 错误，固件在这些循环里待得太久，看起来就像卡死了。

补丁改用 `GetPerformanceCounter()` 计时，让超时时间名副其实。错误本身仍然要在主机驱动
（`DwcSdhciDxe`）里修；这个补丁只是不让它再耗掉好几分钟。

### `0006-MdeModulePkg-UI-add-zh-Hans-strings.patch`

给自带的设置菜单加上简体中文（`zh-Hans`）：首页（UiApp）、Device Manager、Boot Manager、
Boot Maintenance Manager 和 Boot Discovery Policy 表单。只改字符串文件，不改代码。

这些文件里的每一条字符串都有 `zh-Hans` 条目，包括两种语言写法相同的那些（波特率、终端类型）。
这不是可有可无的：选中的语言里缺了某条字符串时，HiiDatabase 不会退回到英文，设置界面会把它画成 "!"。
`scripts/gen-zh-hans-font.py check` 会检查这一点，并找出这些字符串需要的字形，供
`Silicon/Rockchip/Drivers/ZhHansFontDxe` 使用。

首页的语言列表从这个文件（`#langdef`）里取每种语言的名称，所以即使只是为了让平台自己的表单能选成中文，UiApp 也需要这个补丁。

设置界面自身的字符串（按键帮助、弹窗）在 0009 里。

### `0007-zh-Hans-strings-for-the-Device-Manager-forms.patch`

这些板子编译进去的 TianoCore 驱动的表单，全部字符串的简体中文：RAM disk、Secure Boot、
EFI Memory Attribute Protocol（EmbeddedPkg 的 MemoryAttributeManagerDxe），以及每块网卡的网络表单
（VLAN、IPv4、IPv6、HTTP boot、TLS authentication）。只改字符串文件。

这些驱动里有三处文字是在 C 代码里拼出来的，会保持英文，或者保持板子启动时的语言：IPv4、IPv6 和 HTTP boot
驱动在启动时一次性追加到帮助行末尾的 "(MAC:...)" 后缀；VlanConfigDxe 整条
"VLAN Configuration (MAC:...)" 帮助行；以及 Secure Boot 的 "Enabled"/"Disabled" 状态，它是一个 C 字面量。

### `0008-UiApp-rebuild-the-front-page-after-a-language-change.patch`

首页列出其他菜单（Device Manager、Boot Manager、Boot Maintenance Manager，以及所有属于首页类别的驱动
formset）的方式是：在构建页面时，把它们的标题以当时的语言复制到自己的字符串里，所有语言共用同一份文字。
"Select Language" 只写了 PlatformLang，所以这些条目要等到下次启动才会换成新语言。现在切换语言后会重新构建菜单
（`UpdateFrontPageForm ()`），用新语言取标题；浏览器看到更新后会重新解析表单。之所以选这个办法，而不是给每个标题按语言分别保存，
是因为它只需要在回调里加一次调用，就能一次覆盖所有复制过来的条目。

横幅里的几行（CPU、内存、固件版本）来自 SMBIOS 和 PCD，各种语言下都一样，不会重建。

### `0009-DisplayEngine-follow-the-language-and-show-touch-key-help.patch`

DisplayEngineDxe 和 CustomizedDisplayLib（底部的按键帮助栏、保存 / 放弃 / 重置弹窗、F9 / F10 提示）的简体中文，
外加两处代码改动：

- 两者都只在驱动入口加载一次字符串。DisplayEngineDxe 的 depex 不包含变量服务，所以加载时可能还读不到 PlatformLang，
  而且 "Select Language" 之后也不会再加载。现在，当 PlatformLang 和加载时的语言不同，`FormDisplay ()` 和
  `DisplayPageFrame ()` 会重新加载字符串，F9 / F10 热键也会带着新的帮助文字重新注册。
- 有一块能"打出按键"的触摸屏时（一个设备既是文本输入又是绝对坐标指针，且不是控制台分配器；泰山派 3M 上的
  TouchKeysDxe），按键帮助也会写出对应的手势：`<Enter>/tap=Select
  Entry`、`Esc/2 fingers=Exit`、`/drag=Move Highlight`。没有这样的设备时，帮助栏和原来完全一样。
  这些在 80 列下都放得进帮助栏的各列。

以下字符串保持原样，因为它们的第一个字符就是弹窗等待的按键：`ARE_YOU_SURE_YES` "Y"、`ARE_YOU_SURE_NO` "N"
（`ConfirmDataChange ()` 比较的是 `gYesResponse[0]` / `gNoResponse[0]`）、`CONFIRM_OPTION_YES` "Y (y)"、
`CONFIRM_OPTION_NO` "N (n)"、`CONFIRM_OPTION_OK` "O (o)"、
`CONFIRM_OPTION_CANCEL` "C (c)"、`DISCARD_OR_JUMP_DISCARD` "D (d)"、
`DISCARD_OR_JUMP_JUMP` "G (g)" 和 `DISCARD_OR_CHECK_CHECK` "C (c)"
（`FormDisplay.c` 和 `Popup.c` 比较的是它们的 `[0]`）。解释这些按键的中文提示里也保留同样的拉丁字母。

### `0010-DisplayEngine-break-lines-between-CJK-characters-not-inside-words.patch`

要在 0009 之上打。负责给提示、选项和帮助文字换行的 `GetLineByWidth ()`，原来只能在空格处断行，没有空格就在最后一个放得下的字符处断。
中文没有空格，所以总是在最后一个放得下的字符后面断开，放不下的 "。" 或 "，" 会单独出现在下一行开头；
中文里夹着的拉丁单词（"Synchronous"）也会这样被切成两半。

现在它在这一行里最后一个允许断开的地方断行：像以前一样在空格前，或者在两个字符之间（其中至少一个是宽字符），
但下一行不能以闭合标点开头（。，、；：！？）」』 以及 ASCII `) , . : ; ! ?`），这一行也不能以开始标点结尾
（（「『 以及 `( [ { <`）。两个窄字符之间，也就是单词内部，不会断开。只有比整行还宽的连续字符（长 URL、GUID）
仍然在溢出处截断。英文的断行位置和以前完全一样。

在 80、100、128 和 160 列的帮助宽度下，对每一条 zh-Hans 字符串做了模拟：以闭合标点开头的行，之前 36 行，之后 0 行
（上面说的超长连续字符除外）；被切开的拉丁单词，之前 20 个，之后 0 个。

### `0011-TerminalDxe-send-non-ASCII-text-as-UTF-8.patch`

串口控制台是 TTYTERM 终端。除了 VT-UTF8 以外的所有终端类型，`TerminalConOutOutputString ()`
都会把既不是 ASCII、也不是框线或箭头字形的字符变成 `?`。菜单切成中文后，串口控制台显示的是一行行问号。
现在这些字符以 UTF-8 发送，东亚宽字符让驱动的光标前进两列，和终端以及图形控制台上的表现一致。英文输出不变。
把终端类型改成 VT-UTF8 行不通：它的功能键不一样，而且 `PlatformBm.c` 断言终端类型是 TTYTERM。

### `0012-UiApp-add-Shut-Down-to-the-front-page.patch`

要在 0006 和 0008 之上打。在首页 "Reset" 下面加一项 "Shut Down" / "关机"，调用
`ResetSystem (EfiResetShutdown)`。在泰山派 3M 上，它通过 Rk806I2cPowerOffDxe 操作 PMIC；
在其他板子上，还是原来的 PSCI SYSTEM_OFF。这个补丁还补上了 Reset 分支后面缺的 `break`，
以前它会一路落到 `default`。

### `0013-DisplayEngine-select-the-statement-a-mouse-clicks.patch`

要在 0009 和 0010 之上打；如果有 0012，要在 0012 之后打。它不碰这几个补丁的任何一行。
以前在菜单里点鼠标等于按 Enter，打开的是高亮的条目，而不是鼠标指着的那一项。现在显示引擎等待输入时，
左键点哪里就选哪里：点在一个条目上，高亮移到那里；点在已经高亮的条目上，等于按 Enter。
one-of 弹窗里的选项和 Yes/No 弹窗里的按钮也是这样。点在其他地方（空行、帮助文字、标题、按键栏）没有反应。
右键和滚轮仍然是按键（Esc、Up、Down）。

显示引擎并不知道指针在哪里。新增的 `PointerClick.c` 会查找 `DISPLAY_POINTER_CLICK_PROTOCOL`，
这个协议由 OnScreenKeyboardDxe 安装：显示引擎在等待时告诉它（`SetReading`），并以文字单元格的形式拿到每次点击的位置
（`WaitForClick`、`GetClick`）。那个驱动根据画面、控制台的列数和行数，以及 GraphicsConsole 居中排布的 8x19 单元格算出是哪一格，
所以屏幕旋转已经算在里面了。没有这个协议时一切照旧。这个结构体在
`Silicon/Rockchip/Include/Protocol/RockchipOnScreenKeyboard.h` 里也有定义，两边必须保持一致。

### `0014-DisplayEngine-a-middle-click-selects-at-once.patch`

要在 0013 之后打，而且不碰 0013 的任何一行，这样 `apply.sh` 仍然能识别出 0013 已经打上。
在条目上点中键会直接选中它，而左键只是把高亮移过去。这次点击和其他点击一样进来（0013），先移动高亮；
菜单下一次等待输入时，`UiDisplayMenu ()` 会问 `DISPLAY_POINTER_CLICK_ACTIVATE_PROTOCOL`
（OnScreenKeyboardDxe）刚才是不是中键；如果是，而且高亮确实移动了，就像按 Enter 一样选中这个条目。
在已经高亮的条目上，点击本来就等于 Enter。在 one-of 和 Yes/No 弹窗里，中键和左键效果一样。
没有这个协议时一切照旧。定义同样复制在 `RockchipOnScreenKeyboard.h` 里。

### `0015-UsbMouseDxe-report-the-middle-button-too.patch`

`EFI_SIMPLE_POINTER_STATE` 只有左键和右键，所以 UsbMouseDxe（它会抢在 UsbMouseAbsolutePointerDxe
之前绑定所有 USB 鼠标）把中键丢掉了。现在它还会在鼠标的 handle 上安装 `USB_MOUSE_BUTTONS_PROTOCOL`，
其 `GetButtons ()` 返回最近一次报告里的按键字节（bit 2 是中键）。其他都不变；安装失败也不算错误。
定义同样复制在 `RockchipOnScreenKeyboard.h` 里。

### `0016-UiApp-add-Reset-All-Settings-to-the-front-page.patch`

要在 0012 之上打。在 "Shut Down" 下面加一项 "Reset All Settings" / "重置所有设置"。
这一项从 UiApp 所在的 firmware volume 里启动平台的 FullReset 应用
（`Silicon/Rockchip/Applications/FullReset`，FILE_GUID
`4bdbb2bd-8c5c-4400-bcca-0fd73a11ed49`），只有那个文件存在时才显示。确认和实际工作都由这个应用完成；
补丁本身不含任何平台逻辑。

### `0017-FileExplorerLib-add-zh-Hans-strings.patch`

和其他补丁互不依赖。文件浏览器（"Boot From File"、"Add Boot Option"、"Add Driver Option"）原来没有
zh-Hans 字符串，所以菜单切成中文后，它的标题以及 "new file" / "new folder" 条目还是英文。
现在每条字符串都有 zh-Hans 条目，写法和 0006、0007 一样用 \wide ... \narrow。文件保留 CRLF 换行符。

### `0018-DisplayEngine-Yes-No-popups-start-on-No.patch`

要在 0013 之上打。HII popup 协议弹出的 Yes/No（或 Yes/No/Cancel）弹窗，以前打开时高亮在 Yes 上，
误按一下 Enter 就回答了 Yes。这些弹窗确认的都是无法撤销的操作：Reset All Settings（清空整个变量区）和删除
Secure Boot 密钥。现在它们打开时高亮在 No 上；Y、N、方向键和鼠标点击照常可用。这个协议没有办法让调用者选择默认项
（选择结果只是输出），所以只能在这里改。

### `0019-DisplayEngine-warning-popups-in-red.patch`

要在 0013 和 0018 之上打。以前所有 HII 弹窗不论样式都用同一种弹窗颜色。现在警告和错误弹窗是红底白字，
让 Reset All Settings 这类无法撤销的问题更醒目；普通的提示弹窗保持原来的颜色。高亮的按钮保持高亮颜色。

### `0020-UiApp-round-the-CPU-frequency-and-plain-language-help.patch`

和其他补丁互不依赖。首页横幅上的 CPU 频率以前把 10 MHz 以下直接截掉，2208 MHz 显示成 2.20 GHz；
现在四舍五入到 10 MHz（2.21 GHz）。"Select Language" 的英文说明原来是 "This is the option one adjusts
to change the language for the current system"，现在是 "Changes the language the system uses."，
与 0006 的中文说明一致。

### `0021-BootManagerUiLib-firmware-boot-options-in-the-menu-language.patch`

和其他补丁互不依赖。平台把 "Reset to MaskROM" 和 "Reset All Settings" 注册为启动项。Boot#### 变量里
只有一份不分语言的描述，所以中文的启动管理器里它们还是英文，而首页已经是"重置所有设置"。现在启动项的
文件路径以固件卷文件结尾、且描述完全相同时，启动管理器按菜单语言显示这两项；变量本身不变，启动维护管理器
里的页面仍显示存储的描述。帮助里的 "Device Path : " 也有了中文。

## 可选的补丁（`--with-debug`）

### `0003-DEBUG-PeilessSec-UART-checkpoints.patch`

在 SEC 的各个阶段之间往 UART 输出 `[A]` … `[K]`。不需要 DEBUG 构建（它放不进 FV）就能定位 DXE 之前的无声卡死。

### `0004-DEBUG-PrePiLib-dump-HOB-list-before-DxeCore.patch`

在交接给 DxeCore 之前，输出 DxeCore 的加载地址和完整的 HOB 列表（resource descriptor、FV、内存分配、handoff）。
要回答"区域 X 到底有没有被保留"，这是最直接的办法；参见
`Silicon/Rockchip/RK3576/Library/PlatformLib/Rk3576Mem.c` 里关于 RK3576 内存映射的说明。

这两个补丁本来就输出很多；发布用的映像，以及任何 UART 延迟可能影响结果的测量，都不要打开它们。

## 添加补丁

不要只改了 `third_party/edk2/` 就完事，还要生成补丁：

```sh
cd third_party/edk2
git diff -- path/File.c > ../../patches/000N-what-it-does.patch
```

如果是跟踪用的而不是修复，就命名为 `NNNN-DEBUG-...`。`apply.sh` 正是靠这个前缀判断它是否需要手动选择才打。
