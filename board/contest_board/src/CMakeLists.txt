# ESP32-S3-WROOM-1-N16R8 contest board sources

target_sources(board PRIVATE board_boot.c board_bringup.c)

if(CONFIG_ARCH_BUTTONS)
  target_sources(board PRIVATE board_buttons.c)
endif()

if(CONFIG_BOARDCTL)
  target_sources(board PRIVATE board_appinit.c)
endif()
