#include <nuttx/config.h>

#include <errno.h>
#include <string.h>

#include "mimo_stream.h"

static int b64_value(unsigned char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int mimo_stream_wav_base64(const char *b64, mimo_pcm_cb_t cb, void *arg)
{
  unsigned char q[4]; unsigned char header[44]; unsigned char out[3];
  size_t qlen = 0; size_t header_len = 0; size_t i; int ret;
  if (!b64 || !cb) return -EINVAL;
  for (i = 0; b64[i] != '\0'; i++)
    {
      unsigned char c = (unsigned char)b64[i];
      if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
      q[qlen++] = c;
      if (qlen == 4)
        {
          int a = b64_value(q[0]);
          int b = b64_value(q[1]);
          int cc = q[2] == '=' ? 0 : b64_value(q[2]);
          int d = q[3] == '=' ? 0 : b64_value(q[3]);
          if (a < 0 || b < 0 || (q[2] != '=' && cc < 0) ||
              (q[3] != '=' && d < 0) || (q[2] == '=' && q[3] != '='))
            return -EPROTO;
          out[0] = (unsigned char)((a << 2) | (b >> 4));
          out[1] = (unsigned char)((b << 4) | (cc >> 2));
          out[2] = (unsigned char)((cc << 6) | d);
          size_t n = q[2] == '=' ? 1 : (q[3] == '=' ? 2 : 3);
          size_t pos = 0;
          while (pos < n)
            {
              if (header_len < sizeof(header)) header[header_len++] = out[pos++];
              else { ret = cb(out + pos, n - pos, arg); if (ret < 0) return ret; break; }
            }
          qlen = 0;
        }
    }
  if (qlen != 0 || header_len < 44 || memcmp(header, "RIFF", 4) != 0 ||
      memcmp(header + 8, "WAVE", 4) != 0)
    return -EPROTO;
  /* Header bytes after the first 44 were already emitted above. */
  return 0;
}
