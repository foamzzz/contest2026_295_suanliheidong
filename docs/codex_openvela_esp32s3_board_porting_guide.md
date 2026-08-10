# Codex 执行指南：openvela 板级适配 ESP32-S3-WROOM-1-N16R8

> 目标：指导 Codex 在 **openvela 工作区根目录**中，仅通过 `contest2026_295_suanliheidong` 自有仓完成一个新的 ESP32-S3-WROOM-1-N16R8 板级适配，并执行配置、编译以及条件允许时的烧录/启动验证。
>
> 本任务 **不考虑任何外设**：不适配 LED、按键、LCD、Camera、Audio、I2C/SPI 外设、传感器、Wi-Fi、Bluetooth 等。目标仅是完成最小板级 BSP，使 openvela 能使用现有 ESP32-S3 芯片层启动，并尽可能进入 NSH。
>
> 本文件已经把 `porting_guide.md`、`Interrupt_System_Adaptation_Guide.md`、`Vendor.md`、`hardware.md` 和 `ESP32-S3-EYE.md` 中与本任务直接相关的内容提炼出来。**Codex 不要重新通读这些文档，也不要扫描整个 openvela 仓库。**

---

## 0. 不可违反的约束

### 0.1 唯一允许提交修改的仓库

只允许提交以下仓库中的修改：

```text
contest2026_295_suanliheidong/
```

板级适配源文件必须放在：

```text
contest2026_295_suanliheidong/board/contest_board/
```

该目录由系统映射到：

```text
vendor/openvela/boards/contest2026_295_board/
```

如当前 XML 中还没有对应映射，需要在：

```text
contest2026_295_suanliheidong/contest2026_295_suanliheidong.xml
```

按仓内既有 `<linkfile>` 风格增加一条映射。

**禁止直接在 `vendor/openvela/boards/contest2026_295_board/` 中手工维护一份副本。源文件的唯一真源必须是 `contest2026_295_suanliheidong/board/contest_board/`。**

### 0.2 生产仓库零源码修改

下列目录以及其他非 contest 自有仓不得被 Codex patch/edit：

```text
nuttx/
packages/
vendor/
apps/
frameworks/
external/
prebuilts/
tests/
```

特别禁止修改：

```text
nuttx/arch/xtensa/src/esp32s3/
```

该目录中的 ESP32-S3 架构层/芯片层已经适配完成，本任务只复用，不重做。

允许构建系统在这些目录生成 `.config`、`Make.defs`、对象文件、二进制等**构建产物**，但不得形成已跟踪源文件 diff；这些生成物也不得提交到 contest 仓。

### 0.3 中断系统不属于本次任务

ESP32-S3 芯片层已经实现中断、定时器、串口底层等 SoC 能力。

因此：

- 不实现 `up_irqinitialize()`；
- 不实现 `up_enable_irq()` / `up_disable_irq()`；
- 不实现 IRQ 优先级、向量表；
- 不新增芯片级 UART/Timer/Heap 驱动；
- 不因为编译报错就随意复制其他芯片的中断代码。

若构建暴露芯片层缺失，只允许**读取证据并报告 BLOCKED**，不得越界修改生产仓。

### 0.4 不考虑外设

不要为了“看起来功能更多”启用以下内容：

- Wi-Fi / WLAN
- Bluetooth / BLE
- LCD / framebuffer / LVGL
- Camera / Video
- Audio
- I2C 外设
- SPI 外设
- LED
- Button
- Sensor
- SD/MMC
- USB Host 外设
- 其他板载设备

基础 Console/NSH 不视为“外设功能扩展”，它是 bringup 验证手段。

### 0.5 `.gitignore` 和日志

仓内存在：

```text
.gitignore.example
```

如确实需要启用，可：

```bash
cp contest2026_295_suanliheidong/.gitignore.example \
   contest2026_295_suanliheidong/.gitignore
```

但必须按实际需要检查和修改。

**绝对不要忽略 `logs/`。最终导出的 AI Coding 日志必须提交。**

不要伪造 AI Coding 日志；如果当前会话还没有生成/导出日志，只需在最终反馈中提醒“提交前必须确认 logs/ 已纳入版本控制”。

---

# 1. 本次硬件事实

目标模组：

```text
ESP32-S3-WROOM-1-N16R8
CPU:   ESP32-S3
Flash: 16MB
PSRAM: 8MB Octal
Package: 40Pin
```

