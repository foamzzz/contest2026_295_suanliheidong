#include <nuttx/config.h>

#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/clock.h>
#include <nuttx/semaphore.h>

#include <arch/board/board.h>

#include "robot_audio_capture.h"
#include "robot_voice_config.h"

struct capture_wait_s
{
  sem_t done;
  int result;
  uint8_t *dst;
  size_t cap;
};

static void robot_capture_complete(struct i2s_dev_s *dev,
                                   struct ap_buffer_s *apb, void *arg,
                                   int result)
{
  struct capture_wait_s *wait = arg;
  (void)dev;
  if (apb != NULL && result >= 0 && wait->dst != NULL)
    {
      size_t copy = apb->nbytes;
      if (copy > wait->cap)
        {
          copy = wait->cap;
        }
      memcpy(wait->dst, apb->samp, copy);
    }
  wait->result = result;
  nxsem_post(&wait->done);
}

int robot_audio_capture_record(uint8_t *buf, size_t cap, size_t *out_len,
                               unsigned int duration_ms)
{
  struct i2s_dev_s *mic;
  struct audio_buf_desc_s desc;
  struct ap_buffer_s *apb = NULL;
  struct capture_wait_s wait;
  size_t target;
  size_t used = 0;
  size_t chunk = 3200;
  int ret;

  if (buf == NULL || out_len == NULL || cap < 2 || duration_ms == 0)
    {
      return -EINVAL;
    }

  target = ((size_t)ROBOT_VOICE_CAPTURE_RATE * ROBOT_VOICE_BITS / 8 *
            duration_ms) / 1000;
  if (target > cap)
    {
      target = cap & ~(size_t)1;
    }

  mic = board_voice_mic_i2s();
  if (mic == NULL)
    {
      return -ENODEV;
    }

  ret = I2S_RXCHANNELS(mic, 1);
  if (ret < 0)
    {
      return ret;
    }
  ret = I2S_RXSAMPLERATE(mic, ROBOT_VOICE_CAPTURE_RATE);
  if (ret != ROBOT_VOICE_CAPTURE_RATE)
    {
      return ret < 0 ? ret : -ENOTSUP;
    }
  ret = I2S_RXDATAWIDTH(mic, ROBOT_VOICE_BITS);
  if (ret != ROBOT_VOICE_BITS)
    {
      return ret < 0 ? ret : -ENOTSUP;
    }

  ret = I2S_IOCTL(mic, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      return ret;
    }

  printf("[RV-CAP] recording %u ms\n", duration_ms);
  while (used < target)
    {
      size_t want = target - used;
      if (want > chunk)
        {
          want = chunk;
        }

      memset(&desc, 0, sizeof(desc));
      desc.numbytes = want;
      desc.u.pbuffer = &apb;
      ret = apb_alloc(&desc);
      if (ret < 0 || apb == NULL)
        {
          ret = -ENOMEM;
          break;
        }

      nxsem_init(&wait.done, 0, 0);
      wait.result = -EINPROGRESS;
      wait.dst = buf + used;
      wait.cap = want;
      ret = I2S_RECEIVE(mic, apb, robot_capture_complete, &wait,
                        MSEC2TICK(1500));
      if (ret >= 0)
        {
          while (nxsem_wait(&wait.done) < 0)
            {
            }
          ret = wait.result;
          apb_free(apb);
        }
      else
        {
          apb_free(apb);
        }
      nxsem_destroy(&wait.done);
      apb = NULL;
      if (ret < 0)
        {
          break;
        }
      used += want;
    }

  I2S_IOCTL(mic, AUDIOIOC_STOP, 0);
  if (ret >= 0)
    {
      *out_len = used;
      printf("[RV-CAP] bytes=%zu\n", used);
    }
  else
    {
      *out_len = 0;
      printf("[RV-CAP] failed rc=%d\n", ret);
    }
  return ret;
}
