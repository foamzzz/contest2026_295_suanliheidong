#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/clock.h>
#include <nuttx/semaphore.h>

#include <arch/board/board.h>

#include "robot_audio_playback.h"

#define RV_RING_BYTES  (256 * 1024)
#define RV_SOURCE_BLOCK 1024
#define RV_WIRE_BLOCK   2048

struct playback_ctx_s
{
  pthread_t thread;
  pthread_mutex_t lock;
  sem_t wake;
  uint8_t *ring;
  size_t head;
  size_t tail;
  size_t used;
  bool opened;
  bool finished;
  int error;
};

struct playback_wait_s
{
  sem_t done;
  int result;
};

static struct playback_ctx_s g_play;

static void robot_playback_complete(struct i2s_dev_s *dev,
                                    struct ap_buffer_s *apb, void *arg,
                                    int result)
{
  struct playback_wait_s *wait = arg;
  (void)dev;
  (void)apb;
  wait->result = result;
  nxsem_post(&wait->done);
}

static size_t ring_read(uint8_t *out, size_t len)
{
  size_t first = RV_RING_BYTES - g_play.tail;
  if (first > len)
    {
      first = len;
    }
  memcpy(out, g_play.ring + g_play.tail, first);
  if (len > first)
    {
      memcpy(out + first, g_play.ring, len - first);
    }
  g_play.tail = (g_play.tail + len) % RV_RING_BYTES;
  g_play.used -= len;
  return len;
}

static void *robot_playback_worker(void *arg)
{
  struct i2s_dev_s *speaker = board_voice_speaker_i2s();
  uint8_t source[RV_SOURCE_BLOCK];
  uint8_t wire[RV_WIRE_BLOCK];
  (void)arg;

  if (speaker == NULL)
    {
      g_play.error = -ENODEV;
      return NULL;
    }
  if (I2S_TXCHANNELS(speaker, 2) < 0 ||
      I2S_TXSAMPLERATE(speaker, 24000) != 24000 ||
      I2S_TXDATAWIDTH(speaker, 16) != 16)
    {
      g_play.error = -ENOTSUP;
      return NULL;
    }
  if (I2S_IOCTL(speaker, AUDIOIOC_START, 0) < 0)
    {
      g_play.error = -EIO;
      return NULL;
    }

  for (;;)
    {
      struct audio_buf_desc_s desc;
      struct ap_buffer_s *apb = NULL;
      struct playback_wait_s wait;
      size_t take;
      int ret;

      nxsem_wait(&g_play.wake);
      for (;;)
        {
          pthread_mutex_lock(&g_play.lock);
          take = g_play.used;
          if (take > RV_SOURCE_BLOCK)
            {
              take = RV_SOURCE_BLOCK;
            }
          if (take > 0)
            {
              ring_read(source, take);
            }
          else if (g_play.finished)
            {
              pthread_mutex_unlock(&g_play.lock);
              I2S_IOCTL(speaker, AUDIOIOC_STOP, 0);
              return NULL;
            }
          pthread_mutex_unlock(&g_play.lock);
          if (take == 0)
            {
              break;
            }

          memset(wire, 0, sizeof(wire));
          for (size_t i = 0; i + 1 < take; i += 2)
            {
              wire[i * 2] = source[i];
              wire[i * 2 + 1] = source[i + 1];
              wire[i * 2 + 2] = source[i];
              wire[i * 2 + 3] = source[i + 1];
            }
          memset(&desc, 0, sizeof(desc));
          desc.numbytes = take * 2;
          desc.u.pbuffer = &apb;
          ret = apb_alloc(&desc);
          if (ret < 0 || apb == NULL)
            {
              g_play.error = -ENOMEM;
              break;
            }
          memcpy(apb->samp, wire, take * 2);
          apb->nbytes = take * 2;
          apb->nsamples = take;
          nxsem_init(&wait.done, 0, 0);
          wait.result = -EINPROGRESS;
          ret = I2S_SEND(speaker, apb, robot_playback_complete, &wait,
                         MSEC2TICK(1500));
          if (ret < 0)
            {
              apb_free(apb);
              g_play.error = ret;
              break;
            }
          while (nxsem_wait(&wait.done) < 0)
            {
            }
          ret = wait.result;
          nxsem_destroy(&wait.done);
          apb_free(apb);
          if (ret < 0)
            {
              g_play.error = ret;
              break;
            }
        }
      if (g_play.error < 0)
        {
          I2S_IOCTL(speaker, AUDIOIOC_STOP, 0);
          return NULL;
        }
    }
}

int robot_audio_playback_open(void)
{
  memset(&g_play, 0, sizeof(g_play));
  g_play.ring = malloc(RV_RING_BYTES);
  if (g_play.ring == NULL)
    {
      return -ENOMEM;
    }
  pthread_mutex_init(&g_play.lock, NULL);
  nxsem_init(&g_play.wake, 0, 0);
  g_play.opened = true;
  if (pthread_create(&g_play.thread, NULL, robot_playback_worker, NULL) != 0)
    {
      robot_audio_playback_close();
      return -EAGAIN;
    }
  return 0;
}

int robot_audio_playback_write(const uint8_t *pcm, size_t len)
{
  if (!g_play.opened || (!pcm && len > 0) || len > RV_RING_BYTES)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_play.lock);
  if (len > RV_RING_BYTES - g_play.used)
    {
      pthread_mutex_unlock(&g_play.lock);
      return -EAGAIN;
    }
  size_t first = RV_RING_BYTES - g_play.head;
  if (first > len)
    {
      first = len;
    }
  memcpy(g_play.ring + g_play.head, pcm, first);
  if (len > first)
    {
      memcpy(g_play.ring, pcm + first, len - first);
    }
  g_play.head = (g_play.head + len) % RV_RING_BYTES;
  g_play.used += len;
  pthread_mutex_unlock(&g_play.lock);
  nxsem_post(&g_play.wake);
  return 0;
}

int robot_audio_playback_finish(void)
{
  if (!g_play.opened)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_play.lock);
  g_play.finished = true;
  pthread_mutex_unlock(&g_play.lock);
  nxsem_post(&g_play.wake);
  pthread_join(g_play.thread, NULL);
  g_play.thread = 0;
  return g_play.error;
}

void robot_audio_playback_close(void)
{
  if (g_play.opened)
    {
      g_play.finished = true;
      nxsem_post(&g_play.wake);
      if (g_play.thread != 0)
        {
          pthread_join(g_play.thread, NULL);
        }
      pthread_mutex_destroy(&g_play.lock);
      nxsem_destroy(&g_play.wake);
      free(g_play.ring);
    }
  memset(&g_play, 0, sizeof(g_play));
}
