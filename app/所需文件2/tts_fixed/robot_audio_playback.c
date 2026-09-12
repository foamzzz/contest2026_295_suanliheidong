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
#include "robot_voice_config.h"

#define RV_RING_BYTES        (256 * 1024)
#define RV_PREBUFFER_BYTES   (16 * 1024)
#define RV_SOURCE_BLOCK      1024
#define RV_WIRE_BLOCK        2048
#define RV_DMA_SLOTS         2
#define RV_SEND_TIMEOUT_MS   2000

struct playback_ctx_s
{
  pthread_t thread;
  pthread_mutex_t lock;
  sem_t wake;
  sem_t space;
  uint8_t *ring;
  size_t head;
  size_t tail;
  size_t used;
  bool opened;
  bool finished;
  bool started;
  int error;
};

struct playback_slot_s
{
  struct ap_buffer_s *apb;
  sem_t done;
  int result;
  bool active;
};

static struct playback_ctx_s g_play;

static void robot_playback_complete(struct i2s_dev_s *dev,
                                    struct ap_buffer_s *apb, void *arg,
                                    int result)
{
  struct playback_slot_s *slot = arg;
  (void)dev;
  (void)apb;

  if (slot != NULL)
    {
      slot->result = result;
      nxsem_post(&slot->done);
    }
}

