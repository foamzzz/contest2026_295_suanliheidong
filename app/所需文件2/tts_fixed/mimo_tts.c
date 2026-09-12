#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netutils/cJSON.h>

#include "mimo_client.h"
#include "mimo_stream.h"
#include "mimo_tts.h"
#include "robot_voice_config.h"

#define RV_TTS_WORKER_STACK (32 * 1024)

struct tts_sink_s
{
  mimo_tts_pcm_cb_t cb;
  void *arg;
};

struct tts_worker_s
{
  char *api_key;
  char *text;
  mimo_tts_pcm_cb_t cb;
  void *arg;
  int result;
};

static int tts_sink(const uint8_t *pcm, size_t len, void *arg)
{
  struct tts_sink_s *sink = arg;

  if (sink == NULL || sink->cb == NULL)
    {
      return -EINVAL;
    }

  return sink->cb(pcm, len, sink->arg);
}

/* The TTS response can be well over one megabyte because WAV PCM is base64
 * encoded inside choices[0].message.audio.data.  Parsing that response with
 * cJSON duplicates the very large base64 string and creates unnecessary heap
 * pressure.  Locate only the audio.data JSON string in-place instead.
 *
 * MiMo audio.data is base64, so the value itself cannot contain an unescaped
 * quote.  We still handle normal JSON whitespace around ':' for robustness.
 */