`hardware.md` 给出的引脚如下：

| Module Pin | Signal | 说明 |
|---:|---|---|
| 1 | GND | Power |
| 2 | 3V3 | 3.3V |
| 3 | EN | Enable / Reset |
| 4 | IO4 | GPIO |
| 5 | IO5 | GPIO |
| 6 | IO6 | GPIO |
| 7 | IO7 | GPIO |
| 8 | IO15 | GPIO |
| 9 | IO16 | GPIO |
| 10 | IO17 | GPIO |
| 11 | IO18 | GPIO |
| 12 | IO8 | GPIO |
| 13 | IO19 | GPIO |
| 14 | IO20 | GPIO |
| 15 | IO3 | GPIO |
| 16 | IO46 | GPIO |
| 17 | IO9 | GPIO |
| 18 | IO10 | GPIO |
| 19 | IO11 | GPIO |
| 20 | IO12 | GPIO |
| 21 | IO13 | GPIO |
| 22 | IO14 | GPIO |
| 23 | IO21 | GPIO |
| 24 | IO47 | GPIO |
| 25 | IO48 | GPIO |
| 26 | IO45 | GPIO |
| 27 | IO0 | GPIO / Strapping，启动相关 |
| 28 | IO35 | GPIO |
| 29 | IO36 | GPIO |
| 30 | IO37 | GPIO |
| 31 | IO38 | GPIO |
| 32 | IO39 | GPIO |
| 33 | IO40 | GPIO |
| 34 | IO41 | GPIO |
| 35 | IO42 | GPIO |
| 36 | RXD0 | UART0 RX |
| 37 | TXD0 | UART0 TX |
| 38 | IO2 | GPIO |
| 39 | IO1 | GPIO |
| 40 | GND | Power |
| 41 | GND / EPAD | 中间散热/接地焊盘 |

注意：

1. `hardware.md` 说明 Package 为 40Pin，同时列出 `41 = GND/EPAD`。把 41 视为 exposed ground pad，不作为可配置 GPIO。
2. `IO0` 是 strapping/启动相关引脚。除非系统启动机制明确要求，否则不要把它分配给普通板级功能。
3. `hardware.md` 对 Pin 36/37 只写了 `RXD0/TXD0`，**没有给出 GPIO 数字**。Codex 不得凭经验硬编码 UART0 RX/TX 对应 GPIO；必须从当前 openvela ESP32-S3 本地代码/Kconfig/已适配板配置中找到证据后再配置。
4. 本任务无外设，因此除 Console/启动必须项之外，不要人为给这些 GPIO 分配 I2C、SPI、LED、按键等用途。

---

# 2. Token 节省规则

Codex 必须遵守以下阅读策略。

## 2.1 禁止无目的全仓扫描

不要执行：

```bash
find . -type f
rg . .
grep -R "" .
tree -a .
```

不要通读整个：

```text
nuttx/
vendor/
packages/
```

## 2.2 优先精确查询

优先使用：

```bash
find <dir> -maxdepth 2 -type f | sort
rg -n 'exact_symbol|second_symbol' <specific_dir>
sed -n 'start,endp' <specific_file>
head
tail
```

构建日志不要整段贴回对话。保存到 `/tmp`，只反馈关键报错附近内容。

## 2.3 最多参考两个现有 ESP32-S3 板

参考优先级：

1. `nuttx/arch/xtensa/src/esp32s3/esp32s3-devkit`
2. `nuttx/arch/xtensa/src/esp32s3/esp32s3-eye`

第一参考能回答的问题，不再打开第二参考。

只有确实缺少某种构建/初始化模式时，才从下面列表中再选**一个**最相似目标，不要全部读取：

```text
common
esp32s3-box
esp32s3-korvo-2
esp32s3-lcd-ev
esp32s3-lhcbit
esp32s3-meadow
lckfb-szpi-esp32s3
```

## 2.4 对参考板只读必要文件

先查看文件名：

```bash
find nuttx/arch/xtensa/src/esp32s3/esp32s3-devkit -maxdepth 2 -type f | sort
```

然后只打开与以下目的直接相关的文件：

- board pin 定义；
- board initialize / bringup；
- board Kconfig；
- board Makefile / Make.defs；
- console 配置；
- PSRAM/Flash 相关配置引用。

不要读取 LCD、Camera、Audio、Wi-Fi、Bluetooth 等驱动源码。

---

# 3. Codex 必须采用的反馈协议

