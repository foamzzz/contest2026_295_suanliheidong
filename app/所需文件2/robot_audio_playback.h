#ifndef __APPS_ROBOT_VOICE_AUDIO_PLAYBACK_H
#define __APPS_ROBOT_VOICE_AUDIO_PLAYBACK_H

#include <stddef.h>
#include <stdint.h>

int robot_audio_playback_open(void);
int robot_audio_playback_write(const uint8_t *pcm, size_t len);
int robot_audio_playback_finish(void);
void robot_audio_playback_close(void);

#endif
