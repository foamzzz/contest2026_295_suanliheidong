#ifndef __APPS_ROBOT_VOICE_AUDIO_CAPTURE_H
#define __APPS_ROBOT_VOICE_AUDIO_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

int robot_audio_capture_record(uint8_t *buf, size_t cap, size_t *out_len,
                               unsigned int duration_ms);

#endif