每完成一个阶段，Codex 在对话中给出简短反馈：

```text
[STEP N RESULT]
status: PASS | FAIL | BLOCKED
read:
  - 实际读取的关键文件
changed:
  - 实际修改的文件；没有则写 none
evidence:
  - 1~4 条成功/失败证据
next:
  - 下一步
```

规则：

- 不粘贴整个文件；
- 不粘贴完整 build log；
- 编译失败时只给“第一处真正错误”及必要上下文；
- 不隐瞒未完成项；
- 没有硬件时不能宣称“烧录/启动成功”。

最终再给一份统一报告，格式见第 12 节。

---

# 4. STEP 0：确认工作区与变更基线

从 openvela 根目录开始：

```bash
pwd
ls -1
```

预期能看到：

```text
apps
docs
frameworks
tests
build
nuttx
vendor
packages
contest2026_295_suanliheidong
external
prebuilts
build.sh
```

只检查 contest 仓状态：

```bash
git -C contest2026_295_suanliheidong status --short
```

记录已有用户变更，**绝不能覆盖或回滚用户已有修改**。

如果根工作区使用 repo 多仓工具，可以只执行轻量只读状态查询；不要为本任务做全量 `repo sync`。

### 成功判据

- 当前目录确实是 openvela 根目录；
- `contest2026_295_suanliheidong` 存在；
- 已记录任务开始前的 contest 仓状态。

### 失败处理

若上述路径不存在，直接 `BLOCKED`，报告实际 `pwd` 和缺失路径，不要猜路径。

---

# 5. STEP 1：确认 linkfile 映射方式

只读取 XML 中与 `<linkfile>` 有关的局部内容，不要先通读整个 XML。

推荐：

```bash
rg -n '<linkfile|contest_board|contest2026_295_board' \
  contest2026_295_suanliheidong/contest2026_295_suanliheidong.xml
```

如需要上下文，再对命中附近使用 `sed -n`。

目标关系：

```text
source:
contest2026_295_suanliheidong/board/contest_board/

destination:
vendor/openvela/boards/contest2026_295_board/
```

判断：

- 若映射已存在且正确：不要重复添加。
- 若不存在：严格复制 XML 内已有 `<linkfile>` 的语法风格，只增加这一条对应关系。
- 若 XML 中完全找不到可参考的 linkfile 格式：不要自创格式，报告 `BLOCKED`。

**禁止为了方便直接编辑 destination 下的 vendor 文件。**

接着确认 source 目录：

```bash
find contest2026_295_suanliheidong/board/contest_board \
  -maxdepth 3 -type f 2>/dev/null | sort
```

如果目录不存在，可以在 contest 仓创建。

### 成功判据

- XML 映射已存在或已在 contest 仓正确新增；
- source/destination 关系唯一且明确；
- 没有修改 `vendor/`。

---

# 6. STEP 2：最小化研究现有 ESP32-S3 适配

## 6.1 先研究 esp32s3-devkit

只列目录：

```bash
find nuttx/arch/xtensa/src/esp32s3/esp32s3-devkit \
  -maxdepth 2 -type f | sort
```

需要回答以下问题即可：

1. 板目录采用哪些文件参与构建？
2. board init 函数名是什么？
3. 公共 ESP32-S3 board helper 从哪里调用？
4. board pin 宏是什么风格？
5. `Make.defs`/`Makefile` 如何加入板级 `.c` 文件？
6. 无外设情况下，最小 bringup 能删到什么程度？

只打开能回答这些问题的文件。

## 6.2 只在必要时研究 esp32s3-eye

官方 EYE 示例已证明 ESP32-S3 构建流程可用，并展示了：

- Xtensa + ESP32-S3；
- ESP32-S3 board config；
- SPIRAM；
- Octal SPIRAM 模式；
- USB serial / NSH 等配置。

但 EYE 示例里的模组配置是另一容量型号，而且包含 LCD/Wi-Fi/Video 等功能。

因此：

**可以借它确认构建和 ESP32-S3 配置风格，不能复制它的整份 defconfig。**

尤其禁止直接照抄：

```text
CONFIG_ARCH_CHIP_ESP32S3WROOM1N4=y
```

本板是 N16R8。

## 6.3 精确查找 N16R8/Flash/PSRAM 能力

在 ESP32-S3 芯片层 Kconfig/相关配置中做**关键词定向搜索**：

