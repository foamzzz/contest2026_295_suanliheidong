#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netutils/cJSON.h>
#include "mimo_client.h"
#include "mimo_stream.h"
#include "mimo_tts.h"
#include "robot_voice_config.h"

struct tts_sink_s { mimo_tts_pcm_cb_t cb; void *arg; };

static int tts_sink(const uint8_t *pcm, size_t len, void *arg)
{
  struct tts_sink_s *sink = arg;
  return sink->cb(pcm, len, sink->arg);
}

int mimo_tts_speak_stream(const char *api_key, const char *text,
                          mimo_tts_pcm_cb_t cb, void *arg)
{
  cJSON *root = cJSON_CreateObject(); cJSON *messages = cJSON_CreateArray();
  cJSON *message = cJSON_CreateObject(); cJSON *audio = cJSON_CreateObject();
  char *body = NULL; char *response = NULL; size_t response_len = 0;
  cJSON *choices; cJSON *first; cJSON *msg; cJSON *audio_obj; cJSON *data;
  struct tts_sink_s sink = { cb, arg }; int ret;
  if (!api_key || !text || !cb || !root || !messages || !message || !audio)
    { cJSON_Delete(root); cJSON_Delete(messages); cJSON_Delete(message); cJSON_Delete(audio); return -EINVAL; }
  cJSON_AddStringToObject(root, "model", ROBOT_VOICE_TTS_MODEL);
  cJSON_AddStringToObject(message, "role", "assistant"); cJSON_AddStringToObject(message, "content", text);
  cJSON_AddItemToArray(messages, message); message = NULL; cJSON_AddItemToObject(root, "messages", messages); messages = NULL;
  cJSON_AddStringToObject(audio, "format", "wav"); cJSON_AddItemToObject(root, "audio", audio); audio = NULL;
  body = cJSON_PrintUnformatted(root); cJSON_Delete(root); root = NULL;
  response = malloc(ROBOT_VOICE_HTTP_RESPONSE);
  if (!body || !response) { free(body); free(response); return -ENOMEM; }
  printf("[RV-TTS] request begin bytes=%zu\n", strlen(body));
  ret = robot_mimo_post(api_key, body, strlen(body), response, ROBOT_VOICE_HTTP_RESPONSE, &response_len);
  free(body); if (ret < 0) { free(response); return ret; }
  root = cJSON_Parse(response); free(response); if (!root) return -EPROTO;
  choices = cJSON_GetObjectItem(root, "choices"); first = cJSON_GetArrayItem(choices, 0);
  msg = first ? cJSON_GetObjectItem(first, "message") : NULL;
  audio_obj = msg ? cJSON_GetObjectItem(msg, "audio") : NULL;
  data = audio_obj ? cJSON_GetObjectItem(audio_obj, "data") : NULL;
  if (!cJSON_IsString(data) || !data->valuestring) { cJSON_Delete(root); return -EPROTO; }
  ret = mimo_stream_wav_base64(data->valuestring, tts_sink, &sink);
  printf("[RV-TTS] response bytes=%zu pcm result=%d\n", response_len, ret);
  cJSON_Delete(root); return ret;
}
