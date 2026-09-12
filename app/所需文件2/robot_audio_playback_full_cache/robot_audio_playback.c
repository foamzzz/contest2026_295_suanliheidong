#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
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
 * This file intentionally mirrors the playback mechanics of the previously
 * proven packages_ modified audio_playback.c.
 *
 * Verified MiMo TTS source format:
 *   pcm_s16le / 24000 Hz / mono / 16 bit
 *
 * Physical I2S wire format used by the proven implementation:
 *   24000 Hz / 2 slots / 16 bit
 *
 * Each 16-bit mono source sample is explicitly duplicated into L/R slots.
 * 1024 source bytes = 512 PCM16 frames ~= 21.33 ms at 24 kHz.
 * After duplication one DMA APB is exactly 2048 bytes.
 */
#define RV_TTS_SAMPLE_RATE          24000
#define RV_TTS_SOURCE_CHANNELS      1
#define RV_TTS_BITS                 16
#define RV_WIRE_CHANNELS            2

#define RV_SOURCE_BLOCK_BYTES       1024
#define RV_WIRE_BLOCK_BYTES         (RV_SOURCE_BLOCK_BYTES * RV_WIRE_CHANNELS)
#define RV_MAX_INFLIGHT             2
#define RV_SEND_TIMEOUT_MS          1000

/*
 * Strict two-phase playback:
 *
 *   phase 1: HTTPS/TTS/Base64/WAV decode -> PCM cache only
 *   phase 2: after EOS -> configure/start I2S -> DMA playback
 *
 * Do not run I2S or the playback worker while WLAN/TLS is active.  The prior
 * hardware tests showed that the same PCM becomes clean when network activity
 * has fully finished before I2S playback starts.
 *
 * The current TTS response buffer is 2 MiB of Base64/JSON data; decoded PCM is
 * therefore below roughly 1.5 MiB.  A 2 MiB PCM cache gives comfortable room
 * and stores about 43.7 seconds of 24 kHz mono PCM16.
 */
#define RV_RING_BYTES               (2 * 1024 * 1024)
#define RV_START_PREBUFFER_BYTES    RV_RING_BYTES
#define RV_REBUFFER_BYTES           (64 * 1024)

/* The proven version used priority 120.  Keep that real-time bias.  Use a
 * larger stack than the old 4 KiB implementation because this contest-local
 * wrapper has additional logging/call depth, while the DMA cadence remains
 * identical to the proven code.
 */
#define RV_WORKER_STACK             (16 * 1024)
#define RV_WORKER_PRIORITY          120

enum rv_play_state_e
{
  RV_STATE_PREBUFFER = 0,
  RV_STATE_PLAYING,
  RV_STATE_REBUFFER,
  RV_STATE_DRAINING,
  RV_STATE_STOPPED
};

struct rv_ring_s
{
  uint8_t *data;
  size_t head;
  size_t tail;
  size_t count;
};

struct rv_playback_s;

struct rv_slot_s
{
  struct rv_playback_s *pb;
  sem_t done;
  int result;
  int inflight;
  unsigned int index;
  uint32_t submit_count;
  uint32_t done_count;
};

struct rv_playback_s
{
  struct i2s_dev_s *i2s;

  sem_t i2s_done;
  sem_t ring_lock;
  sem_t ring_data;
  sem_t ring_space;

  pthread_t worker;
  int worker_started;
  int worker_exited;

  volatile int i2s_inflight;
  int i2s_result;
  int i2s_started;

  volatile int stopped;
  int eos;
  enum rv_play_state_e state;
  uint32_t underrun_count;

  struct rv_ring_s ring;
  struct rv_slot_s slots[RV_MAX_INFLIGHT];

  size_t total_written;
  int opened;
  int finished;
};

static struct rv_playback_s g_pb;

static int rv_lock(struct rv_playback_s *pb)
{
  return nxsem_wait_uninterruptible(&pb->ring_lock);
}

