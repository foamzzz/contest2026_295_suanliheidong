/****************************************************************************
 * contest2026_295_suanliheidong/app/robotctl/robotctl_servo.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <nuttx/timers/pwm.h>

#include "robotctl_servo.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ROBOTCTL_PWM_PATH         "/dev/pwm0"
#define ROBOTCTL_PWM_FREQUENCY    50
#define ROBOTCTL_SERVO_PERIOD_US  20000
#define ROBOTCTL_SERVO_MIN_US     500
#define ROBOTCTL_SERVO_MAX_US     2500
#define ROBOTCTL_NORMAL_UPDATE_MS 10
#define ROBOTCTL_SAFE_STEP_DEG    3

#if CONFIG_PWM_NCHANNELS < ROBOTCTL_SERVO_COUNT
#  error "robotctl requires five PWM channels"
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const int g_robotctl_home[ROBOTCTL_SERVO_COUNT] =
{
  90, 90, 90, 90, 85
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const int g_trim[ROBOTCTL_SERVO_COUNT] =
{
  0, 0, 0, 0, 0
};

static const char *const g_servo_names[ROBOTCTL_SERVO_COUNT] =
{
  "RF", "RR", "LR", "LF", "TAIL"
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int robotctl_clamp_angle(int angle)
{
  if (angle < 0)
    {
      return 0;
    }

  if (angle > 180)
    {
      return 180;
    }

  return angle;
}

static ub16_t robotctl_angle_to_duty(int angle)
{
  uint32_t pulse_us;

  angle = robotctl_clamp_angle(angle);
  pulse_us = ROBOTCTL_SERVO_MIN_US +
             ((ROBOTCTL_SERVO_MAX_US - ROBOTCTL_SERVO_MIN_US) * angle) / 180;

  return (ub16_t)(((uint64_t)pulse_us << 16) / ROBOTCTL_SERVO_PERIOD_US);
}

static int robotctl_duty_to_angle(ub16_t duty)
{
  uint32_t pulse_us;

  pulse_us = ((uint64_t)duty * ROBOTCTL_SERVO_PERIOD_US) >> 16;
  if (pulse_us < ROBOTCTL_SERVO_MIN_US ||
      pulse_us > ROBOTCTL_SERVO_MAX_US)
    {
      return -1;
    }

  return (int)(((pulse_us - ROBOTCTL_SERVO_MIN_US) * 180) /
               (ROBOTCTL_SERVO_MAX_US - ROBOTCTL_SERVO_MIN_US));
}

static int robotctl_apply_frame(struct robotctl_servo_ctx_s *ctx,
                                const int angle[ROBOTCTL_SERVO_COUNT],
                                unsigned int active_mask)
{
  struct pwm_info_s info;
  int saved_errno;
  int ret;
  int i;

  memset(&info, 0, sizeof(info));
  info.frequency = ROBOTCTL_PWM_FREQUENCY;

  for (i = 0; i < ROBOTCTL_SERVO_COUNT; i++)
    {
      info.channels[i].channel = i + 1;
      if ((active_mask & (1u << i)) != 0)
        {
          info.channels[i].duty =
            robotctl_angle_to_duty(angle[i] + g_trim[i]);
        }

      info.channels[i].cpol = PWM_CPOL_NDEF;
      info.channels[i].dcpol = PWM_DCPOL_LOW;
    }

  ret = ioctl(ctx->fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)((uintptr_t)&info));
  if (ret < 0)
    {
      saved_errno = errno;
      fprintf(stderr,
              "robotctl: PWMIOC_SETCHARACTERISTICS failed errno=%d\n",
              saved_errno);
      return -saved_errno;
    }

  ret = ioctl(ctx->fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      saved_errno = errno;
      fprintf(stderr, "robotctl: PWMIOC_START failed errno=%d\n",
              saved_errno);
      return -saved_errno;
    }

  return 0;
}

static int robotctl_servo_open_device(struct robotctl_servo_ctx_s *ctx)
{
  int saved_errno;

  ctx->fd = open(ROBOTCTL_PWM_PATH, O_RDONLY);
  if (ctx->fd < 0)
    {
      saved_errno = errno;
      fprintf(stderr, "robotctl: open %s failed errno=%d\n",
              ROBOTCTL_PWM_PATH, saved_errno);
      return -saved_errno;
    }

  return 0;
}

static void robotctl_servo_stop_and_close(struct robotctl_servo_ctx_s *ctx)
{
  if (ctx->fd >= 0)
    {
      (void)robotctl_servo_stop(ctx, false);
      robotctl_servo_close(ctx);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

const char *robotctl_servo_name(enum robotctl_servo_e servo)
{
  if (servo < 0 || servo >= ROBOTCTL_SERVO_COUNT)
    {
      return "UNKNOWN";
    }

  return g_servo_names[servo];
}

int robotctl_servo_from_name(const char *name)
{
  int i;

  for (i = 0; i < ROBOTCTL_SERVO_COUNT; i++)
    {
      if (strcasecmp(name, g_servo_names[i]) == 0)
        {
          return i;
        }
    }

  return -EINVAL;
}

int robotctl_servo_open(struct robotctl_servo_ctx_s *ctx)
{
  struct pwm_info_s info;
  int angle;
  int ret;
  int i;

  memcpy(ctx->angle, g_robotctl_home, sizeof(ctx->angle));
  ret = robotctl_servo_open_device(ctx);
  if (ret < 0)
    {
      return ret;
    }

  memset(&info, 0, sizeof(info));
  if (ioctl(ctx->fd, PWMIOC_GETCHARACTERISTICS,
            (unsigned long)((uintptr_t)&info)) == 0 &&
      info.frequency == ROBOTCTL_PWM_FREQUENCY)
    {
      for (i = 0; i < ROBOTCTL_SERVO_COUNT; i++)
        {
          angle = robotctl_duty_to_angle(info.channels[i].duty);
          if (angle >= 0)
            {
              ctx->angle[i] = robotctl_clamp_angle(angle - g_trim[i]);
            }
        }
    }

  return 0;
}

void robotctl_servo_close(struct robotctl_servo_ctx_s *ctx)
{
  if (ctx->fd >= 0)
    {
      close(ctx->fd);
      ctx->fd = -1;
    }
}

int robotctl_servo_stop(struct robotctl_servo_ctx_s *ctx, bool announce)
{
  int zero[ROBOTCTL_SERVO_COUNT] =
  {
    0, 0, 0, 0, 0
  };

  int first_error = 0;
  int saved_errno;
  int ret;

  ret = robotctl_apply_frame(ctx, zero, 0);
  if (ret < 0)
    {
      first_error = ret;
    }

  ret = ioctl(ctx->fd, PWMIOC_STOP, 0);
  if (ret < 0)
    {
      saved_errno = errno;
      fprintf(stderr, "robotctl: PWMIOC_STOP failed errno=%d\n",
              saved_errno);
      return -saved_errno;
    }

  if (announce)
    {
      printf("robotctl: servo PWM stopped\n");
    }

  return first_error;
}

int robotctl_servo_move_one(struct robotctl_servo_ctx_s *ctx,
                            enum robotctl_servo_e servo, int target,
                            int interval_ms, int settle_ms)
{
  int frame[ROBOTCTL_SERVO_COUNT];
  int current;
  int next;
  int ret;

  if (ctx->fd < 0)
    {
      ret = robotctl_servo_open_device(ctx);
      if (ret < 0)
        {
          return ret;
        }
    }

  target = robotctl_clamp_angle(target);
  current = ctx->angle[servo];
  memcpy(frame, ctx->angle, sizeof(frame));

  do
    {
      if (current < target)
        {
          next = current + ROBOTCTL_SAFE_STEP_DEG;
          if (next > target)
            {
              next = target;
            }
        }
      else if (current > target)
        {
          next = current - ROBOTCTL_SAFE_STEP_DEG;
          if (next < target)
            {
              next = target;
            }
        }
      else
        {
          next = target;
        }

      frame[servo] = next;
      ret = robotctl_apply_frame(ctx, frame, 1u << servo);
      if (ret < 0)
        {
          robotctl_servo_stop_and_close(ctx);
          return ret;
        }

      current = next;
      ctx->angle[servo] = next;
      if (current != target)
        {
          usleep(interval_ms * 1000);
        }
    }
  while (current != target);

  usleep(settle_ms * 1000);
  ret = robotctl_servo_stop(ctx, false);
  robotctl_servo_close(ctx);
  return ret;
}

int robotctl_servo_move_all(struct robotctl_servo_ctx_s *ctx,
                            const int target[ROBOTCTL_SERVO_COUNT],
                            int duration_ms)
{
  int frame[ROBOTCTL_SERVO_COUNT];
  int start[ROBOTCTL_SERVO_COUNT];
  int steps;
  int ret;
  int i;
  int step;

  memcpy(start, ctx->angle, sizeof(start));
  steps = duration_ms / ROBOTCTL_NORMAL_UPDATE_MS;
  if (steps < 1)
    {
      steps = 1;
    }

  for (step = 1; step <= steps; step++)
    {
      for (i = 0; i < ROBOTCTL_SERVO_COUNT; i++)
        {
          frame[i] = start[i] + ((target[i] - start[i]) * step) / steps;
        }

      ret = robotctl_apply_frame(ctx, frame,
                                 (1u << ROBOTCTL_SERVO_COUNT) - 1u);
      if (ret < 0)
        {
          return ret;
        }

      if (step != steps)
        {
          usleep(ROBOTCTL_NORMAL_UPDATE_MS * 1000);
        }
    }

  memcpy(ctx->angle, target, sizeof(ctx->angle));
  return 0;
}