static char *find_audio_data(char *json, size_t len, char **value_end)
{
  static const char audio_key[] = "\"audio\"";
  static const char data_key[] = "\"data\"";
  char *begin = json;
  char *limit = json + len;
  char *audio;
  char *data;
  char *p;

  if (json == NULL || value_end == NULL)
    {
      return NULL;
    }

  *value_end = NULL;

  audio = strstr(begin, audio_key);
  if (audio == NULL || audio >= limit)
    {
      return NULL;
    }

  data = strstr(audio + sizeof(audio_key) - 1, data_key);
  if (data == NULL || data >= limit)
    {
      return NULL;
    }

  p = data + sizeof(data_key) - 1;
  while (p < limit && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
    {
      p++;
    }

  if (p >= limit || *p != ':')
    {
      return NULL;
    }

  p++;
  while (p < limit && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
    {
      p++;
    }

  if (p >= limit || *p != '"')
    {
      return NULL;
    }

  begin = ++p;
  while (p < limit)
    {
      if (*p == '"')
        {
          *value_end = p;
          return begin;
        }

      /* Base64 itself never needs JSON escaping.  Treat an escape as a
       * malformed audio payload rather than accidentally decoding it.
       */

      if (*p == '\\')
        {
          return NULL;
        }

      p++;
    }

  return NULL;
}

static int mimo_tts_run(const char *api_key, const char *text,
                        mimo_tts_pcm_cb_t cb, void *arg)
{
  cJSON *root = NULL;
  cJSON *messages = NULL;
  cJSON *message = NULL;
  cJSON *audio = NULL;
  char *body = NULL;
  char *response = NULL;
  char *audio_data = NULL;
  char *audio_end = NULL;
  size_t body_len;
  size_t response_len = 0;
  struct tts_sink_s sink;
  int ret = -ENOMEM;
  char saved;

  root = cJSON_CreateObject();
  messages = cJSON_CreateArray();
  message = cJSON_CreateObject();
  audio = cJSON_CreateObject();

  if (root == NULL || messages == NULL || message == NULL || audio == NULL)
    {
      goto out;
    }

  if (!cJSON_AddStringToObject(root, "model", ROBOT_VOICE_TTS_MODEL) ||
      !cJSON_AddStringToObject(message, "role", "assistant") ||
      !cJSON_AddStringToObject(message, "content", text) ||
      !cJSON_AddStringToObject(audio, "format", "wav"))
    {
      goto out;
    }

  cJSON_AddItemToArray(messages, message);
  message = NULL;
  cJSON_AddItemToObject(root, "messages", messages);
  messages = NULL;
  cJSON_AddItemToObject(root, "audio", audio);
  audio = NULL;

  body = cJSON_PrintUnformatted(root);
  if (body == NULL)
    {
      goto out;
    }

  body_len = strlen(body);

  /* +1 is intentional: robot_mimo_post() receives exactly the advertised
   * response capacity, while we retain one private byte for a NUL terminator.
   */

  response = malloc((size_t)ROBOT_VOICE_TTS_RESPONSE + 1);
  if (response == NULL)
    {
      goto out;
    }

  response[0] = '\0';
  response[ROBOT_VOICE_TTS_RESPONSE] = '\0';

  printf("[RV-TTS] request begin body=%zu response_cap=%u\n",
         body_len, (unsigned int)ROBOT_VOICE_TTS_RESPONSE);

  ret = robot_mimo_post(api_key, body, body_len, response,
                        ROBOT_VOICE_TTS_RESPONSE, &response_len);
  if (ret < 0)
    {
      printf("[RV-TTS] transport failed rc=%d\n", ret);
      goto out;
    }

  if (response_len >= ROBOT_VOICE_TTS_RESPONSE)
    {
      printf("[RV-TTS] response too large len=%zu cap=%u\n",
             response_len, (unsigned int)ROBOT_VOICE_TTS_RESPONSE);
      ret = -ENOSPC;
      goto out;
    }

  response[response_len] = '\0';
  printf("[RV-TTS] transport done response=%zu\n", response_len);

  audio_data = find_audio_data(response, response_len, &audio_end);
  if (audio_data == NULL || audio_end == NULL || audio_end <= audio_data)
    {
      printf("[RV-TTS] audio.data not found\n");
      ret = -EPROTO;
      goto out;
    }

  /* Temporarily terminate the base64 substring in-place.  This avoids both a
   * multi-megabyte cJSON string allocation and another base64 copy.
   */

  saved = *audio_end;
  *audio_end = '\0';
  sink.cb = cb;
  sink.arg = arg;
  ret = mimo_stream_wav_base64(audio_data, tts_sink, &sink);
  *audio_end = saved;

  printf("[RV-TTS] decode done b64=%zu result=%d\n",
         (size_t)(audio_end - audio_data), ret);

out:
  free(response);
  free(body);
  cJSON_Delete(audio);
  cJSON_Delete(message);
  cJSON_Delete(messages);
  cJSON_Delete(root);
  return ret;
}

static void *mimo_tts_worker(void *arg)
{
  struct tts_worker_s *work = arg;

  printf("[RV-TTS] worker start stack=%u\n",
         (unsigned int)RV_TTS_WORKER_STACK);
  work->result = mimo_tts_run(work->api_key, work->text,
                              work->cb, work->arg);
  printf("[RV-TTS] worker done rc=%d\n", work->result);
  return NULL;
}

int mimo_tts_speak_stream(const char *api_key, const char *text,
                          mimo_tts_pcm_cb_t cb, void *arg)
{
  struct tts_worker_s work;
  pthread_attr_t attr;
  pthread_t thread;
  int ret;

  if (api_key == NULL || api_key[0] == '\0' || text == NULL ||
      text[0] == '\0' || cb == NULL)
    {
      return -EINVAL;
    }

  memset(&work, 0, sizeof(work));
  work.api_key = strdup(api_key);
  work.text = strdup(text);
  work.cb = cb;
  work.arg = arg;
  work.result = -EIO;

  if (work.api_key == NULL || work.text == NULL)
    {
      free(work.api_key);
      free(work.text);
      return -ENOMEM;
    }

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      free(work.api_key);
      free(work.text);
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attr, RV_TTS_WORKER_STACK);
  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr, mimo_tts_worker, &work);
    }

  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      free(work.api_key);
      free(work.text);
      return -ret;
    }

  /* Keep voice_channel_speak() synchronous.  The ai_agent outbound dispatcher
   * owns its message text and may free it immediately after speak returns.
   * The worker uses private key/text copies and is joined before those copies
   * are released, avoiding both stack exhaustion and use-after-free.
   */

  ret = pthread_join(thread, NULL);
  if (ret != 0)
    {
      work.result = -ret;
    }

  free(work.api_key);
  free(work.text);
  return work.result;
}
