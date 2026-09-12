if(CONFIG_LVX_USE_DEMO_CONTEST2026_295_ROBOT_VOICE)
  nuttx_add_application(
    NAME robot_voice
    SRCS robot_voice_main.c robot_voice_capture.c robot_audio_playback.c
         mimo_client.c mimo_asr.c mimo_llm.c mimo_tts.c mimo_stream.c
    STACKSIZE 32768)
endif()
