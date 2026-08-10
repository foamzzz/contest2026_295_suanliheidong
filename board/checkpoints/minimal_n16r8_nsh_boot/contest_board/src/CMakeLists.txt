# ESP32-S3-WROOM-1-N16R8 contest board sources

target_sources(board PRIVATE board_boot.c board_bringup.c)

if(CONFIG_BOARDCTL)
  target_sources(board PRIVATE board_appinit.c)
endif()
