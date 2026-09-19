# openvela ESP32-S3 AI 机器狗

---

# 一. 作品介绍

这是我基于 **openvela 和 ESP32-S3** 开发的一款 AI 机器狗。

作品希望把大模型从“会说话”进一步扩展到“能够控制真实机器人”。同一个 Agent 除了完成语音问答，还可以根据上下文和 Skill 调用机器狗的动作、表情和音乐能力。

当前作品实现：

- MiMo V2.5 ASR；
- MiMo V2.5 LLM；
- MiMo V2.5 TTS；
- openvela 官方 `ai_agent`；
- 文本与语音交互；
- 前进、后退、左转、右转；
- 安全摇尾巴；
- SSD1306 OLED 表情；
- `robot_react` 短时情绪反应；
- 本地 WAV 与 openvela Media 播放；
- Markdown Skill 自动打包与加载；
- 饮食推荐、机器人陪伴等自定义 Skill。

---

# 二. 使用、编译与调试

本项目运行于 **openvela + ESP32-S3**，集成 openvela 官方 `ai_agent`、MiMo V2.5 ASR/LLM/TTS、舵机运动、OLED 表情和音频播放。

## 2.1 编译

进入 openvela 工作区：

```bash
cd /vela/openvela
```

首次使用可准备 Python 环境：

```bash
python3 -m venv myenv
source myenv/bin/activate
```

配置工程：

```bash
./build.sh vendor/openvela/boards/contest2026_295_board/configs/nsh --cmake menuconfig
./build.sh vendor/openvela/boards/contest2026_295_board/configs/nsh --cmake savedefconfig
```

编译完整固件：

```bash
chmod +x contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh

contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh -j8
```

主要产物：

```text
nuttx/nuttx
nuttx/nuttx.bin
```

需要完整清理时：

```bash
contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh distclean -j8
```

## 2.2 烧录

```bash
PORT=/dev/ttyUSB0
```

需要完全重置 Flash 时：

```bash
python -m esptool \
  --chip esp32s3 \
  --port "$PORT" \
  erase_flash
```

烧录固件：

```bash
python -m esptool \
  --chip esp32s3 \
  --port "$PORT" \
  --baud 460800 \
  write-flash \
  --flash-size 16MB \
  --flash-mode dio \
  --flash-freq 40m \
  0x000000 \
  nuttx/nuttx.bin
```

## 2.3 启动与首次配置

烧录完成后松开 BOOT/GPIO0，按 EN/RESET，板卡进入下载模式，打开串口：

```bash
picocom -b 115200 /dev/ttyUSB0
```

启动 Agent：

```text
nsh> ai_agent
vela>
```

配置网络与 MiMo：

```text
vela> set_wifi <SSID> <密码>
vela> set_llm https://token-plan-cn.xiaomimimo.com/v1 mimo-v2.5 <API_KEY>
vela> set_voice_asr mimo-v2.5-asr
vela> set_voice_tts mimo-v2.5-tts
```

文本测试：

```text
vela> ask hello
```

语音测试：

```text
vela> voice_start
# 对麦克风说话
vela> voice_stop
```

## 2.4 调试命令

网络与 Agent：

```text
vela> net_status
vela> config_show
vela> router_status
vela> ask hello
```

运动、Tool 与 OLED：

```text
vela> robotctl status
vela> robotctl forward
vela> robotctl left 1 2000

vela> ask 向前走一步
vela> ask 摇一下尾巴
vela> ask 显示开心表情
```

Media 本地音频：
test.wav烧录保留在固件中，可以被直接调用播放
```text
vela> mediad &
vela> mediatool
mediatool> open Music
mediatool> prepare 0 url /etc/media/test.wav
mediatool> start 0
```

常用日志：

| 日志 | 模块 |
|---|---|
| `[BOOT-DIAG]`、`[BOARD-*]` | 系统与板级初始化 |
| `[RV-CAP]` | 麦克风采集 |
| `[RV-ASR]` | ASR |
| `[LLM-JSON]`、`[agent]` | LLM / Agent |
| `[ROBOT-MOTION]`、`[POWER-SAFE]` | 舵机运动 |
| `[RV-EXPR]`、`[RV-OLED]` | OLED 表情 |
| `[C-I2S]` | I2S |
| `[media]` | Media |



