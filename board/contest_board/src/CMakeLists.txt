# ESP32-S3-WROOM-1-N16R8 contest board sources

target_sources(board PRIVATE board_boot.c board_bringup.c)

if(CONFIG_ESP32S3_I2S)
  target_sources(board PRIVATE board_voice_i2s.c)
endif()

if(CONFIG_CONTEST_BOARD_I2S0_RX OR CONFIG_CONTEST_BOARD_I2S1_TX)
  target_sources(board PRIVATE contest_i2s.c)
endif()

if(CONFIG_LCD_SSD1306_I2C)
  target_sources(board PRIVATE board_oled.c)
endif()

if(CONFIG_ARCH_BUTTONS)
  target_sources(board PRIVATE board_buttons.c)
endif()

if(CONFIG_BOARDCTL)
  target_sources(board PRIVATE board_appinit.c)
endif()
