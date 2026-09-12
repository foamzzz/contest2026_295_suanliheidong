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

/*
 * MiMo TTS source format is verified as:
 *   pcm_s16le / 24000 Hz / mono / 16 bit
 *
 * The contest ESP32-S3 speaker I2S path is most stable when driven as a
 * conventional two-slot wire.  Each mono sample is therefore duplicated into
 * L/R slots before I2S_SEND().
 *
 * IMPORTANT: the current contest MiMo transport is synchronous: the complete
 * JSON/base64 response is already in memory before mimo_stream starts decoding
 * PCM.  Starting I2S after only 16 KiB therefore does not improve network
 * first-audio latency; it only makes base64 decoding and real-time I2S compete
 * for CPU/heap bandwidth.  The previously proven implementation was cleanest
 * when playback started after EOS.  Keep a 1 MiB ring and use the full ring as
 * a safety threshold for unusually long utterances, so the producer cannot
 * deadlock before EOS.
 */
#define RV_RING_BYTES              (1024 * 1024)
#define RV_START_PREBUFFER_BYTES   RV_RING_BYTES
#define RV_SOURCE_BLOCK            1024
#define RV_WIRE_BLOCK              (RV_SOURCE_BLOCK * 2)
#define RV_DMA_SLOTS               2
#define RV_SEND_TIMEOUT_MS         2000
#define RV_PLAYBACK_WORKER_STACK   (16 * 1024)

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
  sem_t done;
  int result;
  bool active;
  uint32_t submit_count;
  uint32_t done_count;
};

static struct playback_ctx_s g_play;