static void rv_unlock(struct rv_playback_s *pb)
{
  nxsem_post(&pb->ring_lock);
}

static size_t rv_ring_count(struct rv_playback_s *pb)
{
  size_t count;

  if (rv_lock(pb) < 0)
    {
      return pb->ring.count;
    }

  count = pb->ring.count;
  rv_unlock(pb);
  return count;
}

static int rv_get_inflight(struct rv_playback_s *pb)
{
  int value;

  if (rv_lock(pb) < 0)
    {
      return pb->i2s_inflight;
    }

  value = pb->i2s_inflight;
  rv_unlock(pb);
  return value;
}

static int rv_get_result(struct rv_playback_s *pb)
{
  int value;

  if (rv_lock(pb) < 0)
    {
      return pb->i2s_result;
    }

  value = pb->i2s_result;
  rv_unlock(pb);
  return value;
}

static int rv_ring_write(struct rv_playback_s *pb,
                         const uint8_t *src, size_t len)
{
  size_t first;
  size_t second;
  size_t space;
  int ret;

  if (pb == NULL || src == NULL || len == 0)
    {
      return -EINVAL;
    }

  if (pb->stopped)
    {
      return -ECANCELED;
    }

  ret = rv_lock(pb);
  if (ret < 0)
    {
      return ret;
    }

  if (pb->eos)
    {
      rv_unlock(pb);
      return -EPIPE;
    }

  space = RV_RING_BYTES - pb->ring.count;
  if (len > space)
    {
      size_t buffered = pb->ring.count;
      rv_unlock(pb);

      printf("[RV-PB] PCM cache full buffered=%zu incoming=%zu capacity=%u
",
             buffered, len, RV_RING_BYTES);
      return -ENOSPC;
    }

  first = RV_RING_BYTES - pb->ring.head;
  if (first > len)
    {
      first = len;
    }

  memcpy(pb->ring.data + pb->ring.head, src, first);
  pb->ring.head = (pb->ring.head + first) % RV_RING_BYTES;
  pb->ring.count += first;

  second = len - first;
  if (second > 0)
    {
      memcpy(pb->ring.data + pb->ring.head, src + first, second);
      pb->ring.head = (pb->ring.head + second) % RV_RING_BYTES;
      pb->ring.count += second;
    }

  rv_unlock(pb);
  nxsem_post(&pb->ring_data);
  return 0;
}

static size_t rv_ring_read(struct rv_playback_s *pb,
                           uint8_t *dst, size_t wanted)
{
  size_t n;
  size_t first;

  if (rv_lock(pb) < 0)
    {
      return 0;
    }

  n = pb->ring.count;
  if (n > wanted)
    {
      n = wanted;
    }

  first = RV_RING_BYTES - pb->ring.tail;
  if (first > n)
    {
      first = n;
    }

  if (first > 0)
    {
      memcpy(dst, pb->ring.data + pb->ring.tail, first);
      pb->ring.tail = (pb->ring.tail + first) % RV_RING_BYTES;
      pb->ring.count -= first;
    }

  if (first < n)
    {
      size_t second = n - first;

      memcpy(dst + first, pb->ring.data + pb->ring.tail, second);
      pb->ring.tail = (pb->ring.tail + second) % RV_RING_BYTES;
      pb->ring.count -= second;
    }

  rv_unlock(pb);

  if (n > 0)
    {
      nxsem_post(&pb->ring_space);
    }

  return n;
}

