#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "agent_config.h"
#include "core/message_bus.h"
#include "infra/config_store.h"
#include "mimo_asr.h"
#include "mimo_llm.h"
#include "mimo_tts.h"
#include "robot_voice_capture.h"
#include "robot_audio_playback.h"
#include "robot_voice_config.h"

/* Strong definitions live in the application entry object so the linker
 * cannot discard them while resolving ai_agent's weak simulation stubs. */
static char g_voice_asr_backend[32] = "none";
static char g_voice_tts_backend[32] = "none";
static pthread_mutex_t g_voice_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_voice_speak_lock = PTHREAD_MUTEX_INITIALIZER;

#define CONTEST_VOICE_RECORD_STACK  (16 * 1024)
#define CONTEST_VOICE_ASR_STACK     (24 * 1024)
static volatile int g_voice_running;
static volatile int g_voice_speaking;
static volatile int g_voice_oneshot;
static volatile int g_voice_thread_valid;
static volatile bool g_voice_stop_requested;
static volatile bool g_voice_worker_exited;
static pthread_t g_voice_thread;
static int robot_voice_pcm_sink(const uint8_t *pcm, size_t len, void *arg);

enum contest_voice_state_e
{
  CONTEST_VOICE_IDLE = 0,
  CONTEST_VOICE_RECORDING,
  CONTEST_VOICE_ASR,
  CONTEST_VOICE_WAIT_AGENT,
  CONTEST_VOICE_SPEAKING,
  CONTEST_VOICE_STOPPING,
};

static volatile enum contest_voice_state_e g_voice_state = CONTEST_VOICE_IDLE;

static const char *contest_voice_state_name(enum contest_voice_state_e state)
{
  switch (state)
    {
      case CONTEST_VOICE_RECORDING:
        return "RECORDING";
      case CONTEST_VOICE_ASR:
        return "ASR";
      case CONTEST_VOICE_WAIT_AGENT:
        return "WAIT_AGENT";
      case CONTEST_VOICE_SPEAKING:
        return "SPEAKING";
      case CONTEST_VOICE_STOPPING:
        return "STOPPING";
      case CONTEST_VOICE_IDLE:
      default:
        return "IDLE";
    }
}

static void contest_voice_set_state(enum contest_voice_state_e state)
{
  enum contest_voice_state_e previous = g_voice_state;

  g_voice_state = state;
  if (previous != state)
    {
      printf("[contest_voice] state %s -> %s\n",
             contest_voice_state_name(previous), contest_voice_state_name(state));
    }
}

struct voice_file_sink_s
{
  int fd;
};

static int robot_voice_file_sink(const uint8_t *pcm, size_t len, void *arg)
{
  struct voice_file_sink_s *sink = arg;
  ssize_t written;

  if (sink == NULL || sink->fd < 0)
    {
      return -EINVAL;
    }

  written = write(sink->fd, pcm, len);
  if (written != (ssize_t)len)
    {
      return -EIO;
    }

  return robot_audio_playback_write(pcm, len);
}

static int voice_get_api_key(char *key, size_t cap)
{
  if (key == NULL || cap < 2 ||
      claw_config_get(AGENT_CFG_KEY_API_KEY, key, cap) != OK ||
      key[0] == '\0')
    {
      return -ENOENT;
    }
  return 0;
}

struct contest_asr_job_s
{
  const uint8_t *pcm;
  size_t pcm_len;
  int ret;
  char text[512];
};

static void *contest_voice_asr_worker(void *arg)
{
  struct contest_asr_job_s *job = arg;
  char key[256];

  if (job == NULL)
    {
      return NULL;
    }

  job->ret = voice_get_api_key(key, sizeof(key));
  if (job->ret < 0)
    {
      printf("[contest_voice] ASR skipped: configure set_llm first\n");
      return NULL;
    }

  job->text[0] = '\0';
  printf("[RV-ASR] worker start bytes=%zu stack=%u\n",
         job->pcm_len, (unsigned int)CONTEST_VOICE_ASR_STACK);
  job->ret = mimo_asr_recognize(key, job->pcm, job->pcm_len,
                                job->text, sizeof(job->text));
  memset(key, 0, sizeof(key));
  printf("[RV-ASR] worker done rc=%d\n", job->ret);
  return NULL;
}

