#ifndef __APPS_ROBOT_VOICE_CONFIG_H
#define __APPS_ROBOT_VOICE_CONFIG_H

#define ROBOT_VOICE_HOST             "token-plan-cn.xiaomimimo.com"
#define ROBOT_VOICE_PORT             "443"
#define ROBOT_VOICE_PATH             "/v1/chat/completions"
#define ROBOT_VOICE_ASR_MODEL        "mimo-v2.5-asr"
#define ROBOT_VOICE_LLM_MODEL        "mimo-v2.5"
#define ROBOT_VOICE_TTS_MODEL        "mimo-v2.5-tts"
#define ROBOT_VOICE_TTS_RATE         24000
#define ROBOT_VOICE_CAPTURE_RATE     16000
#define ROBOT_VOICE_CAPTURE_SECONDS  3
#define ROBOT_VOICE_CHANNELS         1
#define ROBOT_VOICE_BITS             16
#define ROBOT_VOICE_HTTP_RESPONSE    (256 * 1024)
#define ROBOT_VOICE_MAX_BODY         (512 * 1024)

#endif