static void rv_i2s_done(struct i2s_dev_s *dev,
                        struct ap_buffer_s *apb,
                        void *arg, int result)
{
  struct rv_slot_s *slot = arg;
  struct rv_playback_s *pb;

  (void)dev;
  (void)apb;

  if (slot == NULL || slot->pb == NULL)
    {
      return;
    }

  pb = slot->pb;

  if (rv_lock(pb) == 0)
    {
      slot->result = result;
      slot->inflight = 0;
      slot->done_count++;

      if (result < 0 && result != -ECANCELED && pb->i2s_result >= 0)
        {
          pb->i2s_result = result;
        }

      if (pb->i2s_inflight > 0)
        {
          pb->i2s_inflight--;
        }

      rv_unlock(pb);
    }
  else
    {
      slot->result = result;
      slot->inflight = 0;
      slot->done_count++;

      if (result < 0 && result != -ECANCELED && pb->i2s_result >= 0)
        {
          pb->i2s_result = result;
        }

      if (pb->i2s_inflight > 0)
        {
          pb->i2s_inflight--;
        }
    }

  nxsem_post(&slot->done);
  nxsem_post(&pb->i2s_done);
}

static int rv_wait_slot(struct rv_playback_s *pb,
                        struct rv_slot_s *slot)
{
  int ret;

  if (pb == NULL || slot == NULL)
    {
      return -EINVAL;
    }

  for (;;)
    {
      int inflight;
      int result;

      ret = rv_lock(pb);
      if (ret < 0)
        {
          return ret;
        }

      inflight = slot->inflight;
      result = slot->result;
      rv_unlock(pb);

      if (!inflight)
        {
          if (result < 0 && result != -ECANCELED)
            {
              return result;
            }
          return 0;
        }

      if (pb->stopped)
        {
          return -ECANCELED;
        }

      ret = nxsem_wait_uninterruptible(&slot->done);
      if (ret < 0)
        {
          return ret;
        }
    }
}

static int rv_queue_slot(struct rv_playback_s *pb,
                         struct rv_slot_s *slot,
                         const uint8_t *src, size_t len)
{
  struct audio_buf_desc_s desc;
  struct ap_buffer_s *apb = NULL;
  uint8_t *dst;
  size_t frames;
  size_t tx_len;
  size_t i;
  int ret;

  if (pb == NULL || slot == NULL || src == NULL || len == 0 ||
      len > RV_SOURCE_BLOCK_BYTES)
    {
      return -EINVAL;
    }

  /* PCM16 source must remain sample aligned. */
  len &= ~(size_t)1;
  if (len == 0)
    {
      return -EINVAL;
    }

  frames = len / 2;
  tx_len = frames * 4;
  if (tx_len == 0 || tx_len > RV_WIRE_BLOCK_BYTES)
    {
      return -EINVAL;
    }

  ret = rv_wait_slot(pb, slot);
  if (ret < 0)
    {
      return ret;
    }

  memset(&desc, 0, sizeof(desc));
  desc.numbytes = tx_len;
  desc.u.pbuffer = &apb;

  ret = apb_alloc(&desc);
  if (ret < 0 || apb == NULL)
    {
      return ret < 0 ? ret : -ENOMEM;
    }

  /* Exact known-good byte packing:
   *   mono:   S0lo S0hi  S1lo S1hi ...
   *   wire:   S0lo S0hi S0lo S0hi  S1lo S1hi S1lo S1hi ...
   */
  dst = apb->samp;
  for (i = 0; i < frames; i++)
    {
      uint8_t lo = src[i * 2];
      uint8_t hi = src[i * 2 + 1];

      dst[i * 4] = lo;
      dst[i * 4 + 1] = hi;
      dst[i * 4 + 2] = lo;
      dst[i * 4 + 3] = hi;
    }

  apb->curbyte = 0;
  apb->nbytes = tx_len;
  apb->nsamples = frames;

  ret = rv_lock(pb);
  if (ret < 0)
    {
      apb_free(apb);
      return ret;
    }

  slot->result = -EINPROGRESS;
  slot->inflight = 1;
  slot->submit_count++;
  pb->i2s_inflight++;
  rv_unlock(pb);

  ret = I2S_SEND(pb->i2s, apb, rv_i2s_done, slot,
                 MSEC2TICK(RV_SEND_TIMEOUT_MS));
  if (ret < 0)
    {
      if (rv_lock(pb) == 0)
        {
          slot->result = ret;
          slot->inflight = 0;

          if (pb->i2s_inflight > 0)
            {
              pb->i2s_inflight--;
            }

          if (ret != -EBUSY && pb->i2s_result >= 0)
            {
              pb->i2s_result = ret;
            }

          rv_unlock(pb);
        }

      nxsem_post(&slot->done);
      apb_free(apb);
      return ret;
    }

  /* contest_i2s_send() takes its own APB reference. */
  apb_free(apb);
  return 0;
}