```bash
rg -n \
  'N16R8|WROOM|FLASH.*16|16MB|SPIRAM|PSRAM|OCT|Octal' \
  nuttx/arch/xtensa/src/esp32s3 \
  --glob 'Kconfig*' \
  --glob '*.h'
```

如果符号不在 Kconfig，再只对已适配板的 `defconfig` 做定向搜索，不要扫源码全文：

```bash
rg -n \
  'N16R8|WROOM|SPIRAM|PSRAM|FLASH' \
  vendor nuttx \
  --glob 'defconfig' \
  --glob 'Kconfig*'
```

如果该命令命中太多，立即缩小到 ESP32-S3 相关目录。

需要最终确认的事实：

- 使用哪个本地 `CONFIG_*` 表示 ESP32-S3；
- 使用哪个本地 `CONFIG_*` 表示 16MB Flash；
- 使用哪个本地 `CONFIG_*` 表示 8MB PSRAM；
- 使用哪个本地 `CONFIG_*` 表示 Octal PSRAM；
- 是否存在直接对应 WROOM-1-N16R8 的 chip/module 选项。

### 强制止损条件 A

如果当前芯片层**没有**足够配置表达 16MB Flash + 8MB Octal PSRAM：

```text
status = BLOCKED
```

必须报告缺少什么符号、搜索过哪些文件。

**不得修改 `nuttx/arch/xtensa/src/esp32s3` 来新增芯片型号。**

---

# 7. STEP 3：确认 Console 引脚，不允许猜 RXD0/TXD0

`hardware.md`：

```text
Pin 36 = RXD0
Pin 37 = TXD0
```

但没有给出它们的 GPIO number。

进行定向搜索：

```bash
rg -n \
  'UART0|U0RX|U0TX|RXPIN|TXPIN|CONSOLE.*RX|CONSOLE.*TX' \
  nuttx/arch/xtensa/src/esp32s3 \
  --glob 'Kconfig*' \
  --glob '*.h' \
  --glob '*.c'
```

优先寻找：

- ESP32-S3 默认 UART0 pin 配置；
- esp32s3-devkit 的 console 定义；
- 芯片层已有 Kconfig 默认值。

只要当前本地代码已有默认 pin，并且 board 不需要覆盖，就**不要新增重复定义**。

### 强制止损条件 B

若无法从当前仓库证明 `RXD0/TXD0` 的 GPIO 映射：

- 不要凭 ESP32 经验硬编码；
- 保持芯片层默认配置（若能正常构建）；
- 在最终报告中标记“UART0 物理 GPIO 映射未从 hardware.md 明示，当前复用芯片默认配置”；
- 如果构建必须显式填写 GPIO 且无证据，则 `BLOCKED`。

---

# 8. STEP 4：建立最小 Board 目录

板源目录：

```text
contest2026_295_suanliheidong/board/contest_board/
```

最终应能映射为：

```text
vendor/openvela/boards/contest2026_295_board/
```

不要机械创建所有模板文件。先依据当前 openvela ESP32-S3 自定义 board 构建机制决定实际需要哪些文件。

一般目标结构可参考：

```text
contest_board/
├── Kconfig
├── configs/
│   └── nsh/
│       └── defconfig
├── include/
│   └── board.h
├── scripts/
│   └── Make.defs
└── src/
    ├── Makefile
    ├── contest_board.h
    ├── contest_board_boot.c        # 仅当现有 ESP32-S3 board 模式需要
    ├── contest_board_bringup.c     # 仅当现有 ESP32-S3 board 模式需要
    └── contest_board_appinit.c     # 仅当现有 ESP32-S3 board 模式需要
```

如果参考板证明某些文件不需要，则不要为了“目录看起来完整”而增加空壳文件。

链接脚本同理：

- 如果 ESP32-S3 当前机制复用芯片/公共链接脚本，就复用；
- 只有参考板明确要求 board 自带 linker script 时才创建；
- 禁止从 ARM 示例复制链接脚本给 Xtensa。

### 成功判据

- 所有新源文件都在 contest 仓；
- destination 若已经由 linkfile 映射，可看到对应内容；
- 没有 vendor 手工副本；
- 文件集合和 ESP32-S3 当前构建机制一致。

---

# 9. STEP 5：实现 board.h / 引脚定义

目标不是“给每个 GPIO 安排功能”，而是：

