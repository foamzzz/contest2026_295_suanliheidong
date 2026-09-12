#ifndef __APPS_ROBOT_VOICE_MIMO_STREAM_H
#define __APPS_ROBOT_VOICE_MIMO_STREAM_H

#include <stddef.h>
#include <stdint.h>

typedef int (*mimo_pcm_cb_t)(const uint8_t *pcm, size_t len, void *arg);
int mimo_stream_wav_base64(const char *b64, mimo_pcm_cb_t cb, void *arg);

#endif