static void robot_playback_complete(struct i2s_dev_s *dev,
                                    struct ap_buffer_s *apb, void *arg,
                                    int result)
{
  struct playback_slot_s *slot = arg;

  (void)dev;
  (void)apb;

  if (slot == NULL)
    {
      return;
    }

  /* contest_i2s invokes this callback from HPWORK, not directly from the DMA
   * ISR.  Publish completion before waking the playback worker so the logical
   * slot cannot be reused while it is still marked in-flight.
   */
  pthread_mutex_lock(&g_play.lock);
  slot->result = result;
  slot->active = false;
  slot->done_count++;
  if (result < 0 && result != -ECANCELED && g_play.error == 0)
    {
      g_play.error = result;
    }
  pthread_mutex_unlock(&g_play.lock);

  nxsem_post(&slot->done);
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
      if (g_play.used >= RV_START_PREBUFFER_BYTES ||
          (g_play.finished && g_play.used > 0))
        {
          g_play.started = true;
          printf("[RV-PB] PREBUFFER -> PLAYING buffered=%zu eos=%d\n",
                 g_play.used, g_play.finished ? 1 : 0);
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
          /* PCM16: only emit complete samples at the real end of stream. */
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

static int playback_wait_slot(struct playback_slot_s *slot)
{
  int ret;

  for (;;)
    {
      bool active;
      int result;

      pthread_mutex_lock(&g_play.lock);
      active = slot->active;
      result = slot->result;
      pthread_mutex_unlock(&g_play.lock);

      if (!active)
        {
          if (result < 0 && result != -ECANCELED)
            {
              return result;
            }
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
    }
}

static int playback_queue_slot(struct i2s_dev_s *speaker,
                               struct playback_slot_s *slot,
                               const uint8_t *source, size_t source_len)
{
  struct audio_buf_desc_s desc;
  struct ap_buffer_s *apb = NULL;
  uint8_t *dst;
  size_t frames;
  size_t wire_len;
  size_t i;
  int ret;

  if (source == NULL || source_len == 0 ||
      source_len > RV_SOURCE_BLOCK || (source_len & 1) != 0)
    {
      return -EINVAL;
    }

  ret = playback_wait_slot(slot);
  if (ret < 0)
    {
      return ret;
    }

  frames = source_len / 2;
  wire_len = frames * 4;
  if (wire_len == 0 || wire_len > RV_WIRE_BLOCK)
    {
      return -EINVAL;
    }

  memset(&desc, 0, sizeof(desc));
  desc.numbytes = wire_len;
  desc.u.pbuffer = &apb;
  ret = apb_alloc(&desc);
  if (ret < 0 || apb == NULL)
    {
      return ret < 0 ? ret : -ENOMEM;
    }

  /* Write mono -> stereo directly into APB storage.  Do not place another
   * 2048-byte wire buffer on the playback worker stack: that was enough to
   * overflow/corrupt a default NuttX pthread stack when combined with the
   * worker's source buffers and I2S call chain.
   */
  dst = apb->samp;
  for (i = 0; i < frames; i++)
    {
      uint8_t lo = source[i * 2];
      uint8_t hi = source[i * 2 + 1];

      dst[i * 4] = lo;
      dst[i * 4 + 1] = hi;
      dst[i * 4 + 2] = lo;
      dst[i * 4 + 3] = hi;
    }

  apb->curbyte = 0;
  apb->nbytes = wire_len;
  apb->nsamples = frames;

  pthread_mutex_lock(&g_play.lock);
  slot->result = -EINPROGRESS;
  slot->active = true;
  slot->submit_count++;
  pthread_mutex_unlock(&g_play.lock);

  ret = I2S_SEND(speaker, apb, robot_playback_complete, slot,
                 MSEC2TICK(RV_SEND_TIMEOUT_MS));
  if (ret < 0)
    {
      pthread_mutex_lock(&g_play.lock);
      slot->result = ret;
      slot->active = false;
      if (ret != -EBUSY && g_play.error == 0)
        {
          g_play.error = ret;
        }
      pthread_mutex_unlock(&g_play.lock);

      apb_free(apb);
      return ret;
    }

  /* contest_i2s_send() took its own APB reference.  Drop the producer's
   * reference immediately; the lower half releases its reference after the
   * completion callback.  This is the ownership pattern used by the proven
   * packages_ modification/voice_echo playback implementation.
   */
  apb_free(apb);
  return 0;
}

static void *robot_playback_worker(void *arg)
{
  struct i2s_dev_s *speaker = NULL;
  struct playback_slot_s slots[RV_DMA_SLOTS];
  uint8_t source[RV_SOURCE_BLOCK];
  unsigned int slot_index = 0;
  unsigned int primed = 0;
  unsigned int i;
  int ret = 0;

  (void)arg;

  memset(slots, 0, sizeof(slots));
  for (i = 0; i < RV_DMA_SLOTS; i++)
    {
      ret = nxsem_init(&slots[i].done, 0, 0);
      if (ret < 0)
        {
          while (i > 0)
            {
              i--;
              nxsem_destroy(&slots[i].done);
            }
          goto worker_done;
        }
      slots[i].result = 0;
    }

  speaker = board_voice_speaker_i2s();
  if (speaker == NULL)
    {
      ret = -ENODEV;
      goto stop_and_destroy;
    }

  if (I2S_TXCHANNELS(speaker, 2) < 0 ||
      I2S_TXSAMPLERATE(speaker, ROBOT_VOICE_TTS_RATE) !=
        ROBOT_VOICE_TTS_RATE ||
      I2S_TXDATAWIDTH(speaker, ROBOT_VOICE_BITS) != ROBOT_VOICE_BITS)
    {
      ret = -ENOTSUP;
      goto stop_and_destroy;
    }

  ret = I2S_IOCTL(speaker, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      goto stop_and_destroy;
    }

  printf("[RV-PB] worker start stack=%u rate=%u source=mono16 "
         "wire=stereo-dup ring=%u start_prebuffer=%u dma_slots=%u "
         "decode_isolated=1\n",
         (unsigned int)RV_PLAYBACK_WORKER_STACK,
         (unsigned int)ROBOT_VOICE_TTS_RATE,
         (unsigned int)RV_RING_BYTES,
         (unsigned int)RV_START_PREBUFFER_BYTES,
         (unsigned int)RV_DMA_SLOTS);

  for (;;)
    {
      struct playback_slot_s *slot = &slots[slot_index];
      size_t take;

      /* Prime slot0/slot1, then always wait for exactly the logical slot that
       * will be refilled: 0,1,wait0/refill0,wait1/refill1,...
       */
      if (primed >= RV_DMA_SLOTS)
        {
          ret = playback_wait_slot(slot);
          if (ret < 0)
            {
              break;
            }
        }

      take = playback_take_source(source);
      if (take > 0)
        {
          ret = playback_queue_slot(speaker, slot, source, take);
          if (ret < 0)
            {
              break;
            }

          if (primed < RV_DMA_SLOTS)
            {
              primed++;
            }

          slot_index = (slot_index + 1) % RV_DMA_SLOTS;
          continue;
        }

      if (playback_is_drained())
        {
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

  /* Every logical slot must complete before AUDIOIOC_STOP and before the
   * callback semaphores go out of scope.
   */
  for (i = 0; i < RV_DMA_SLOTS; i++)
    {
      int slot_ret = playback_wait_slot(&slots[i]);
      if (slot_ret < 0 && ret >= 0)
        {
          ret = slot_ret;
        }
    }

  if (speaker != NULL)
    {
      int stop_ret = I2S_IOCTL(speaker, AUDIOIOC_STOP, 0);
      if (stop_ret < 0 && ret >= 0)
        {
          ret = stop_ret;
        }
    }

stop_and_destroy:
  if (speaker != NULL && ret < 0)
    {
      /* Abort path: force the lower half to complete/cancel any outstanding
       * callbacks, then wait each logical slot before destroying semaphores.
       */
      I2S_IOCTL(speaker, AUDIOIOC_STOP, 0);
      for (i = 0; i < RV_DMA_SLOTS; i++)
        {
          int slot_ret = playback_wait_slot(&slots[i]);
          if (slot_ret < 0 && slot_ret != -ECANCELED && ret >= 0)
            {
              ret = slot_ret;
            }
        }
    }

  printf("[RV-PB] worker exit rc=%d slots=%lu/%lu,%lu/%lu\n",
         ret,
         (unsigned long)slots[0].done_count,
         (unsigned long)slots[0].submit_count,
         (unsigned long)slots[1].done_count,
         (unsigned long)slots[1].submit_count);

  for (i = 0; i < RV_DMA_SLOTS; i++)
    {
      nxsem_destroy(&slots[i].done);
    }

worker_done:
  pthread_mutex_lock(&g_play.lock);
  if (g_play.error == 0 && ret < 0)
    {
      g_play.error = ret;
    }
  pthread_mutex_unlock(&g_play.lock);

  nxsem_post(&g_play.space);
  return NULL;
}

int robot_audio_playback_open(void)
{
  pthread_attr_t attr;
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

  ret = nxsem_init(&g_play.wake, 0, 0);
  if (ret < 0)
    {
      pthread_mutex_destroy(&g_play.lock);
      free(g_play.ring);
      memset(&g_play, 0, sizeof(g_play));
      return ret;
    }

  ret = nxsem_init(&g_play.space, 0, 0);
  if (ret < 0)
    {
      nxsem_destroy(&g_play.wake);
      pthread_mutex_destroy(&g_play.lock);
      free(g_play.ring);
      memset(&g_play, 0, sizeof(g_play));
      return ret;
    }

  g_play.opened = true;

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      robot_audio_playback_close();
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attr, RV_PLAYBACK_WORKER_STACK);
  if (ret == 0)
    {
      ret = pthread_create(&g_play.thread, &attr,
                           robot_playback_worker, NULL);
    }
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      g_play.thread = 0;
      robot_audio_playback_close();
      return -ret;
    }

  printf("[RV-PB] open ring=%u start_prebuffer=%u source_block=%u "
         "wire_block=%u worker_stack=%u decode_isolated=1\n",
         (unsigned int)RV_RING_BYTES,
         (unsigned int)RV_START_PREBUFFER_BYTES,
         (unsigned int)RV_SOURCE_BLOCK,
         (unsigned int)RV_WIRE_BLOCK,
         (unsigned int)RV_PLAYBACK_WORKER_STACK);
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

      /* Ring full before EOS means the utterance exceeded the isolation
       * buffer.  The worker starts at the full-ring threshold and frees space;
       * block here instead of dropping PCM.
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

  pthread_mutex_lock(&g_play.lock);
  ret = g_play.error;
  pthread_mutex_unlock(&g_play.lock);
  return ret;
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