static int rv_drain(struct rv_playback_s *pb)
{
  int result = 0;
  int ret;

  for (;;)
    {
      ret = rv_get_result(pb);
      if (ret < 0 && result >= 0)
        {
          result = ret;
        }

      if (rv_get_inflight(pb) == 0)
        {
          return result;
        }

      ret = nxsem_wait_uninterruptible(&pb->i2s_done);
      if (ret < 0)
        {
          return ret;
        }
    }
}

static int rv_wait_buffer(struct rv_playback_s *pb, size_t threshold)
{
  for (;;)
    {
      size_t available = rv_ring_count(pb);

      if (available >= threshold)
        {
          return 0;
        }

      if (pb->eos || pb->stopped)
        {
          return 0;
        }

      if (rv_get_result(pb) < 0)
        {
          return rv_get_result(pb);
        }

      if (nxsem_wait_uninterruptible(&pb->ring_data) < 0)
        {
          return -EINTR;
        }
    }
}

static void *rv_worker(void *arg)
{
  struct rv_playback_s *pb = arg;
  uint8_t block[RV_SOURCE_BLOCK_BYTES];
  unsigned int slot_index = 0;
  int primed_slots = 0;
  int ret = 0;

  printf("[RV-PB] worker start rate=%u source=mono16 wire=stereo-dup "
         "source_block=%u wire_block=%u slots=%u priority=%u\n",
         RV_TTS_SAMPLE_RATE, RV_SOURCE_BLOCK_BYTES, RV_WIRE_BLOCK_BYTES,
         RV_MAX_INFLIGHT, RV_WORKER_PRIORITY);

  pb->state = RV_STATE_PREBUFFER;

  while (!pb->stopped)
    {
      struct rv_slot_s *slot;
      size_t available;
      size_t want;
      size_t got;

      ret = rv_get_result(pb);
      if (ret < 0)
        {
          printf("[RV-PB] I2S worker error=%d\n", ret);
          break;
        }

      if (pb->state == RV_STATE_PREBUFFER || pb->state == RV_STATE_REBUFFER)
        {
          size_t threshold = pb->state == RV_STATE_PREBUFFER ?
                             RV_START_PREBUFFER_BYTES : RV_REBUFFER_BYTES;
          const char *from = pb->state == RV_STATE_PREBUFFER ?
                             "PREBUFFER" : "REBUFFER";

          ret = rv_wait_buffer(pb, threshold);
          if (ret < 0)
            {
              break;
            }

          available = rv_ring_count(pb);
          if (available >= 2)
            {
              pb->state = RV_STATE_PLAYING;
              primed_slots = 0;
              slot_index = 0;
              printf("[RV-PB] %s -> PLAYING buffered=%zu inflight=%d\n",
                     from, available, rv_get_inflight(pb));
            }
          else if (pb->eos)
            {
              pb->state = RV_STATE_DRAINING;
            }
          else
            {
              continue;
            }
        }

      if (pb->state == RV_STATE_DRAINING)
        {
          break;
        }

      slot = &pb->slots[slot_index];

      /* Proven cadence:
       * slot0, slot1, wait0/refill0, wait1/refill1, ...
       */
      if (primed_slots >= RV_MAX_INFLIGHT)
        {
          ret = rv_wait_slot(pb, slot);
          if (ret < 0)
            {
              break;
            }
        }

      available = rv_ring_count(pb);

      if (available >= RV_SOURCE_BLOCK_BYTES)
        {
          want = RV_SOURCE_BLOCK_BYTES;
        }
      else if (pb->eos && available > 0)
        {
          want = available & ~(size_t)1;
          if (want == 0)
            {
              (void)rv_ring_read(pb, block, available);
              continue;
            }
        }
      else if (pb->eos && available == 0)
        {
          pb->state = RV_STATE_DRAINING;
          break;
        }
      else
        {
          if (rv_get_inflight(pb) > 0)
            {
              ret = nxsem_wait_uninterruptible(&pb->ring_data);
              if (ret < 0)
                {
                  break;
                }
              continue;
            }

          pb->underrun_count++;
          pb->state = RV_STATE_REBUFFER;
          printf("[RV-PB] underrun #%lu buffered=%zu -> REBUFFER %u\n",
                 (unsigned long)pb->underrun_count, available,
                 RV_REBUFFER_BYTES);
          continue;
        }

      got = rv_ring_read(pb, block, want);
      if (got == 0)
        {
          continue;
        }

      ret = rv_queue_slot(pb, slot, block, got);
      if (ret < 0)
        {
          if (ret != -ECANCELED)
            {
              printf("[RV-PB] queue slot=%u failed=%d\n",
                     slot_index, ret);
            }
          break;
        }

      if (primed_slots < RV_MAX_INFLIGHT)
        {
          primed_slots++;
        }

      slot_index = (slot_index + 1) % RV_MAX_INFLIGHT;
    }

  /* Do not stop I2S before the logical slots have completed. */
  {
    unsigned int i;

    for (i = 0; i < RV_MAX_INFLIGHT; i++)
      {
        int slot_ret = rv_wait_slot(pb, &pb->slots[i]);
        if (slot_ret < 0 && slot_ret != -ECANCELED && ret >= 0)
          {
            ret = slot_ret;
          }
      }
  }

  if (!pb->stopped)
    {
      int drain_ret = rv_drain(pb);
      if (drain_ret < 0 && ret >= 0)
        {
          ret = drain_ret;
        }
    }

  if (rv_lock(pb) == 0)
    {
      if (ret < 0 && ret != -ECANCELED && pb->i2s_result >= 0)
        {
          pb->i2s_result = ret;
        }
      pb->worker_exited = 1;
      pb->state = RV_STATE_STOPPED;
      rv_unlock(pb);
    }
  else
    {
      pb->worker_exited = 1;
      pb->state = RV_STATE_STOPPED;
    }

  printf("[RV-PB] worker exit buffered=%zu inflight=%d underruns=%lu "
         "result=%d eos=%d stopped=%d slots=%lu/%lu,%lu/%lu\n",
         rv_ring_count(pb), rv_get_inflight(pb),
         (unsigned long)pb->underrun_count, rv_get_result(pb),
         pb->eos, pb->stopped,
         (unsigned long)pb->slots[0].done_count,
         (unsigned long)pb->slots[0].submit_count,
         (unsigned long)pb->slots[1].done_count,
         (unsigned long)pb->slots[1].submit_count);

  nxsem_post(&pb->i2s_done);
  return NULL;
}