static size_t ring_read_locked(uint8_t *out, size_t len)
{
  size_t first;

  if (len > g_play.used)
    {
      len = g_play.used;
    }

  first = RV_RING_BYTES - g_play.tail;
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

static size_t playback_take_source(uint8_t *source)
{
  size_t take = 0;

  pthread_mutex_lock(&g_play.lock);

  if (!g_play.started)
    {
      if (g_play.used >= RV_PREBUFFER_BYTES ||
          (g_play.finished && g_play.used > 0))
        {
          g_play.started = true;
          printf("[RV-PB] PREBUFFER -> PLAYING buffered=%zu\n", g_play.used);
        }
    }

  if (g_play.started)
    {
      if (g_play.used >= RV_SOURCE_BLOCK)
        {
          take = RV_SOURCE_BLOCK;
        }
      else if (g_play.finished)
        {
          take = g_play.used & ~(size_t)1;
        }

      if (take > 0)
        {
          ring_read_locked(source, take);
        }
    }

  pthread_mutex_unlock(&g_play.lock);

  if (take > 0)
    {
      /* Wake a producer that may be applying ring-buffer backpressure. */
      nxsem_post(&g_play.space);
    }

  return take;
}

static bool playback_is_drained(void)
{
  bool drained;

  pthread_mutex_lock(&g_play.lock);
  drained = g_play.finished && g_play.used == 0;
  pthread_mutex_unlock(&g_play.lock);
  return drained;
}

static void source_to_stereo_wire(const uint8_t *source, size_t source_len,
                                  uint8_t *wire)
{
  size_t i;
  size_t o = 0;

  for (i = 0; i + 1 < source_len; i += 2)
    {
      uint8_t lo = source[i];
      uint8_t hi = source[i + 1];

      /* MAX98357 is driven from a two-slot I2S frame on this board.  Duplicate
       * the mono signed-16 sample into L/R slots without changing cadence.
       */
      wire[o++] = lo;
      wire[o++] = hi;
      wire[o++] = lo;
      wire[o++] = hi;
    }
}

static int playback_queue_slot(struct i2s_dev_s *speaker,
                               struct playback_slot_s *slot,
                               const uint8_t *source, size_t source_len)
{
  struct audio_buf_desc_s desc;
  uint8_t wire[RV_WIRE_BLOCK];
  size_t wire_len;
  int ret;

  if (source_len == 0 || source_len > RV_SOURCE_BLOCK ||
      (source_len & 1) != 0)
    {
      return -EINVAL;
    }

  wire_len = source_len * 2;
  source_to_stereo_wire(source, source_len, wire);

  memset(&desc, 0, sizeof(desc));
  desc.numbytes = wire_len;
  desc.u.pbuffer = &slot->apb;
  ret = apb_alloc(&desc);
  if (ret < 0 || slot->apb == NULL)
    {
      slot->apb = NULL;
      return ret < 0 ? ret : -ENOMEM;
    }

  memcpy(slot->apb->samp, wire, wire_len);
  slot->apb->nbytes = wire_len;
  slot->apb->nsamples = source_len / 2;
  slot->result = -EINPROGRESS;
  slot->active = true;

  ret = I2S_SEND(speaker, slot->apb, robot_playback_complete, slot,
                 MSEC2TICK(RV_SEND_TIMEOUT_MS));
  if (ret < 0)
    {
      slot->active = false;
      apb_free(slot->apb);
      slot->apb = NULL;
      return ret;
    }

  return 0;
}

static int playback_wait_slot(struct playback_slot_s *slot)
{
  int ret;

  if (!slot->active)
    {
      return 0;
    }

  do
    {
      ret = nxsem_wait(&slot->done);
    }
  while (ret < 0 && errno == EINTR);

  if (ret < 0)
    {
      return -errno;
    }

  ret = slot->result;
  slot->active = false;

  if (slot->apb != NULL)
    {
      apb_free(slot->apb);
      slot->apb = NULL;
    }

  return ret;
}

static void *robot_playback_worker(void *arg)
{
  struct i2s_dev_s *speaker;
  struct playback_slot_s slots[RV_DMA_SLOTS];
  uint8_t source[RV_DMA_SLOTS][RV_SOURCE_BLOCK];
  unsigned int current = 0;
  int ret = 0;
  unsigned int i;
  (void)arg;

  memset(slots, 0, sizeof(slots));
  for (i = 0; i < RV_DMA_SLOTS; i++)
    {
      nxsem_init(&slots[i].done, 0, 0);
    }

  speaker = board_voice_speaker_i2s();
  if (speaker == NULL)
    {
      ret = -ENODEV;
      goto out;
    }

  if (I2S_TXCHANNELS(speaker, 2) < 0 ||
      I2S_TXSAMPLERATE(speaker, ROBOT_VOICE_TTS_RATE) != ROBOT_VOICE_TTS_RATE ||
      I2S_TXDATAWIDTH(speaker, ROBOT_VOICE_BITS) != ROBOT_VOICE_BITS)
    {
      ret = -ENOTSUP;
      goto out;
    }

  ret = I2S_IOCTL(speaker, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      goto out;
    }

  printf("[RV-PB] worker start rate=%u source=mono16 wire=stereo-dup "
         "ring=%u prebuffer=%u dma_slots=%u\n",
         (unsigned int)ROBOT_VOICE_TTS_RATE,
         (unsigned int)RV_RING_BYTES,
         (unsigned int)RV_PREBUFFER_BYTES,
         (unsigned int)RV_DMA_SLOTS);

  for (;;)
    {
      size_t take;

      /* Keep two DMA transfers outstanding.  contest_i2s can chain active and
       * pending descriptors; waiting after every single send inserts a worker
       * scheduling gap and was a major source of periodic discontinuities.
       */

      if (slots[current].active)
        {
          ret = playback_wait_slot(&slots[current]);
          if (ret < 0)
            {
              break;
            }
        }

      take = playback_take_source(source[current]);
      if (take > 0)
        {
          ret = playback_queue_slot(speaker, &slots[current],
                                    source[current], take);
          if (ret < 0)
            {
              break;
            }

          current = (current + 1) % RV_DMA_SLOTS;
          continue;
        }

      if (playback_is_drained())
        {
          /* Drain any transfer queued in the other slot before stopping I2S. */
          for (i = 0; i < RV_DMA_SLOTS; i++)
            {
              ret = playback_wait_slot(&slots[i]);
              if (ret < 0)
                {
                  break;
                }
            }
          break;
        }

      do
        {
          ret = nxsem_wait(&g_play.wake);
        }
      while (ret < 0 && errno == EINTR);

      if (ret < 0)
        {
          ret = -errno;
          break;
        }
    }

  I2S_IOCTL(speaker, AUDIOIOC_STOP, 0);

out:
  for (i = 0; i < RV_DMA_SLOTS; i++)
    {
      if (slots[i].active)
        {
          int wait_ret = playback_wait_slot(&slots[i]);
          if (ret == 0 && wait_ret < 0)
            {
              ret = wait_ret;
            }
        }
      nxsem_destroy(&slots[i].done);
    }

  pthread_mutex_lock(&g_play.lock);
  if (g_play.error == 0 && ret < 0)
    {
      g_play.error = ret;
    }
  pthread_mutex_unlock(&g_play.lock);

  /* Release any writer blocked waiting for ring space. */
  nxsem_post(&g_play.space);
  printf("[RV-PB] worker exit rc=%d\n", ret);
  return NULL;
}

int robot_audio_playback_open(void)
{
  int ret;

  if (g_play.opened)
    {
      return -EBUSY;
    }

  memset(&g_play, 0, sizeof(g_play));
  g_play.ring = malloc(RV_RING_BYTES);
  if (g_play.ring == NULL)
    {
      return -ENOMEM;
    }

  ret = pthread_mutex_init(&g_play.lock, NULL);
  if (ret != 0)
    {
      free(g_play.ring);
      memset(&g_play, 0, sizeof(g_play));
      return -ret;
    }

  nxsem_init(&g_play.wake, 0, 0);
  nxsem_init(&g_play.space, 0, 0);
  g_play.opened = true;

  ret = pthread_create(&g_play.thread, NULL, robot_playback_worker, NULL);
  if (ret != 0)
    {
      robot_audio_playback_close();
      return -ret;
    }

  printf("[RV-PB] open ring=%u prebuffer=%u source_block=%u wire_block=%u\n",
         (unsigned int)RV_RING_BYTES,
         (unsigned int)RV_PREBUFFER_BYTES,
         (unsigned int)RV_SOURCE_BLOCK,
         (unsigned int)RV_WIRE_BLOCK);
  return 0;
}

int robot_audio_playback_write(const uint8_t *pcm, size_t len)
{
  size_t written = 0;

  if (!g_play.opened || (pcm == NULL && len > 0) || (len & 1) != 0)
    {
      return -EINVAL;
    }

  while (written < len)
    {
      size_t free_bytes;
      size_t chunk;
      size_t first;
      int error;
      bool finished;

      pthread_mutex_lock(&g_play.lock);
      error = g_play.error;
      finished = g_play.finished;
      free_bytes = RV_RING_BYTES - g_play.used;

      if (error < 0 || finished)
        {
          pthread_mutex_unlock(&g_play.lock);
          return error < 0 ? error : -EPIPE;
        }

      chunk = len - written;
      if (chunk > free_bytes)
        {
          chunk = free_bytes;
        }
      chunk &= ~(size_t)1;

      if (chunk > 0)
        {
          first = RV_RING_BYTES - g_play.head;
          if (first > chunk)
            {
              first = chunk;
            }

          memcpy(g_play.ring + g_play.head, pcm + written, first);
          if (chunk > first)
            {
              memcpy(g_play.ring, pcm + written + first, chunk - first);
            }

          g_play.head = (g_play.head + chunk) % RV_RING_BYTES;
          g_play.used += chunk;
        }

      pthread_mutex_unlock(&g_play.lock);

      if (chunk > 0)
        {
          written += chunk;
          nxsem_post(&g_play.wake);
          continue;
        }

      /* The decoder can run hundreds of times faster than 24-kHz playback.
       * Do not fail with -EAGAIN when the ring is full; apply backpressure and
       * resume as soon as the DMA worker consumes another block.
       */

      do
        {
          error = nxsem_wait(&g_play.space);
        }
      while (error < 0 && errno == EINTR);

      if (error < 0)
        {
          return -errno;
        }
    }

  return 0;
}

int robot_audio_playback_finish(void)
{
  int ret;

  if (!g_play.opened)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_play.lock);
  g_play.finished = true;
  pthread_mutex_unlock(&g_play.lock);

  nxsem_post(&g_play.wake);
  nxsem_post(&g_play.space);

  if (g_play.thread != 0)
    {
      ret = pthread_join(g_play.thread, NULL);
      if (ret != 0)
        {
          return -ret;
        }
      g_play.thread = 0;
    }

  return g_play.error;
}

void robot_audio_playback_close(void)
{
  if (!g_play.opened)
    {
      return;
    }

  pthread_mutex_lock(&g_play.lock);
  g_play.finished = true;
  pthread_mutex_unlock(&g_play.lock);

  nxsem_post(&g_play.wake);
  nxsem_post(&g_play.space);

  if (g_play.thread != 0)
    {
      pthread_join(g_play.thread, NULL);
      g_play.thread = 0;
    }

  pthread_mutex_destroy(&g_play.lock);
  nxsem_destroy(&g_play.wake);
  nxsem_destroy(&g_play.space);
  free(g_play.ring);
  memset(&g_play, 0, sizeof(g_play));
}