1. 记录本模组物理 pin -> signal 的事实；
2. 定义 BSP 真正需要的板级 pin 宏；
3. Console pin 仅在有本地证据时覆盖；
4. 不定义不存在的外设。

建议在 `include/board.h` 中保留清晰注释表，至少覆盖：

```text
IO0, IO1, IO2, IO3, IO4, IO5, IO6, IO7, IO8,
IO9, IO10, IO11, IO12, IO13, IO14, IO15, IO16,
IO17, IO18, IO19, IO20, IO21,
IO35, IO36, IO37, IO38, IO39, IO40, IO41, IO42,
IO45, IO46, IO47, IO48
```

以及：

```text
EN
RXD0
TXD0
GND/3V3/EPAD
```

注意：

- GND、3V3、EPAD 不作为软件 GPIO；
- EN 是芯片使能/复位，不作为普通 GPIO；
- IO0 标记为 strapping；
- `RXD0/TXD0` 不随意填写 GPIO 数值；
- 不增加 LED/BUTTON/I2C/SPI/CAMERA/LCD 等别名。

如果现有 ESP32-S3 board API 要求 `BOARD_NGPIOOUT`、`BOARD_NGPIOIN` 等外设数量，而本板没有此类板载外设，按参考板的合法“0 个设备”写法配置，不要造假设备。

### 成功判据

- `board.h` 可被当前 build include；
- 编译器没有因 board pin 宏缺失报错；
- 不包含无关外设 pin；
- IO0/EN/电源脚没有误用。

---

# 10. STEP 6：最小 Kconfig / defconfig / 初始化

## 10.1 Kconfig

只定义本板真正需要的配置项。

优先复用已有 ESP32-S3/common 逻辑，不复制其他板的大量外设选项。

如果当前 vendor/custom-board 机制需要 board selector，则名称与实际 destination 一致，例如围绕：

```text
contest2026_295_board
```

但**具体 CONFIG 符号必须遵循当前仓已有 custom board 机制**，不要自造一个没人引用的 Kconfig symbol。

## 10.2 configs/nsh/defconfig

目标是“最小可启动 NSH”。

必须确认：

- Architecture = Xtensa；
- Chip = ESP32-S3；
- 使用已有 ESP32-S3 芯片层；
- Board 指向本板/自定义 board；
- Flash = 16MB；
- PSRAM = 8MB；
- PSRAM bus/mode = Octal；
- NSH 开启；
- Console 开启；
- 适当的 init/bringup 配置开启。

明确不需要：

```text
ESP32S3_WIFI
DRIVERS_VIDEO
VIDEO
LCD
LVGL
CAMERA
AUDIO
I2C 外设
SPI 外设
```

不要照抄 ESP32-S3-EYE 的 Wi-Fi/LCD/Video 配置。

SMP 是否开启，以当前 ESP32-S3 最小参考板的稳定默认值为准，不为本任务专门改芯片层。

## 10.3 Board 初始化代码

openvela board 层可能涉及：

```c
board_early_initialize()
board_late_initialize()
board_app_initialize()
board_app_finalinitialize()
```

只实现当前 ESP32-S3 board 构建链实际需要的入口。

原则：

- Early init：仅做必须在启动早期完成的 board 动作；
- Late/app init：只调用本板存在的初始化；
- 无外设时，bringup 应尽可能小；
- 复用 `common` 或 ESP32-S3 已有 helper；
- 不复制 EYE 的 LCD、Camera、Wi-Fi 初始化；
- 不用空壳“假成功”吞掉关键错误。

如果某个 board init API 约定返回 `int`：

- 成功返回 `OK`/0；
- 对真正失败的必须项返回错误；
- 不应无条件把错误改成成功。

### 成功判据

- board init 文件参与 `libboards`/当前板库构建；
- 没有 unresolved board symbols；
- 没有外设初始化代码。

---

# 11. STEP 7：Makefile / Make.defs 构建接入

只参考现有 ESP32-S3 board 的构建方式。

目标：

- `src/Makefile` 只加入实际存在的板级 `.c`；
- `scripts/Make.defs` 使用当前 Xtensa/ESP32-S3 的正确工具链和链接规则；
- 如果 ESP32-S3 board 复用公共脚本，就不要复制一个新的 linker script；
- 不从 ARM `porting_guide` 示例中复制 `OUTPUT_ARCH(arm)` 等内容。

先做静态检查：

```bash
find contest2026_295_suanliheidong/board/contest_board \
  -maxdepth 3 -type f | sort
```