static void rv_signal_all(struct rv_playback_s *pb)
{
  unsigned int i;

  nxsem_post(&pb->ring_data);
  nxsem_post(&pb->ring_space);
  nxsem_post(&pb->i2s_done);

  for (i = 0; i < RV_MAX_INFLIGHT; i++)
    {
      nxsem_post(&pb->slots[i].done);
    }
}

static void rv_destroy(struct rv_playback_s *pb)
{
  unsigned int i;

  for (i = 0; i < RV_MAX_INFLIGHT; i++)
    {
      nxsem_destroy(&pb->slots[i].done);
    }

  nxsem_destroy(&pb->ring_space);
  nxsem_destroy(&pb->ring_data);
  nxsem_destroy(&pb->ring_lock);
  nxsem_destroy(&pb->i2s_done);

  free(pb->ring.data);
  pb->ring.data = NULL;
}

static int rv_start_i2s_playback(struct rv_playback_s *pb)
{
  pthread_attr_t attr;
  struct sched_param param;
  int actual_rate;
  int actual_width;
  int ret;

  if (pb == NULL || !pb->opened)
    {
      return -EINVAL;
    }

  if (pb->i2s_started || pb->worker_started)
    {
      return -EBUSY;
    }

  pb->i2s = board_voice_speaker_i2s();
  if (pb->i2s == NULL)
    {
      return -ENODEV;
    }

  /*
   * Keep the exact known-good format and order:
   *   2 physical slots -> 24 kHz frame rate -> 16-bit slot width.
   */
  ret = I2S_TXCHANNELS(pb->i2s, RV_WIRE_CHANNELS);
  if (ret < 0)
    {
      printf("[RV-PB] I2S_TXCHANNELS(2) failed=%d
", ret);
      goto fail;
    }

  actual_rate = (int)I2S_TXSAMPLERATE(pb->i2s, RV_TTS_SAMPLE_RATE);
  if (actual_rate != RV_TTS_SAMPLE_RATE)
    {
      printf("[RV-PB] I2S rate request=%u actual=%d
",
             RV_TTS_SAMPLE_RATE, actual_rate);
      ret = actual_rate < 0 ? actual_rate : -ENOTSUP;
      goto fail;
    }

  actual_width = (int)I2S_TXDATAWIDTH(pb->i2s, RV_TTS_BITS);
  if (actual_width != RV_TTS_BITS)
    {
      printf("[RV-PB] I2S width request=%u actual=%d
",
             RV_TTS_BITS, actual_width);
      ret = actual_width < 0 ? actual_width : -ENOTSUP;
      goto fail;
    }

  ret = I2S_IOCTL(pb->i2s, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      printf("[RV-PB] AUDIOIOC_START failed=%d
", ret);
      goto fail;
    }

  pb->i2s_started = 1;
  pb->i2s_result = 0;
  pb->state = RV_STATE_PREBUFFER;

  printf("[RV-PB] I2S START after TTS EOS "
         "source=%uHz/mono/%ubit wire=%dHz/%uch/%dbit "
         "source_block=%u wire_dma=%u
",
         RV_TTS_SAMPLE_RATE, RV_TTS_BITS,
         actual_rate, RV_WIRE_CHANNELS, actual_width,
         RV_SOURCE_BLOCK_BYTES, RV_WIRE_BLOCK_BYTES);

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      ret = -ret;
      goto fail_started;
    }

  ret = pthread_attr_setstacksize(&attr, RV_WORKER_STACK);
  if (ret != 0)
    {
      pthread_attr_destroy(&attr);
      ret = -ret;
      goto fail_started;
    }

  memset(&param, 0, sizeof(param));
  param.sched_priority = RV_WORKER_PRIORITY;
  (void)pthread_attr_setschedparam(&attr, &param);

  ret = pthread_create(&pb->worker, &attr, rv_worker, pb);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      ret = -ret;
      goto fail_started;
    }

  pb->worker_started = 1;
  return 0;

fail_started:
  if (pb->i2s_started)
    {
      pb->i2s_started = 0;
      I2S_IOCTL(pb->i2s, AUDIOIOC_STOP, 0);
    }

fail:
  pb->i2s = NULL;
  return ret;
}