# 三. 系统实现框架

## 3.1 完整语音链路

机器狗的语音交互链路为：

```text
INMP441 麦克风
    ↓
I2S0 RX
    ↓
robot_voice_capture
    ↓
MiMo V2.5 ASR
    ↓
openvela ai_agent
    ↓
MiMo V2.5 LLM
    ↓
Skills / Tool Calls
    ↓
Agent 回复
    ↓
MiMo V2.5 TTS
    ↓
robot_audio_playback
    ↓
I2S1 TX
    ↓
MAX98357
    ↓
扬声器
```

麦克风采集：

```text
16 kHz / mono / PCM16
```

TTS 和扬声器逻辑播放：

```text
24 kHz / mono / PCM16
```

`vela ask` 文本输入直接进入同一套 Agent，因此文本和语音最终共用同一个 LLM、Skill、Tool 和回复链路。

## 3.2 Agent 框架

本项目直接使用 openvela 官方 `ai_agent` 作为统一 Agent 中枢。

```text
用户输入
  ↓
Session / Context / Skills
  ↓
MiMo V2.5
  ↓
ReAct / Tool Call
  ↓
Tool Registry
  ↓
机器人硬件能力
  ↓
Tool Result
  ↓
Agent 最终回复
```

这里将“大模型推理”和“硬件执行”分离：

- LLM 判断用户意图和需要调用的能力；
- Tool 对外描述机器人能做什么；
- Tool Executor 负责真正执行动作；
- 执行结果返回 Agent，再生成最终回复。

---

# 四. Agent Tool 设计

项目通过 openvela 官方 `tool_registry_register_provider()` 注册机器狗能力。

| Tool | 功能 | 实现 |
|---|---|---|
| `robot_move` | 前进、后退、左转、右转 | `robot_motion_tool.c` |
| `robot_tail_wag` | 安全摇尾巴 | `robot_motion_tool.c` |
| `robot_set_expression` | 设置持续表情 | `robot_expression_tool.c` |
| `robot_react` | 短时情绪反应 | `robot_expression_tool.c` |
| `robot_play_music` | 播放本地 WAV | `robot_music_player.c` |
| `music_search` | 在线搜索音乐 | openvela Tool |
| `music_play` | Media 音乐播放 | openvela Tool |

实体动作不会把原始 PWM 或任意舵机角度直接交给 LLM，而是提供前进、转向、摇尾等高层动作。运动通过统一 worker 和 `robot_action_guard` 管理。

## 4.1 Tool 可扩展框架

一个新的机器人能力可以按照统一模式接入：

```text
底层硬件能力
    ↓
控制函数
    ↓
Tool 描述
    ↓
JSON 参数 Schema
    ↓
Tool Executor
    ↓
Provider 注册
    ↓
openvela Agent
```

因此后续增加灯光、环境传感器、机械臂或新的运动能力时，只需要新增对应 Tool Provider，不需要重新实现 Agent 主循环。

---

# 五. 当前机器人能力

## 5.1 OLED 表情

使用 **SSD1306 128×64 OLED** 作为机器狗的“脸”。

当前表情包括：

```text
idle / listening / thinking / speaking
happy / error / excited / wink
love / surprised / cool / playful
sleepy / singing / curious / proud
```

表情路径：

```text
Agent / Voice
    ↓
robot_expression
    ↓
robot_oled_expr
    ↓
robot_oled
    ↓
SSD1306
```

`robot_set_expression` 用于持续表情，`robot_react` 用于短时情绪反馈。语音过程中也会跟随 listening、thinking、speaking 等状态切换。

## 5.2 运动与尾巴

机器狗使用 5 路舵机：

```text
右前腿
右后腿
左后腿
左前腿
尾巴
```

动作路径：

```text
Agent Tool
   ↓
robot_motion_tool
   ↓
robot_action_guard
   ↓
robot_motion worker
   ↓
robotctl_servo
   ↓
LEDC PWM
   ↓
舵机
```

## 5.3 音乐与音频

本地 WAV：

```text
robot_play_music
  ↓
robot_music_player
  ↓
robot_audio_playback
  ↓
I2S1
  ↓
MAX98357
```

openvela Media：