确认没有：

- EYE/LCD/Camera/Wi-Fi 遗留文件；
- 绝对路径；
- 指向个人 home 目录的路径；
- `vendor` 目录副本。

---

# 12. STEP 8：验证 linkfile materialization

检查 destination：

```bash
find vendor/openvela/boards/contest2026_295_board \
  -maxdepth 3 -type f 2>/dev/null | sort
```

若 source 已有文件但 destination 尚未出现：

1. **不要手工 copy 文件到 vendor；**
2. 检查当前工作区已有的 manifest/linkfile 生效方式；
3. 只使用项目已有的、范围最小的刷新方式；
4. 不要做无必要全量 `repo sync`；
5. 如果无法安全 materialize，则 `BLOCKED` 并报告。

### 成功判据

destination 中看到的文件与 contest source 对应，且 contest source 是唯一真源。

---

# 13. STEP 9：配置并编译

ESP32-S3-EYE 已验证的基本构建模式为：

```bash
rm -f nuttx/.config nuttx/Make.defs
./build.sh <board-config-path> -j8
```

本板优先尝试：

```bash
rm -f nuttx/.config nuttx/Make.defs

./build.sh \
  vendor/openvela/boards/contest2026_295_board/configs/nsh/ \
  -j8
```

如果本仓当前同类 ESP32-S3 custom board 明确使用 `--cmake`，则遵循该本地模式：

```bash
./build.sh \
  vendor/openvela/boards/contest2026_295_board/configs/nsh/ \
  --cmake -j8
```

**不要同时反复尝试随机构建参数。先依据一个本地已工作的 ESP32-S3 配置决定模式。**

建议记录日志：

```bash
rm -f /tmp/contest_board_build.log

./build.sh \
  vendor/openvela/boards/contest2026_295_board/configs/nsh/ \
  -j8 2>&1 | tee /tmp/contest_board_build.log
```

如果使用 pipe，记得检查真实 build 返回值，可使用：

```bash
set -o pipefail
```

## 13.1 构建失败处理顺序

只修复**第一处真实错误**，不要看到大量后续错误就同时修改很多文件。

优先分类：

1. board 路径/映射错误；
2. Kconfig symbol 错误；
3. Makefile 没加入源文件；
4. include path 错误；
5. board init symbol 缺失；
6. Flash/PSRAM 配置冲突；
7. 链接错误。

如果错误需要修改 `nuttx/arch/xtensa/src/esp32s3` 才能解决：

```text
BLOCKED
```

不要越界。

## 13.2 构建成功判据

必须同时满足：

- build 命令 exit code = 0；
- 没有最终 linker error；
- 找到实际生成的固件 `.bin`；
- 报告真实产物路径与大小；
- 不只凭“某个中间库生成了”就宣称成功。

可用范围受控的查询，例如：

```bash
find nuttx build \
  -maxdepth 3 \
  -type f \
  \( -name '*.bin' -o -name 'nuttx' -o -name '*.elf' \) \
  -printf '%p %s bytes\n' 2>/dev/null | tail -30
```

---

# 14. STEP 10：编译后配置审计

构建成功后检查实际 `nuttx/.config`，不要只相信 defconfig。

定向查询：

```bash
rg -n \
  'CONFIG_ARCH=|CONFIG_ARCH_XTENSA|CONFIG_ARCH_CHIP|ESP32S3|FLASH|SPIRAM|PSRAM|NSH|CONSOLE|WIFI|VIDEO|LCD|LVGL|CAMERA|AUDIO' \
  nuttx/.config
```

必须从实际配置中确认：

1. Xtensa；
2. ESP32-S3；
3. 本板/custom board 选择生效；
4. 16MB Flash 配置生效；
5. 8MB PSRAM 配置生效；
6. Octal PSRAM 配置生效；
7. NSH/Console 生效；
8. Wi-Fi/LCD/Camera/Audio/Video 等没有被误启用。

如果本仓的 Flash/PSRAM 配置不以直观的 `16MB`/`8MB` 字面量出现，Codex 必须指出对应 symbol 及其来源，而不是简单说“应该是”。

### 成功判据

最终反馈中给出上述 8 项的实际 CONFIG 证据。

---

# 15. STEP 11：硬件烧录与启动验证（条件执行）

只有同时满足以下条件才执行：

- ESP32-S3 工具链可用；
- esptool/当前仓烧录工具可用；
- 目标板连接到当前机器；
- 找得到正确串口；
- Codex 的执行环境允许访问串口。

