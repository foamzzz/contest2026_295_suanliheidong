Module:
ESP32-S3-WROOM-1-N16R8

CPU:
ESP32-S3

Flash:
16MB

PSRAM:
8MB Octal

Package:
40Pin


| 模组 Pin | GPIO/信号 | 程序宏                       | 实际用途                | 方向/外设模式       | 程序中的行为                |
| -----: | ------- | ------------------------- | ------------------- | ------------- | --------------------- |
|      4 | GPIO4   | `LED_GPIO_PIN`            | 板载 LED              | GPIO 输出       | 初始化为输出并写低电平           |
|      9 | GPIO16  | `AUDIO_I2S_MIC_GPIO_SCK`  | 数字麦克风时钟 SCK/BCLK    | I2S 输出        | 向麦克风提供串行时钟            |
|     10 | GPIO17  | `AUDIO_I2S_MIC_GPIO_WS`   | 数字麦克风左右声道时钟 WS/LRCK | I2S 输出        | 向麦克风提供采样帧时钟           |
|     11 | GPIO18  | `AUDIO_I2S_MIC_GPIO_DIN`  | 数字麦克风数据输入           | I2S 输入        | ESP32-S3 接收麦克风音频数据    |
|     17 | GPIO9   | `RIGHT_FRONT_LEG_PIN`     | 右前腿舵机               | LEDC PWM 输出   | 50Hz 舵机 PWM           |
|     18 | GPIO10  | `RIGHT_REAR_LEG_PIN`      | 右后腿舵机               | LEDC PWM 输出   | 50Hz 舵机 PWM           |
|     20 | GPIO12  | `DISPLAY_SDA_PIN`         | OLED SDA            | I2C0 双向       | OLED 数据线，启用内部上拉       |
|     21 | GPIO13  | `DISPLAY_SCL_PIN`         | OLED SCL            | I2C0 时钟       | OLED 时钟线，400kHz       |
|     22 | GPIO14  | `TOUCH_BUTTON_GPIO`       | 语音触摸按钮/按键           | GPIO 输入       | 按下开始录音，松开停止录音         |
|     23 | GPIO21  | `LEFT_REAR_LEG_PIN`       | 左后腿舵机               | LEDC PWM 输出   | 50Hz 舵机 PWM           |
|     24 | GPIO47  | `LEFT_FRONT_LEG_PIN`      | 左前腿舵机               | LEDC PWM 输出   | 50Hz 舵机 PWM           |
|     25 | GPIO48  | `TAIL_SERVO_PIN`          | 尾巴舵机                | LEDC PWM 输出   | 执行摇尾动作                |
|     27 | GPIO0   | `BOOT_BUTTON_GPIO`        | 启动按钮                | GPIO 输入、启动配置脚 | 创建了按钮对象，但压缩包内没有注册按键回调 |
|     31 | GPIO38  | `AUDIO_I2S_SPK_GPIO_LRCK` | 扬声器/功放 LRCK/WS      | I2S 输出        | 输出音频左右声道时钟            |
|     32 | GPIO39  | `AUDIO_I2S_SPK_GPIO_BCLK` | 扬声器/功放 BCLK         | I2S 输出        | 输出音频位时钟               |
|     33 | GPIO40  | `AUDIO_I2S_SPK_GPIO_DOUT` | 扬声器/功放数据            | I2S 输出        | ESP32-S3 输出数字音频数据     |