static int contest_voice_run_asr(const uint8_t *pcm, size_t pcm_len,
                                 char *text, size_t text_cap)
{
  struct contest_asr_job_s *job;
  pthread_attr_t attr;
  pthread_t thread;
  int ret;

  if (pcm == NULL || pcm_len == 0 || text == NULL || text_cap == 0)
    {
      return -EINVAL;
    }

  job = calloc(1, sizeof(*job));
  if (job == NULL)
    {
      return -ENOMEM;
    }

  job->pcm = pcm;
  job->pcm_len = pcm_len;

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      free(job);
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attr, CONTEST_VOICE_ASR_STACK);
  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr, contest_voice_asr_worker, job);
    }
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      printf("[contest_voice] failed to start ASR worker: %d\n", ret);
      free(job);
      return -ret;
    }

  ret = pthread_join(thread, NULL);
  if (ret != 0)
    {
      printf("[contest_voice] failed to join ASR worker: %d\n", ret);
      free(job);
      return -ret;
    }

  ret = job->ret;
  if (ret == 0)
    {
      strncpy(text, job->text, text_cap - 1);
      text[text_cap - 1] = '\0';
    }
  else
    {
      text[0] = '\0';
    }

  free(job);
  return ret;
}

static void contest_voice_wait_for_reply_cycle(void)
{
  /* Once ASR text has entered ai_agent, keep the microphone idle until the
   * outbound voice reply has gone through TTS.  voice_channel_speak() changes
   * WAIT_AGENT -> SPEAKING -> RECORDING, which releases this wait. */

  while (!g_voice_stop_requested)
    {
      if (!g_voice_speaking && g_voice_state == CONTEST_VOICE_RECORDING)
        {
          return;
        }

      usleep(20 * 1000);
    }
}

static void *voice_record_worker(void *arg)
{
  size_t pcm_cap = (size_t)ROBOT_VOICE_CAPTURE_RATE * 2 *
                   ROBOT_VOICE_CAPTURE_SECONDS;
  uint8_t *pcm = malloc(pcm_cap);
  char text[512];
  (void)arg;

  if (pcm == NULL)
    {
      printf("[contest_voice] worker cleanup: pcm allocation failed\n");
      goto cleanup;
    }

  while (!g_voice_stop_requested)
    {
      size_t pcm_len = 0;
      int ret;

      contest_voice_set_state(CONTEST_VOICE_RECORDING);
      ret = robot_voice_capture_record_interruptible(
          pcm, pcm_cap, &pcm_len, ROBOT_VOICE_CAPTURE_SECONDS * 1000,
          &g_voice_stop_requested);
      printf("[RV-VOICE] capture returned rc=%d bytes=%zu\n", ret, pcm_len);
      if (ret < 0 || g_voice_stop_requested)
        {
          break;
        }
      if (g_voice_speaking)
        {
          continue;
        }
      contest_voice_set_state(CONTEST_VOICE_ASR);
      printf("[RV-VOICE] invoking ASR bytes=%zu on dedicated worker\n", pcm_len);
      ret = contest_voice_run_asr(pcm, pcm_len, text, sizeof(text));
      if (g_voice_stop_requested)
        {
          break;
        }
      if (ret < 0 || text[0] == '\0')
        {
          printf("[contest_voice] ASR failed: %d\n", ret);
          if (g_voice_oneshot)
            {
              break;
            }
          continue;
        }

      printf("[RV-ASR] text: %s\n", text);
      agent_msg_t msg;
      memset(&msg, 0, sizeof(msg));
      strncpy(msg.channel, AGENT_CHAN_VOICE, sizeof(msg.channel) - 1);
      strncpy(msg.chat_id, "voice", sizeof(msg.chat_id) - 1);
      msg.content = strdup(text);
      contest_voice_set_state(CONTEST_VOICE_WAIT_AGENT);
      printf("[RV-VOICE] push inbound voice:voice\n");
      if (msg.content == NULL || message_bus_push_inbound(&msg) != OK)
        {
          free(msg.content);
          printf("[contest_voice] message bus submit failed\n");
          if (g_voice_oneshot)
            {
              break;
            }
          continue;
        }

      if (g_voice_oneshot)
        {
          break;
        }

      contest_voice_wait_for_reply_cycle();
    }

cleanup:
  free(pcm);
  pthread_mutex_lock(&g_voice_lock);
  g_voice_running = 0;
  g_voice_worker_exited = true;
  contest_voice_set_state(CONTEST_VOICE_IDLE);
  pthread_mutex_unlock(&g_voice_lock);
  printf("[contest_voice] worker cleanup\n");
  return NULL;
}