先只读检测：

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true
```

不要猜端口。

ESP32-S3-EYE 的已验证流程是在 `nuttx` 下使用：

```bash
make flash ESPTOOL_PORT=<real-port> ESPTOOL_BINDIR=./
```

本板仅在当前构建系统仍生成兼容 flash target 时使用这一模式。

例如：

```bash
cd nuttx
make flash ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BINDIR=./
```

`/dev/ttyACM0` 只是示例，必须换成实际检测到的设备。

## 15.1 烧录成功判据

- flash 命令 exit code = 0；
- esptool 报告写入/校验成功；
- 没有连接失败、超时、wrong chip 等错误。

## 15.2 启动成功判据

使用 115200 波特率作为**优先验证值**，但若本地 defconfig 明确配置其他波特率，以实际配置为准。

成功至少需要看到：

- ESP32-S3/openvela 启动日志；
- 没有立即 reset loop / panic；
- 能进入 `nsh>`。

在 NSH 中执行最小检查：

```text
help
uname -a
free
```

本任务不验证 Wi-Fi、LCD、Camera 等。

### 无硬件时的反馈

必须写：

```text
Software build: PASS
Flash/boot verification: NOT RUN
Reason: no accessible target serial device / no hardware access
```

绝不能把“编译成功”写成“板卡适配已经硬件验证成功”。

---

# 16. STEP 12：生产仓零源码修改审计

最终必须检查 contest 仓：

```bash
git -C contest2026_295_suanliheidong status --short
git -C contest2026_295_suanliheidong diff -- \
  contest2026_295_suanliheidong.xml \
  board/contest_board
```

注意：第二条命令在 `-C contest2026_295_suanliheidong` 下执行时，路径应写成仓内相对路径：

```bash
git -C contest2026_295_suanliheidong diff -- \
  contest2026_295_suanliheidong.xml \
  board/contest_board
```

如果 `nuttx`、`vendor`、`packages` 各自是 Git worktree，则做只读检查：

```bash
git -C nuttx status --short
git -C vendor status --short
git -C packages status --short
```

若其中某个目录不是独立 Git repo，不要强行处理；报告“not a standalone git worktree”。

判断标准：

- 本任务不得造成生产仓中已跟踪源文件修改；
- build 生成的 ignored/untracked 产物不算源码 patch，但不能提交；
- 不要通过 `git reset --hard` 清理生产仓，因为可能破坏用户已有工作；
- 若发现非本任务已有改动，只报告，绝不回滚。

---

# 17. STEP 13：`.gitignore` 与 logs 检查

如果 `.gitignore` 已存在：

```bash
rg -n 'logs|build|nuttx|\.config' \
  contest2026_295_suanliheidong/.gitignore