int robot_audio_playback_open(void)
{
  struct rv_playback_s *pb = &g_pb;
  unsigned int i;
  int ret;

  if (pb->opened)
    {
      return -EBUSY;
    }

  memset(pb, 0, sizeof(*pb));

  if (ROBOT_VOICE_TTS_RATE != RV_TTS_SAMPLE_RATE ||
      ROBOT_VOICE_CHANNELS != RV_TTS_SOURCE_CHANNELS ||
      ROBOT_VOICE_BITS != RV_TTS_BITS)
    {
      printf("[RV-PB] source format mismatch cfg=%uHz/%uch/%ubit "
             "expected=%uHz/%uch/%ubit
",
             ROBOT_VOICE_TTS_RATE, ROBOT_VOICE_CHANNELS, ROBOT_VOICE_BITS,
             RV_TTS_SAMPLE_RATE, RV_TTS_SOURCE_CHANNELS, RV_TTS_BITS);
      return -ENOTSUP;
    }

  pb->ring.data = malloc(RV_RING_BYTES);
  if (pb->ring.data == NULL)
    {
      return -ENOMEM;
    }

  ret = nxsem_init(&pb->i2s_done, 0, 0);
  if (ret < 0)
    {
      free(pb->ring.data);
      memset(pb, 0, sizeof(*pb));
      return ret;
    }

  ret = nxsem_init(&pb->ring_lock, 0, 1);
  if (ret < 0)
    {
      nxsem_destroy(&pb->i2s_done);
      free(pb->ring.data);
      memset(pb, 0, sizeof(*pb));
      return ret;
    }

  ret = nxsem_init(&pb->ring_data, 0, 0);
  if (ret < 0)
    {
      nxsem_destroy(&pb->ring_lock);
      nxsem_destroy(&pb->i2s_done);
      free(pb->ring.data);
      memset(pb, 0, sizeof(*pb));
      return ret;
    }

  ret = nxsem_init(&pb->ring_space, 0, 0);
  if (ret < 0)
    {
      nxsem_destroy(&pb->ring_data);
      nxsem_destroy(&pb->ring_lock);
      nxsem_destroy(&pb->i2s_done);
      free(pb->ring.data);
      memset(pb, 0, sizeof(*pb));
      return ret;
    }

  for (i = 0; i < RV_MAX_INFLIGHT; i++)
    {
      ret = nxsem_init(&pb->slots[i].done, 0, 0);
      if (ret < 0)
        {
          while (i > 0)
            {
              i--;
              nxsem_destroy(&pb->slots[i].done);
            }

          nxsem_destroy(&pb->ring_space);
          nxsem_destroy(&pb->ring_data);
          nxsem_destroy(&pb->ring_lock);
          nxsem_destroy(&pb->i2s_done);
          free(pb->ring.data);
          memset(pb, 0, sizeof(*pb));
          return ret;
        }

      pb->slots[i].pb = pb;
      pb->slots[i].result = 0;
      pb->slots[i].inflight = 0;
      pb->slots[i].index = i;
    }

  pb->i2s_result = 0;
  pb->state = RV_STATE_PREBUFFER;
  pb->opened = 1;

  /*
   * Important: no board_voice_speaker_i2s(), no AUDIOIOC_START and no
   * playback worker here.  During TTS download/decode this object is only a
   * PCM cache.
   */
  printf("[RV-PB] cache-only open capacity=%u "
         "source=%uHz/mono/%ubit; I2S deferred until EOS
",
         RV_RING_BYTES, RV_TTS_SAMPLE_RATE, RV_TTS_BITS);
  return 0;
}

