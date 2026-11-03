/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <infra/network_manager.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

/* One contest-local gate covers official Agent HTTPS and voice HTTP. */
static pthread_mutex_t g_http_transport_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_http_transport_cond = PTHREAD_COND_INITIALIZER;
static bool g_http_transport_busy;

int network_acquire_http_transport(uint32_t timeout_ms)
{
  struct timespec deadline;

  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += (time_t)(timeout_ms / 1000);
  deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L)
    {
      deadline.tv_sec++;
      deadline.tv_nsec -= 1000000000L;
    }

  pthread_mutex_lock(&g_http_transport_lock);
  while (g_http_transport_busy)
    {
      int ret = pthread_cond_timedwait(&g_http_transport_cond,
                                       &g_http_transport_lock, &deadline);
      if (ret == ETIMEDOUT)
        {
          pthread_mutex_unlock(&g_http_transport_lock);
          return -ETIMEDOUT;
        }
    }
  g_http_transport_busy = true;
  pthread_mutex_unlock(&g_http_transport_lock);
  return 0;
}

void network_release_http_transport(void)
{
  pthread_mutex_lock(&g_http_transport_lock);
  g_http_transport_busy = false;
  pthread_cond_signal(&g_http_transport_cond);
  pthread_mutex_unlock(&g_http_transport_lock);
}