```

如果需要启用 `.gitignore.example`，先复制后人工审查。

检查 `logs/` 是否被忽略：

```bash
git -C contest2026_295_suanliheidong check-ignore -v logs/ || true
```

如果该命令显示 `logs/` 被忽略，必须修改 `.gitignore` 使其不被忽略，例如按仓内规则增加反忽略：

```gitignore
!logs/
!logs/**
```

最终 AI Coding 日志导出后，应能被：

```bash
git -C contest2026_295_suanliheidong status --short
```

看到。

---

# 18. 最终交付文件最低要求

预期至少存在这些“类别”，但以本地 ESP32-S3 board 机制为准，不要求机械凑齐文件名：

```text
contest2026_295_suanliheidong/
├── contest2026_295_suanliheidong.xml     # 若原来无映射才修改
└── board/
    └── contest_board/
        ├── configs/nsh/defconfig
        ├── include/board.h
        ├── Kconfig                       # 若机制需要
        ├── scripts/Make.defs             # 若机制需要
        └── src/
            ├── Makefile
            └── 最小 board init 源文件
```

不能出现：

```text
board/contest_board/*wifi*
board/contest_board/*lcd*
board/contest_board/*camera*
board/contest_board/*audio*
```

除非只是注释/文件内容中不可避免出现参考词，不能加入相关功能实现。

---

# 19. Codex 最终反馈模板

执行完成后，Codex 必须按下面格式回复：

```text
# ESP32-S3-WROOM-1-N16R8 Board Porting Report

## 1. Result
Overall:
- PASS
或
- PARTIAL: software build passed, hardware not tested
或
- BLOCKED

## 2. Scope Compliance
- Modified repository: contest2026_295_suanliheidong only: YES/NO
- Patched nuttx source: NO
- Patched vendor source directly: NO
- Patched packages source: NO
- External peripherals added: NO

## 3. Files Read
只列真正读过的关键文件，按：
- contest manifest
- reference board
- ESP32-S3 Kconfig/config
分类。

## 4. Files Changed
逐个列出 contest 仓修改文件，并一句话说明用途。

## 5. Mapping
Source:
contest2026_295_suanliheidong/board/contest_board/

Destination:
vendor/openvela/boards/contest2026_295_board/

linkfile status:
- existing / added / blocked

## 6. Hardware Configuration Evidence
- CPU: ESP32-S3 -> <CONFIG evidence>
- Flash: 16MB -> <CONFIG evidence>
- PSRAM: 8MB -> <CONFIG evidence>
- PSRAM mode: Octal -> <CONFIG evidence>
- Console: <actual config>
- RXD0/TXD0 GPIO mapping: <evidence or "not explicitly overridden; using chip default">

## 7. Pin Definition Summary
- Power/EN treatment:
- IO0 strapping treatment:
- GPIOs declared:
- No peripheral aliases added: YES/NO

## 8. Board Initialization
实际初始化入口：
- ...
调用的 ESP32-S3/common helper：
- ...
明确未初始化：
- Wi-Fi
- LCD
- Camera
- Audio
- sensors
- other peripherals

## 9. Build
Command:
<exact command>

Exit code:
<code>

Artifacts:
- <path> <size>
- ...

Config audit:
- Xtensa: PASS/FAIL
- ESP32-S3: PASS/FAIL
- 16MB Flash: PASS/FAIL
- 8MB PSRAM: PASS/FAIL
- Octal PSRAM: PASS/FAIL
- NSH: PASS/FAIL
- Console: PASS/FAIL
- Unwanted peripherals disabled: PASS/FAIL

## 10. Flash / Boot
- Flash: PASS / FAIL / NOT RUN
- Serial device:
- Boot: PASS / FAIL / NOT RUN
- nsh>: YES / NO / NOT RUN

If NOT RUN:
<reason>

## 11. Repository Cleanliness
contest repo status:
<short summary>

production repo tracked source changes caused by this task:
NONE / list blocker

## 12. logs/
- logs ignored: YES/NO
- AI Coding logs currently present: YES/NO
- Reminder: final exported AI Coding logs must be committed.

## 13. Remaining Risks / Blockers
只列真实存在的问题。
如果没有：
None
```

---

# 20. 一次性执行顺序

Codex 按以下顺序执行，不要跳步骤：

```text
0. 确认工作区与基线
1. 确认 contest XML linkfile 映射
2. 最小读取 esp32s3-devkit；必要时才读 esp32s3-eye
3. 查清本地 N16R8 / 16MB Flash / 8MB Octal PSRAM 配置能力
4. 查清 UART0 Console pin 来源；禁止猜 RXD0/TXD0 GPIO
5. 在 contest/board/contest_board 建最小 board skeleton
6. 写 board.h/pin 定义
7. 写最小 Kconfig/defconfig/board init/build files
8. 验证 linkfile destination
9. 编译
10. 审计最终 nuttx/.config
11. 有硬件才烧录并验证 nsh
12. 审计生产仓无源码 patch
13. 检查 .gitignore 与 logs/
14. 输出最终 Porting Report
```

---

# 21. 关键决策原则

遇到不确定项，按这个优先级做决定：

```text
hardware.md 明示事实
    >
当前 openvela 仓的 ESP32-S3 芯片层/Kconfig
    >
当前已工作的 esp32s3-devkit 最小板实现
    >
ESP32-S3-EYE 已验证构建示例
    >
其他 ESP32-S3 板
    >
通用 porting 模板
```

禁止：

```text
凭记忆猜 ESP32-S3 引脚
凭其他芯片模板改 Xtensa
为了过编译修改 nuttx 芯片层
复制整份 EYE defconfig
扫描全部 board 目录后再选择
把编译成功等同于硬件成功
```

本任务的最佳结果不是“代码最多”，而是：

> **以最少板级代码、最少配置和最少仓库读取，复用现有 ESP32-S3 芯片层，让 N16R8 板得到可解释、可编译、可验证、且完全隔离在 contest 自有仓中的 BSP。**
