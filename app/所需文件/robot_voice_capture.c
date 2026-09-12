#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

#include <arch/board/board.h>

#include "robot_voice_capture.h"
#include "robot_voice_config.h"

/* Keep the verified board-capture transaction shape: one 1024-byte logical
 * RX at a time.  The contest I2S lower half expands this to 2048 DMA bytes. */

#define ROBOT_VOICE_RX_DMA_BYTES       1024
#define ROBOT_VOICE_RX_WARMUP_CHUNKS   2
#define ROBOT_VOICE_RX_TIMEOUT_MS      1000
#define ROBOT_VOICE_RX_POLL_MS         50
#define ROBOT_VOICE_RECORD_TIMEOUT_MS  7000

struct robot_voice_rx_xfer_s
{
  sem_t done;
  mutex_t lock;
  FAR struct ap_buffer_s *apb;
  size_t nbytes;
  int result;
  bool callback_done;
  bool in_flight;
  unsigned int chunk;
};

static bool robot_voice_capture_stop_requested(
  FAR volatile bool *stop_requested)
{
  return stop_requested != NULL && *stop_requested;
}

static void robot_voice_rx_callback(FAR struct i2s_dev_s *dev,
                                    FAR struct ap_buffer_s *apb,
                                    FAR void *arg, int result)
{
  FAR struct robot_voice_rx_xfer_s *xfer = arg;

  (void)dev;

  nxmutex_lock(&xfer->lock);
  xfer->nbytes = apb == NULL ? 0 : apb->nbytes;
  xfer->result = result;
  xfer->callback_done = true;
  xfer->in_flight = false;
  if (xfer->chunk < 2 || result < 0)
    {
      printf("[RV-CAP] callback chunk=%u rc=%d bytes=%zu\n", xfer->chunk,
             result, xfer->nbytes);
    }

  nxsem_post(&xfer->done);
  nxmutex_unlock(&xfer->lock);
}

static void robot_voice_rx_xfer_init(FAR struct robot_voice_rx_xfer_s *xfer,
                                     FAR struct ap_buffer_s *apb,
                                     unsigned int chunk)
{
  memset(xfer, 0, sizeof(*xfer));
  nxsem_init(&xfer->done, 0, 0);
  nxmutex_init(&xfer->lock);
  xfer->apb = apb;
  xfer->result = -EINPROGRESS;
  xfer->chunk = chunk;
}

static void robot_voice_rx_xfer_destroy(
  FAR struct robot_voice_rx_xfer_s *xfer)
{
  /* The callback's final use of xfer is protected by this mutex. */

  nxmutex_lock(&xfer->lock);
  nxmutex_unlock(&xfer->lock);
  nxmutex_destroy(&xfer->lock);
  nxsem_destroy(&xfer->done);
}

static int robot_voice_rx_result(FAR struct robot_voice_rx_xfer_s *xfer,
                                 size_t *nbytes)
{
  int result;

  nxmutex_lock(&xfer->lock);
  result = xfer->result;
  if (nbytes != NULL)
    {
      *nbytes = xfer->nbytes;
    }
  nxmutex_unlock(&xfer->lock);
  return result;
}

static bool robot_voice_rx_pending(FAR struct robot_voice_rx_xfer_s *xfer)
{
  bool pending;

  nxmutex_lock(&xfer->lock);
  pending = xfer->in_flight && !xfer->callback_done;
  nxmutex_unlock(&xfer->lock);
  return pending;
}

static int robot_voice_rx_wait(FAR struct robot_voice_rx_xfer_s *xfer,
                               clock_t deadline,
                               FAR volatile bool *stop_requested,
                               size_t *nbytes)
{
  int ret;

  for (;;)
    {
      ret = nxsem_tickwait(&xfer->done, MSEC2TICK(ROBOT_VOICE_RX_POLL_MS));
      if (ret >= 0)
        {
          return robot_voice_rx_result(xfer, nbytes);
        }

      if (ret == -EINTR)
        {
          continue;
        }

      if (ret != -ETIMEDOUT)
        {
          return ret;
        }

      if (robot_voice_capture_stop_requested(stop_requested))
        {
          return -ECANCELED;
        }

      if ((int32_t)(deadline - clock_systime_ticks()) <= 0)
        {
          return -ETIMEDOUT;
        }
    }
}