int voice_channel_init(void)
{
  g_voice_running = 0;
  g_voice_speaking = 0;
  g_voice_oneshot = 0;
  g_voice_thread_valid = 0;
  g_voice_stop_requested = false;
  g_voice_worker_exited = true;
  contest_voice_set_state(CONTEST_VOICE_IDLE);
  printf("[contest_voice] voice_channel_init\n");
  return 0;
}

static int voice_channel_start_mode(int oneshot)
{
  pthread_mutex_lock(&g_voice_lock);
  if (g_voice_thread_valid)
    {
      pthread_mutex_unlock(&g_voice_lock);
      return -EBUSY;
    }
  g_voice_running = 1;
  g_voice_oneshot = oneshot;
  g_voice_stop_requested = false;
  g_voice_worker_exited = false;
  contest_voice_set_state(CONTEST_VOICE_RECORDING);
  pthread_attr_t attr;
  int create_ret;

  create_ret = pthread_attr_init(&attr);
  if (create_ret == 0)
    {
      create_ret = pthread_attr_setstacksize(&attr, CONTEST_VOICE_RECORD_STACK);
      if (create_ret == 0)
        {
          create_ret = pthread_create(&g_voice_thread, &attr,
                                      voice_record_worker, NULL);
        }
      pthread_attr_destroy(&attr);
    }

  if (create_ret != 0)
    {
      g_voice_running = 0;
      g_voice_oneshot = 0;
      g_voice_worker_exited = true;
      contest_voice_set_state(CONTEST_VOICE_IDLE);
      pthread_mutex_unlock(&g_voice_lock);
      printf("[contest_voice] failed to start record worker: %d\n", create_ret);
      return -EAGAIN;
    }
  g_voice_thread_valid = 1;
  pthread_mutex_unlock(&g_voice_lock);
  printf("[contest_voice] voice_channel_start mode=%s\n",
         oneshot ? "once" : "continuous");
  return 0;
}

int voice_channel_start(void)
{
  if (strcmp(g_voice_asr_backend, "mimo-v2.5-asr") != 0)
    {
      return -ENOSYS;
    }
  return voice_channel_start_mode(0);
}

int voice_channel_stop(void)
{
  enum contest_voice_state_e state;
  bool join_needed = false;
  pthread_t tid = 0;

  pthread_mutex_lock(&g_voice_lock);
  if (!g_voice_thread_valid)
    {
      pthread_mutex_unlock(&g_voice_lock);
      return 0;
    }
  g_voice_stop_requested = true;
  state = g_voice_state;
  contest_voice_set_state(CONTEST_VOICE_STOPPING);
  tid = g_voice_thread;
  join_needed = !pthread_equal(pthread_self(), tid);
  pthread_mutex_unlock(&g_voice_lock);

  printf("[contest_voice] stop requested state=%s\n",
         contest_voice_state_name(state));
  if (join_needed)
    {
      printf("[contest_voice] waiting capture callbacks and worker cleanup\n");
      pthread_join(tid, NULL);
    }

  pthread_mutex_lock(&g_voice_lock);
  if (g_voice_worker_exited)
    {
      g_voice_thread_valid = 0;
      g_voice_oneshot = 0;
    }
  pthread_mutex_unlock(&g_voice_lock);
  printf("[contest_voice] voice_channel_stop state=%s\n",
         contest_voice_state_name(g_voice_state));
  return 0;
}

int voice_channel_speak(const char *text)
{
  char key[256];
  int ret;
  if (!text || text[0] == '\0')
    {
      return -EINVAL;
    }
  if (strcmp(g_voice_tts_backend, "mimo-v2.5-tts") != 0)
    {
      return -ENOSYS;
    }

  pthread_mutex_lock(&g_voice_speak_lock);
  ret = voice_get_api_key(key, sizeof(key));
  if (ret < 0)
    {
      pthread_mutex_unlock(&g_voice_speak_lock);
      return ret;
    }
  ret = robot_audio_playback_open();
  if (ret < 0)
    {
      pthread_mutex_unlock(&g_voice_speak_lock);
      return ret;
    }
  g_voice_speaking = 1;
  contest_voice_set_state(CONTEST_VOICE_SPEAKING);
  printf("[contest_voice] voice_channel_speak text_bytes=%zu\n", strlen(text));
  ret = mimo_tts_speak_stream(key, text, robot_voice_pcm_sink, NULL);
  if (ret == 0)
    {
      ret = robot_audio_playback_finish();
    }
  robot_audio_playback_close();
  g_voice_speaking = 0;
  memset(key, 0, sizeof(key));
  if (g_voice_running && !g_voice_stop_requested)
    {
      contest_voice_set_state(CONTEST_VOICE_RECORDING);
    }
  pthread_mutex_unlock(&g_voice_speak_lock);
  return ret;
}