```text
Media Graph
  ↓
/dev/audio/pcm0p
  ↓
robot_audio_playback
  ↓
I2S1
  ↓
MAX98357
```

测试音频位于：

```text
/etc/media/test.wav
```

## 5.4 Skills

自定义 Skill 位于：

```text
contest2026_295_suanliheidong/skills/
```

构建时自动进入固件：

```text
skills/*.md
  ↓
generate_skill_bundle.py
  ↓
contest_skill_bundle.c
  ↓
robot_skill_installer
  ↓
openvela runtime Skill
```

Skill 负责行为策略和多轮对话，Tool 负责实际执行：

```text
Skill：告诉 Agent 应该怎么做
Tool ：告诉 Agent 可以做什么
```

---

# 六. 硬件与引脚

## 6.1 硬件配置

| 模块 | 配置 |
|---|---|
| 主控 | ESP32-S3 |
| 麦克风 | INMP441 |
| 麦克风接口 | I2S0 RX |
| 功放 | MAX98357 |
| 扬声器接口 | I2S1 TX |
| OLED | SSD1306 128×64 |
| OLED 接口 | I2C0，400 kHz |
| 舵机 | 4 路腿部 + 1 路尾巴 |
| 舵机接口 | LEDC PWM，50 Hz |
| Media Audio | `/dev/audio/pcm0p` |

## 6.2 实际引脚分配

| 模组 Pin | GPIO / 信号 | 程序宏 | 实际用途 | 模式 | 程序中的行为 |
|---:|---|---|---|---|---|
| 4 | GPIO4 | `LED_GPIO_PIN` | 板载 LED | GPIO 输出 | 初始化为输出并写低 |
| 9 | GPIO16 | `AUDIO_I2S_MIC_GPIO_SCK` | 麦克风 BCLK | I2S 输出 | 输出位时钟 |
| 10 | GPIO17 | `AUDIO_I2S_MIC_GPIO_WS` | 麦克风 WS/LRCK | I2S 输出 | 输出采样帧时钟 |
| 11 | GPIO18 | `AUDIO_I2S_MIC_GPIO_DIN` | 麦克风数据 | I2S 输入 | 接收 INMP441 PCM |
| 17 | GPIO9 | `RIGHT_FRONT_LEG_PIN` | 右前腿 | LEDC PWM | 50 Hz 舵机控制 |
| 18 | GPIO10 | `RIGHT_REAR_LEG_PIN` | 右后腿 | LEDC PWM | 50 Hz 舵机控制 |
| 20 | GPIO12 | `DISPLAY_SDA_PIN` | OLED SDA | I2C0 | OLED 数据 |
| 21 | GPIO13 | `DISPLAY_SCL_PIN` | OLED SCL | I2C0 | 400 kHz 时钟 |
| 22 | GPIO14 | `TOUCH_BUTTON_GPIO` | 保留 | 当前未使用 | 不作为主要交互输入 |
| 23 | GPIO21 | `LEFT_REAR_LEG_PIN` | 左后腿 | LEDC PWM | 50 Hz 舵机控制 |
| 24 | GPIO47 | `LEFT_FRONT_LEG_PIN` | 左前腿 | LEDC PWM | 50 Hz 舵机控制 |
| 25 | GPIO48 | `TAIL_SERVO_PIN` | 尾巴 | LEDC PWM | 尾巴复位与摇尾 |
| 27 | GPIO0 | `BOOT_BUTTON_GPIO` | BOOT | GPIO/启动脚 | 烧录与启动模式选择 |
| 31 | GPIO38 | `AUDIO_I2S_SPK_GPIO_LRCK` | 功放 LRCK/WS | I2S 输出 | 输出音频帧时钟 |
| 32 | GPIO39 | `AUDIO_I2S_SPK_GPIO_BCLK` | 功放 BCLK | I2S 输出 | 输出音频位时钟 |
| 33 | GPIO40 | `AUDIO_I2S_SPK_GPIO_DOUT` | 功放数据 | I2S 输出 | 输出数字音频 |

接线汇总：

```text
INMP441
GPIO16 -> SCK/BCLK
GPIO17 -> WS/LRCK
GPIO18 <- SD/DOUT

MAX98357
GPIO39 -> BCLK
GPIO38 -> LRC/WS
GPIO40 -> DIN

SSD1306
GPIO12 <-> SDA
GPIO13  -> SCL

Servo
GPIO9  -> 右前腿
GPIO10 -> 右后腿
GPIO21 -> 左后腿
GPIO47 -> 左前腿
GPIO48 -> 尾巴
```

