############################################################################
# Independent Contest MiMo robot voice application
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = robot_voice
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 32768
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_295_ROBOT_VOICE)

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/src

MAINSRC = robot_voice_main.c

CSRCS += robot_voice_capture.c robot_audio_playback.c
CSRCS += mimo_client.c mimo_asr.c mimo_llm.c mimo_tts.c mimo_stream.c

include $(APPDIR)/Application.mk