int voice_channel_test_tts(const char *text, const char *out_path)
{
  char key[256];
  struct voice_file_sink_s sink;
  int fd;
  int ret;
  if (!text || !out_path ||
      strcmp(g_voice_tts_backend, "mimo-v2.5-tts") != 0 ||
      voice_get_api_key(key, sizeof(key)) < 0)
    {
      return -EINVAL;
    }

  fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -errno;
    }
  sink.fd = fd;

  ret = robot_audio_playback_open();
  if (ret < 0)
    {
      close(fd);
      return ret;
    }
  g_voice_speaking = 1;
  ret = mimo_tts_speak_stream(key, text, robot_voice_file_sink, &sink);
  if (ret == 0)
    {
      ret = robot_audio_playback_finish();
    }
  robot_audio_playback_close();
  g_voice_speaking = 0;
  close(fd);
  printf("[contest_voice] voice_channel_test_tts text_len=%zu path=%s rc=%d\n",
         strlen(text), out_path, ret);
  return ret;
}

int voice_channel_test_asr(const char *pcm_path)
{
  char key[256];
  char text[512];
  uint8_t *pcm = NULL;
  size_t len;
  int fd;
  int ret;
  if (!pcm_path || strcmp(g_voice_asr_backend, "mimo-v2.5-asr") != 0 ||
      voice_get_api_key(key, sizeof(key)) < 0)
    {
      return -EINVAL;
    }

  fd = open(pcm_path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  len = (size_t)lseek(fd, 0, SEEK_END);
  if (len == 0 || len > ROBOT_VOICE_MAX_BODY)
    {
      close(fd);
      return -EFBIG;
    }
  lseek(fd, 0, SEEK_SET);
  pcm = malloc(len);
  if (pcm == NULL)
    {
      close(fd);
      return -ENOMEM;
    }
  if (read(fd, pcm, len) != (ssize_t)len)
    {
      free(pcm);
      close(fd);
      return -EIO;
    }
  close(fd);
  memset(key, 0, sizeof(key));
  ret = contest_voice_run_asr(pcm, len, text, sizeof(text));
  free(pcm);
  if (ret == 0)
    {
      printf("[contest_voice] ASR: %s\n", text);
    }
  return ret;
}

const char *voice_tts_get_backend(void)
{
  return g_voice_tts_backend;
}

int voice_tts_set_backend(const char *name)
{
  if (!name || strcmp(name, "mimo-v2.5-tts") != 0)
    {
      return -ENOENT;
    }

  strncpy(g_voice_tts_backend, name, sizeof(g_voice_tts_backend) - 1);
  g_voice_tts_backend[sizeof(g_voice_tts_backend) - 1] = '\0';
  return 0;
}

const char *voice_asr_get_backend(void)
{
  return g_voice_asr_backend;
}

int voice_asr_set_backend(const char *name)
{
  if (!name || strcmp(name, "mimo-v2.5-asr") != 0)
    {
      return -ENOENT;
    }

  strncpy(g_voice_asr_backend, name, sizeof(g_voice_asr_backend) - 1);
  g_voice_asr_backend[sizeof(g_voice_asr_backend) - 1] = '\0';
  return 0;
}

static char g_api_key[256];

static int robot_voice_pcm_sink(const uint8_t *pcm, size_t len, void *arg)
{
  (void)arg;
  return robot_audio_playback_write(pcm, len);
}

static int robot_voice_once(void)
{
  int ret;

  if (strcmp(g_voice_asr_backend, "mimo-v2.5-asr") != 0 ||
      strcmp(g_voice_tts_backend, "mimo-v2.5-tts") != 0)
    {
      printf("robot_voice: select mimo-v2.5-asr and mimo-v2.5-tts first\n");
      return -ENOSYS;
    }

  ret = voice_channel_start_mode(1);
  if (ret < 0)
    {
      return ret;
    }

  while (g_voice_running)
    {
      usleep(100000);
    }

  return voice_channel_stop();
}

static int robot_voice_capture_test(void)
{
  size_t cap = ROBOT_VOICE_CAPTURE_RATE * 2 * ROBOT_VOICE_CAPTURE_SECONDS;
  uint8_t *pcm = malloc(cap);
  size_t len = 0;
  int ret;
  if (!pcm) return -ENOMEM;
  ret = robot_voice_capture_record(pcm, cap, &len,
                                   ROBOT_VOICE_CAPTURE_SECONDS * 1000);
  printf("[RV-CAP] test rc=%d bytes=%zu\n", ret, len);
  free(pcm);
  return ret;
}

static int robot_voice_asr_test(void)
{
  size_t cap = ROBOT_VOICE_CAPTURE_RATE * 2 * ROBOT_VOICE_CAPTURE_SECONDS;
  uint8_t *pcm = malloc(cap);
  char text[512];
  size_t len = 0;
  int ret;
  if (!pcm) return -ENOMEM;
  ret = robot_voice_capture_record(pcm, cap, &len,
                                   ROBOT_VOICE_CAPTURE_SECONDS * 1000);
  if (ret == 0) ret = contest_voice_run_asr(pcm, len, text, sizeof(text));
  if (ret == 0) printf("ASR: %s\n", text);
  free(pcm);
  return ret;
}

static int robot_voice_tts_test(const char *text)
{
  int ret = robot_audio_playback_open();
  if (ret < 0) return ret;
  ret = mimo_tts_speak_stream(g_api_key, text, robot_voice_pcm_sink, NULL);
  if (ret == 0) ret = robot_audio_playback_finish();
  robot_audio_playback_close();
  return ret;
}

int main(int argc, char *argv[])
{
  if (g_api_key[0] == '\0')
    {
      voice_get_api_key(g_api_key, sizeof(g_api_key));
    }

  if (argc >= 3 && strcmp(argv[1], "set_key") == 0)
    {
      strncpy(g_api_key, argv[2], sizeof(g_api_key) - 1);
      g_api_key[sizeof(g_api_key) - 1] = '\0';
      if (claw_config_set(AGENT_CFG_KEY_API_KEY, g_api_key) != OK)
        {
          printf("robot_voice: failed to save API key\n");
          return 1;
        }
      printf("robot_voice: API key saved\n");
      return 0;
    }
  if (argc >= 2 && strcmp(argv[1], "once") == 0)
    {
      if (g_api_key[0] == '\0') { printf("robot_voice: use set_key first\n"); return 1; }
      return robot_voice_once();
    }
  if (argc >= 2 && strcmp(argv[1], "test_capture") == 0)
    {
      return robot_voice_capture_test();
    }
  if (argc >= 2 && strcmp(argv[1], "test_asr") == 0)
    {
      if (g_api_key[0] == '\0') { printf("robot_voice: use set_key first\n"); return 1; }
      return robot_voice_asr_test();
    }
  if (argc >= 3 && strcmp(argv[1], "test_llm") == 0)
    {
      if (g_api_key[0] == '\0') { printf("robot_voice: use set_key first\n"); return 1; }
      char reply[2048];
      int ret = mimo_llm_chat(g_api_key, argv[2], reply, sizeof(reply));
      if (ret == 0) printf("LLM: %s\n", reply);
      return ret;
    }
  if (argc >= 3 && strcmp(argv[1], "test_tts") == 0)
    {
      if (g_api_key[0] == '\0') { printf("robot_voice: use set_key first\n"); return 1; }
      return robot_voice_tts_test(argv[2]);
    }
  if (argc >= 2 && strcmp(argv[1], "diag") == 0)
    {
      printf("robot_voice: host=%s asr=%s llm=%s tts=%s capture=%dHz tts=%dHz\n",
             ROBOT_VOICE_HOST, ROBOT_VOICE_ASR_MODEL, ROBOT_VOICE_LLM_MODEL,
             ROBOT_VOICE_TTS_MODEL, ROBOT_VOICE_CAPTURE_RATE, ROBOT_VOICE_TTS_RATE);
      return 0;
    }
  printf("Usage: robot_voice set_key <api_key> | once | test_capture | "
         "test_asr | test_llm <text> | test_tts <text> | diag\n");
  return 0;
}
