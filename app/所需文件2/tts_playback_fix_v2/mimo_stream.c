#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mimo_stream.h"
#include "robot_voice_config.h"

#define RV_PCM_EMIT_BYTES 4096

enum wav_state_e
{
  WAV_STATE_RIFF = 0,
  WAV_STATE_CHUNK_HEADER,
  WAV_STATE_CHUNK_PAYLOAD,
  WAV_STATE_CHUNK_PAD,
  WAV_STATE_DONE
};

struct wav_parser_s
{
  enum wav_state_e state;
  uint8_t riff[12];
  size_t riff_len;
  uint8_t chunk_header[8];
  size_t chunk_header_len;
  uint8_t chunk_id[4];
  uint32_t chunk_size;
  uint32_t chunk_pos;
  uint8_t fmt[16];
  size_t fmt_len;
  bool fmt_ok;
  bool data_seen;
  uint32_t data_bytes;
  uint8_t pcm[RV_PCM_EMIT_BYTES];
  size_t pcm_len;
  mimo_pcm_cb_t cb;
  void *arg;
};

static uint16_t get_le16(const uint8_t *p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_le32(const uint8_t *p)
{
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int b64_value(unsigned char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static int wav_emit(struct wav_parser_s *st, bool flush)
{
  size_t emit;
  int ret;

  if (st->pcm_len == 0)
    {
      return 0;
    }

  emit = st->pcm_len;
  if (!flush)
    {
      /* PCM is signed 16-bit little-endian.  Never split a sample across
       * callback boundaries.
       */
      emit &= ~(size_t)1;
      if (emit < RV_PCM_EMIT_BYTES)
        {
          return 0;
        }
    }
  else if ((emit & 1) != 0)
    {
      return -EPROTO;
    }

  ret = st->cb(st->pcm, emit, st->arg);
  if (ret < 0)
    {
      return ret;
    }

  if (emit < st->pcm_len)
    {
      memmove(st->pcm, st->pcm + emit, st->pcm_len - emit);
    }
  st->pcm_len -= emit;
  return 0;
}

static int wav_validate_fmt(struct wav_parser_s *st)
{
  uint16_t format;
  uint16_t channels;
  uint32_t sample_rate;
  uint16_t block_align;
  uint16_t bits;

  if (st->fmt_len < sizeof(st->fmt))
    {
      return -EPROTO;
    }

  format = get_le16(st->fmt + 0);
  channels = get_le16(st->fmt + 2);
  sample_rate = get_le32(st->fmt + 4);
  block_align = get_le16(st->fmt + 12);
  bits = get_le16(st->fmt + 14);

  if (format != 1 || channels != ROBOT_VOICE_CHANNELS ||
      sample_rate != ROBOT_VOICE_TTS_RATE ||
      bits != ROBOT_VOICE_BITS || block_align != 2)
    {
      printf("[RV-TTS] unsupported WAV fmt format=%u rate=%lu ch=%u bits=%u align=%u\n",
             (unsigned int)format, (unsigned long)sample_rate,
             (unsigned int)channels, (unsigned int)bits,
             (unsigned int)block_align);
      return -ENOTSUP;
    }

  st->fmt_ok = true;
  printf("[RV-TTS] WAV fmt pcm_s16le %luHz %uch %ubit\n",
         (unsigned long)sample_rate, (unsigned int)channels,
         (unsigned int)bits);
  return 0;
}

static int wav_begin_chunk(struct wav_parser_s *st)
{
  memcpy(st->chunk_id, st->chunk_header, 4);
  st->chunk_size = get_le32(st->chunk_header + 4);
  st->chunk_pos = 0;
  st->chunk_header_len = 0;
  st->fmt_len = 0;

  if (memcmp(st->chunk_id, "data", 4) == 0)
    {
      if (!st->fmt_ok)
        {
          return -EPROTO;
        }

      if ((st->chunk_size & 1) != 0)
        {
          return -EPROTO;
        }

      st->data_seen = true;
      st->data_bytes = st->chunk_size;
      printf("[RV-TTS] WAV data begin bytes=%lu rate=%u\n",
             (unsigned long)st->chunk_size,
             (unsigned int)ROBOT_VOICE_TTS_RATE);
    }

  st->state = WAV_STATE_CHUNK_PAYLOAD;
  return 0;
}

static int wav_feed_byte(struct wav_parser_s *st, uint8_t byte)
{
  int ret;

  switch (st->state)
    {
      case WAV_STATE_RIFF:
        st->riff[st->riff_len++] = byte;
        if (st->riff_len == sizeof(st->riff))
          {
            if (memcmp(st->riff, "RIFF", 4) != 0 ||
                memcmp(st->riff + 8, "WAVE", 4) != 0)
              {
                return -EPROTO;
              }
            st->state = WAV_STATE_CHUNK_HEADER;
          }
        break;

      case WAV_STATE_CHUNK_HEADER:
        st->chunk_header[st->chunk_header_len++] = byte;
        if (st->chunk_header_len == sizeof(st->chunk_header))
          {
            ret = wav_begin_chunk(st);
            if (ret < 0)
              {
                return ret;
              }
          }
        break;

      case WAV_STATE_CHUNK_PAYLOAD:
        if (memcmp(st->chunk_id, "fmt ", 4) == 0 &&
            st->fmt_len < sizeof(st->fmt))
          {
            st->fmt[st->fmt_len++] = byte;
          }
        else if (memcmp(st->chunk_id, "data", 4) == 0)
          {
            if (st->pcm_len >= sizeof(st->pcm))
              {
                ret = wav_emit(st, false);
                if (ret < 0)
                  {
                    return ret;
                  }
              }

            st->pcm[st->pcm_len++] = byte;
            if (st->pcm_len == sizeof(st->pcm))
              {
                ret = wav_emit(st, false);
                if (ret < 0)
                  {
                    return ret;
                  }
              }
          }

        st->chunk_pos++;
        if (st->chunk_pos == st->chunk_size)
          {
            if (memcmp(st->chunk_id, "fmt ", 4) == 0)
              {
                ret = wav_validate_fmt(st);
                if (ret < 0)
                  {
                    return ret;
                  }
              }
            else if (memcmp(st->chunk_id, "data", 4) == 0)
              {
                ret = wav_emit(st, true);
                if (ret < 0)
                  {
                    return ret;
                  }
                st->state = WAV_STATE_DONE;
                break;
              }

            if ((st->chunk_size & 1) != 0)
              {
                st->state = WAV_STATE_CHUNK_PAD;
              }
            else
              {
                st->state = WAV_STATE_CHUNK_HEADER;
              }
          }
        break;

      case WAV_STATE_CHUNK_PAD:
        /* RIFF chunks are word aligned.  Consume exactly one pad byte. */
        st->state = WAV_STATE_CHUNK_HEADER;
        break;

      case WAV_STATE_DONE:
        /* Ignore bytes after the WAV data chunk. */
        break;
    }

  return 0;
}

int mimo_stream_wav_base64(const char *b64, mimo_pcm_cb_t cb, void *arg)
{
  struct wav_parser_s st;
  unsigned char q[4];
  unsigned char out[3];
  size_t qlen = 0;
  size_t i = 0;
  bool prefix = false;
  int ret;

  if (b64 == NULL || cb == NULL)
    {
      return -EINVAL;
    }

  memset(&st, 0, sizeof(st));
  st.state = WAV_STATE_RIFF;
  st.cb = cb;
  st.arg = arg;

  /* Some APIs return a data URI while others return raw base64. */

  if (strncmp(b64, "data:", 5) == 0)
    {
      prefix = true;
      while (b64[i] != '\0' && b64[i] != ',')
        {
          i++;
        }
      if (b64[i] != ',')
        {
          return -EPROTO;
        }
      i++;
    }

  (void)prefix;

  for (; b64[i] != '\0'; i++)
    {
      unsigned char c = (unsigned char)b64[i];
      int a;
      int b;
      int cc;
      int d;
      size_t n;
      size_t j;

      if (c == ' ' || c == '\r' || c == '\n' || c == '\t')
        {
          continue;
        }

      q[qlen++] = c;
      if (qlen != 4)
        {
          continue;
        }

      a = b64_value(q[0]);
      b = b64_value(q[1]);
      cc = q[2] == '=' ? 0 : b64_value(q[2]);
      d = q[3] == '=' ? 0 : b64_value(q[3]);

      if (a < 0 || b < 0 || (q[2] != '=' && cc < 0) ||
          (q[3] != '=' && d < 0) ||
          (q[2] == '=' && q[3] != '='))
        {
          return -EPROTO;
        }

      out[0] = (unsigned char)((a << 2) | (b >> 4));
      out[1] = (unsigned char)((b << 4) | (cc >> 2));
      out[2] = (unsigned char)((cc << 6) | d);
      n = q[2] == '=' ? 1 : (q[3] == '=' ? 2 : 3);

      for (j = 0; j < n; j++)
        {
          ret = wav_feed_byte(&st, out[j]);
          if (ret < 0)
            {
              return ret;
            }
        }

      qlen = 0;
    }

  if (qlen != 0 || !st.fmt_ok || !st.data_seen ||
      st.state != WAV_STATE_DONE || st.pcm_len != 0)
    {
      return -EPROTO;
    }

  return 0;
}