static int robot_voice_rx_cancel_and_drain(
  FAR struct i2s_dev_s *mic, FAR struct robot_voice_rx_xfer_s *xfer)
{
  bool pending = robot_voice_rx_pending(xfer);
  int stop_ret;
  int ret = OK;

  stop_ret = I2S_IOCTL(mic, AUDIOIOC_STOP, 0);
  printf("[RV-CAP] stop requested rc=%d pending=%d\n", stop_ret, pending);

  if (pending)
    {
      do
        {
          ret = nxsem_wait(&xfer->done);
        }
      while (ret == -EINTR);

      if (ret >= 0)
        {
          ret = robot_voice_rx_result(xfer, NULL);
          printf("[RV-CAP] callback drained chunk=%u result=%d\n",
                 xfer->chunk, ret);
        }
    }

  return stop_ret < 0 ? stop_ret : ret;
}

static int robot_voice_rx_submit(FAR struct i2s_dev_s *mic,
                                 FAR struct robot_voice_rx_xfer_s *xfer,
                                 size_t request_bytes)
{
  int ret;

  printf("[RV-CAP] receive submit chunk=%u logical=%zu\n", xfer->chunk,
         request_bytes);
  xfer->in_flight = true;
  ret = I2S_RECEIVE(mic, xfer->apb, robot_voice_rx_callback, xfer,
                    MSEC2TICK(ROBOT_VOICE_RX_TIMEOUT_MS));
  if (xfer->chunk < 2 || ret < 0)
    {
      printf("[RV-CAP] I2S_RECEIVE chunk=%u rc=%d\n", xfer->chunk, ret);
    }

  if (ret < 0)
    {
      nxmutex_lock(&xfer->lock);
      xfer->in_flight = false;
      nxmutex_unlock(&xfer->lock);
    }

  return ret;
}

static int robot_voice_rx_copy(FAR const struct robot_voice_rx_xfer_s *xfer,
                               uint8_t *dst, size_t cap, size_t *used,
                               size_t request_bytes, size_t nbytes)
{
  FAR const int16_t *src;
  FAR int16_t *out;
  size_t samples;
  size_t i;

  if (xfer->apb == NULL || dst == NULL || used == NULL ||
      nbytes != request_bytes || nbytes > cap - *used)
    {
      return -EIO;
    }

  memcpy(dst + *used, xfer->apb->samp, nbytes);
  src = (FAR const int16_t *)(dst + *used);
  out = (FAR int16_t *)(dst + *used);
  samples = nbytes / sizeof(int16_t);
  for (i = 0; i < samples; i++)
    {
      int32_t gained = (int32_t)src[i] * 2;

      if (gained > INT16_MAX)
        {
          gained = INT16_MAX;
        }
      else if (gained < INT16_MIN)
        {
          gained = INT16_MIN;
        }

      out[i] = (int16_t)gained;
    }

  *used += nbytes;
  return OK;
}

static void robot_voice_rx_release(FAR struct robot_voice_rx_xfer_s *xfer)
{
  /* The application retains the apb_alloc() reference.  The lower half
   * retained and releases its own reference around the callback. */

  if (xfer->apb != NULL)
    {
      apb_free(xfer->apb);
      xfer->apb = NULL;
    }

  robot_voice_rx_xfer_destroy(xfer);
}