int robot_audio_playback_write(const uint8_t *pcm, size_t len)
{
  struct rv_playback_s *pb = &g_pb;
  int ret;

  if (!pb->opened || pcm == NULL || len == 0)
    {
      return -EINVAL;
    }

  if ((len & 1) != 0)
    {
      /* The MiMo WAV parser should emit complete PCM16 samples.  Rejecting an
       * odd chunk is safer than shifting sample alignment for all later DMA.
       */
      printf("[RV-PB] reject odd PCM chunk len=%zu\n", len);
      return -EINVAL;
    }

  if (pb->stopped)
    {
      return -ECANCELED;
    }

  ret = rv_ring_write(pb, pcm, len);
  if (ret < 0)
    {
      return ret;
    }

  pb->total_written += len;
  return 0;
}

int robot_audio_playback_finish(void)
{
  struct rv_playback_s *pb = &g_pb;
  size_t buffered;
  int ret = 0;

  if (!pb->opened)
    {
      return -EINVAL;
    }

  if (pb->finished)
    {
      return rv_get_result(pb);
    }

  pb->eos = 1;
  buffered = rv_ring_count(pb);

  printf("[RV-PB] TTS EOS cached_pcm=%zu total_pcm=%zu; "
         "starting isolated I2S playback now
",
         buffered, pb->total_written);

  if (buffered == 0)
    {
      pb->finished = 1;
      return 0;
    }

  /*
   * This is the isolation boundary.  The caller reaches finish() only after
   * the TTS transport and Base64/WAV decoder have completed, so the I2S/DMA
   * path starts with no concurrent TTS network producer.
   */
  ret = rv_start_i2s_playback(pb);
  if (ret < 0)
    {
      pb->i2s_result = ret;
      pb->finished = 1;
      printf("[RV-PB] isolated I2S start failed=%d
", ret);
      return ret;
    }

  rv_signal_all(pb);

  if (pb->worker_started)
    {
      int join_ret = pthread_join(pb->worker, NULL);

      if (join_ret != 0 && ret >= 0)
        {
          ret = -join_ret;
        }

      pb->worker_started = 0;
    }

  if (rv_get_result(pb) < 0 && ret >= 0)
    {
      ret = rv_get_result(pb);
    }

  /*
   * Worker has drained every submitted slot.  Stop the peripheral immediately
   * so no idle I2S clocks remain after the utterance.
   */
  if (pb->i2s != NULL && pb->i2s_started)
    {
      pb->i2s_started = 0;
      I2S_IOCTL(pb->i2s, AUDIOIOC_STOP, 0);
    }

  pb->finished = 1;

  printf("[RV-PB] isolated playback finish total_pcm=%zu "
         "underruns=%lu result=%d slots=%lu/%lu,%lu/%lu
",
         pb->total_written,
         (unsigned long)pb->underrun_count,
         ret,
         (unsigned long)pb->slots[0].done_count,
         (unsigned long)pb->slots[0].submit_count,
         (unsigned long)pb->slots[1].done_count,
         (unsigned long)pb->slots[1].submit_count);
  return ret;
}

void robot_audio_playback_close(void)
{
  struct rv_playback_s *pb = &g_pb;

  if (!pb->opened)
    {
      return;
    }

  if (pb->worker_started)
    {
      pb->stopped = 1;
      rv_signal_all(pb);

      if (pb->i2s != NULL && pb->i2s_started)
        {
          pb->i2s_started = 0;
          I2S_IOCTL(pb->i2s, AUDIOIOC_STOP, 0);
          rv_signal_all(pb);
        }

      pthread_join(pb->worker, NULL);
      pb->worker_started = 0;
    }

  (void)rv_drain(pb);

  if (pb->i2s != NULL && pb->i2s_started)
    {
      pb->i2s_started = 0;
      I2S_IOCTL(pb->i2s, AUDIOIOC_STOP, 0);
    }

  printf("[RV-PB] close total_pcm=%zu finished=%d underruns=%lu result=%d "
         "slots=%lu/%lu,%lu/%lu\n",
         pb->total_written, pb->finished,
         (unsigned long)pb->underrun_count,
         rv_get_result(pb),
         (unsigned long)pb->slots[0].done_count,
         (unsigned long)pb->slots[0].submit_count,
         (unsigned long)pb->slots[1].done_count,
         (unsigned long)pb->slots[1].submit_count);

  rv_destroy(pb);
  memset(pb, 0, sizeof(*pb));
}