---

# 七. 主要文件职责

## 7.1 语音与音频

| 文件 | 作用 |
|---|---|
| `app/robot_voice/robot_voice_main.c` | 语音入口、Agent 接入、TTS 与诊断命令 |
| `app/robot_voice/robot_voice_capture.c/.h` | I2S0 麦克风采集 |
| `app/robot_voice/mimo_client.c/.h` | MiMo 请求公共逻辑 |
| `app/robot_voice/mimo_asr.c/.h` | MiMo V2.5 ASR |
| `app/robot_voice/mimo_llm.c/.h` | LLM 独立诊断接口 |
| `app/robot_voice/mimo_tts.c/.h` | MiMo V2.5 TTS |
| `app/robot_voice/robot_audio_playback.c/.h` | I2S1 扬声器播放后端 |
| `app/robot_voice/robot_music_player.c/.h` | 本地 WAV Tool |
| `app/robot_voice/robot_voice_config.h` | 语音参数 |

## 7.2 运动与表情

| 文件 | 作用 |
|---|---|
| `app/robot_motion/robot_motion.c/.h` | 动作 worker 与步态 |
| `app/robot_motion/robot_motion_tool.c/.h` | 运动 Agent Tools |
| `app/robotctl/robotctl_main.c` | 手工运动调试 |
| `app/robotctl/robotctl_servo.c/.h` | 舵机 PWM 底层控制 |
| `app/robot_expression/robot_expression.c/.h` | 表情状态与优先级 |
| `app/robot_expression/robot_expression_tool.c` | 表情 Agent Tools |
| `app/robot_expression/robot_oled.c/.h` | OLED worker |
| `app/robot_expression/robot_oled_expr.c/.h` | 表情帧绘制 |

## 7.3 Agent、Skill 与板级

| 文件 | 作用 |
|---|---|
| `app/robot_core/robot_action_guard.c/.h` | 实体动作保护 |
| `app/robot_core/robot_network_adapter.c/.h` | 网络状态适配 |
| `app/robot_proactive/robot_skill_installer.c/.h` | Skill 安装 |
| `scripts/generate_skill_bundle.py` | Markdown Skill 打包 |
| `board/contest_board/configs/nsh/defconfig` | 默认系统配置 |
| `board/contest_board/include/board.h` | GPIO / I2S / PWM 定义 |
| `board/contest_board/src/board_bringup.c` | 板级外设初始化 |
| `board/contest_board/src/contest_i2s.c/.h` | I2S DMA 与数据适配 |
| `board/contest_board/src/board_voice_audio.c` | Media Audio bridge |
| `board/contest_board/src/board_oled.c` | SSD1306 初始化 |
| `board/contest_board/src/etc/media/graph.conf` | Media Graph |
| `board/contest_board/src/etc/media/test.wav` | 测试音频 |
| `board/contest_board/scripts/build_with_hal_backport.sh` | 固件构建入口 |

工程目录：

```text
contest2026_295_suanliheidong/
├── app/
│   ├── robot_voice/
│   ├── robot_motion/
│   ├── robot_expression/
│   ├── robotctl/
│   ├── robot_core/
│   └── robot_proactive/
├── board/
│   └── contest_board/
├── skills/
├── scripts/
├── docs/
├── logs/
└── README.md
```

---

# 八. 作品总结

本作品将 openvela 官方 Agent 与 ESP32-S3 机器人硬件组合成一套完整链路：

```text
听
INMP441 + MiMo V2.5 ASR
        ↓
想
openvela ai_agent + MiMo V2.5 + Skills
        ↓
做
Robot Tools → 动作 / 表情 / 音乐
        ↓
说
MiMo V2.5 TTS + MAX98357
```

Tool Provider 让机器狗的能力能够继续扩展，而 Agent 的对话、推理和上下文框架保持统一。

最终实现的是一套使用openvela系统，能够在 ESP32-S3 上完成 **语音理解、Agent 推理、实体动作、表情反馈和语音输出** 的嵌入式 AI 机器狗。