int robot_voice_capture_record_interruptible(
  uint8_t *buf, size_t cap, size_t *out_len, unsigned int duration_ms,
  FAR volatile bool *stop_requested)
{
  FAR struct i2s_dev_s *mic;
  size_t target;
  size_t capture_target;
  size_t submitted_bytes = 0;
  size_t used = 0;
  unsigned int submitted = 0;
  unsigned int completed = 0;
  unsigned int warmup_chunks = 0;
  clock_t deadline;
  int ret;

  if (buf == NULL || out_len == NULL || cap < sizeof(int16_t) ||
      duration_ms == 0)
    {
      return -EINVAL;
    }

  *out_len = 0;
  target = ((size_t)ROBOT_VOICE_CAPTURE_RATE * sizeof(int16_t) *
            duration_ms) / 1000;
  target = target > cap ? cap : target;
  target -= target % sizeof(int16_t);
  if (target == 0)
    {
      return -EINVAL;
    }

  mic = board_voice_mic_i2s();
  if (mic == NULL)
    {
      return -ENODEV;
    }

  ret = I2S_RXCHANNELS(mic, ROBOT_VOICE_CHANNELS);
  printf("[RV-CAP] set channels %d rc=%d\n", ROBOT_VOICE_CHANNELS, ret);
  if (ret < 0)
    {
      return ret;
    }

  ret = I2S_RXSAMPLERATE(mic, ROBOT_VOICE_CAPTURE_RATE);
  printf("[RV-CAP] set rate %d rc=%d\n", ROBOT_VOICE_CAPTURE_RATE, ret);
  if (ret != ROBOT_VOICE_CAPTURE_RATE)
    {
      return ret < 0 ? ret : -ENOTSUP;
    }

  ret = I2S_RXDATAWIDTH(mic, ROBOT_VOICE_BITS);
  printf("[RV-CAP] set width %d rc=%d\n", ROBOT_VOICE_BITS, ret);
  if (ret != ROBOT_VOICE_BITS)
    {
      return ret < 0 ? ret : -ENOTSUP;
    }

  board_voice_mic_set_diagnostics(false);
  ret = I2S_IOCTL(mic, AUDIOIOC_START, 0);
  printf("[RV-CAP] start rc=%d\n", ret);
  if (ret < 0)
    {
      return ret;
    }

  capture_target = target + ROBOT_VOICE_RX_WARMUP_CHUNKS *
                             ROBOT_VOICE_RX_DMA_BYTES;
  deadline = clock_systime_ticks() + MSEC2TICK(ROBOT_VOICE_RECORD_TIMEOUT_MS);
  printf("[RV-CAP] recording %u ms target=%zu chunk=%d warmup=%d\n",
         duration_ms, target, ROBOT_VOICE_RX_DMA_BYTES,
         ROBOT_VOICE_RX_WARMUP_CHUNKS);

  while (submitted_bytes < capture_target)
    {
      struct audio_buf_desc_s desc;
      struct robot_voice_rx_xfer_s xfer;
      FAR struct ap_buffer_s *apb = NULL;
      size_t request_bytes = capture_target - submitted_bytes;
      size_t nbytes = 0;

      if (robot_voice_capture_stop_requested(stop_requested))
        {
          ret = -ECANCELED;
          goto stop;
        }

      if (request_bytes > ROBOT_VOICE_RX_DMA_BYTES)
        {
          request_bytes = ROBOT_VOICE_RX_DMA_BYTES;
        }

      memset(&desc, 0, sizeof(desc));
      desc.numbytes = request_bytes;
      desc.u.pbuffer = &apb;
      ret = apb_alloc(&desc);
      if (ret < 0 || apb == NULL)
        {
          ret = ret < 0 ? ret : -ENOMEM;
          goto stop;
        }

      robot_voice_rx_xfer_init(&xfer, apb, submitted);
      ret = robot_voice_rx_submit(mic, &xfer, request_bytes);
      if (ret < 0)
        {
          robot_voice_rx_release(&xfer);
          goto stop;
        }

      ret = robot_voice_rx_wait(&xfer, deadline, stop_requested, &nbytes);
      if (ret < 0)
        {
          int drain_ret = robot_voice_rx_cancel_and_drain(mic, &xfer);

          robot_voice_rx_release(&xfer);
          if (ret == -ECANCELED && drain_ret < 0 && drain_ret != -ECANCELED)
            {
              ret = drain_ret;
            }
          goto stopped;
        }

      if (robot_voice_capture_stop_requested(stop_requested))
        {
          ret = -ECANCELED;
          (void)robot_voice_rx_cancel_and_drain(mic, &xfer);
          robot_voice_rx_release(&xfer);
          goto stopped;
        }

      if (warmup_chunks < ROBOT_VOICE_RX_WARMUP_CHUNKS)
        {
          warmup_chunks++;
          printf("[RV-CAP] warmup discard chunk=%u bytes=%zu\n", submitted,
                 nbytes);
        }
      else
        {
          ret = robot_voice_rx_copy(&xfer, buf, cap, &used, request_bytes,
                                    nbytes);
          if (ret < 0)
            {
              (void)robot_voice_rx_cancel_and_drain(mic, &xfer);
              robot_voice_rx_release(&xfer);
              goto stopped;
            }
        }

      robot_voice_rx_release(&xfer);
      submitted_bytes += request_bytes;
      submitted++;
      completed++;
    }

  ret = I2S_IOCTL(mic, AUDIOIOC_STOP, 0);
  printf("[RV-CAP] stop complete rc=%d submitted=%u completed=%u\n", ret,
         submitted, completed);
  if (ret < 0)
    {
      return ret;
    }

  if (used != target || submitted_bytes != capture_target)
    {
      return -EIO;
    }

  *out_len = used;
  printf("[RV-CAP] captured %zu bytes\n", used);
  printf("[RV-CAP] capture complete\n");
  return OK;

stop:
  (void)I2S_IOCTL(mic, AUDIOIOC_STOP, 0);
stopped:
  printf("[RV-CAP] failed stage=rx rc=%d submitted=%u completed=%u\n", ret,
         submitted, completed);
  return ret;
}

int robot_voice_capture_record(uint8_t *buf, size_t cap, size_t *out_len,
                               unsigned int duration_ms)
{
  return robot_voice_capture_record_interruptible(buf, cap, out_len,
                                                  duration_ms, NULL);
}
