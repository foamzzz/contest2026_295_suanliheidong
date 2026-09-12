/****************************************************************************
 * contest2026_295_suanliheidong/app/robotctl/robotctl_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "robotctl_servo.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ROBOTCTL_VERSION              "Stage 3 POWER_SAFE"
#define ROBOTCTL_PWM_PATH             "/dev/pwm0"
#define ROBOTCTL_I2C_PATH             "/dev/i2c0"
#define ROBOTCTL_HOME_INTERVAL_MS     30
#define ROBOTCTL_HOME_SETTLE_MS       200
#define ROBOTCTL_DEFAULT_STEPS        1
#define ROBOTCTL_DEFAULT_WALK_PERIOD  2500
#define ROBOTCTL_DEFAULT_TURN_PERIOD  3000
#define ROBOTCTL_MIN_STEPS            1
#define ROBOTCTL_MAX_STEPS            10
#define ROBOTCTL_MIN_PERIOD           800
#define ROBOTCTL_MAX_PERIOD           3000
#define ROBOTCTL_PHASE_COUNT          8

#define ROBOTCTL_PHASE(rf, rr, lr, lf, first, second) \
  { {rf, rr, lr, lf, 85}, {first, second} }

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum robotctl_motion_mode_e
{
  ROBOTCTL_MOTION_POWER_SAFE = 0,
  ROBOTCTL_MOTION_NORMAL
};

struct robotctl_phase_s
{
  int target[ROBOTCTL_SERVO_COUNT];
  enum robotctl_servo_e order[2];
};

struct robotctl_gait_s
{
  const char *name;
  struct robotctl_phase_s phase[ROBOTCTL_PHASE_COUNT];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const enum robotctl_motion_mode_e g_motion_mode =
  ROBOTCTL_MOTION_POWER_SAFE;

static const enum robotctl_servo_e g_home_order[ROBOTCTL_SERVO_COUNT] =
{
  ROBOTCTL_LF, ROBOTCTL_RF, ROBOTCTL_LR, ROBOTCTL_RR, ROBOTCTL_TAIL
};

static const struct robotctl_gait_s g_forward =
{
  "forward",
  {
    ROBOTCTL_PHASE(135, 90, 45, 90, ROBOTCTL_LR, ROBOTCTL_RF),
    ROBOTCTL_PHASE(135, 45, 45, 135, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 45, 90, 135, ROBOTCTL_LR, ROBOTCTL_RF),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 135, 90, 45, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(45, 135, 135, 45, ROBOTCTL_LR, ROBOTCTL_RF),
    ROBOTCTL_PHASE(45, 90, 135, 90, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_LR, ROBOTCTL_RF)
  }
};

static const struct robotctl_gait_s g_backward =
{
  "backward",
  {
    ROBOTCTL_PHASE(45, 90, 135, 90, ROBOTCTL_LR, ROBOTCTL_RF),
    ROBOTCTL_PHASE(45, 135, 135, 45, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 135, 90, 45, ROBOTCTL_LR, ROBOTCTL_RF),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 45, 90, 135, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(135, 45, 45, 135, ROBOTCTL_LR, ROBOTCTL_RF),
    ROBOTCTL_PHASE(135, 90, 45, 90, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_LR, ROBOTCTL_RF)
  }
};

static const struct robotctl_gait_s g_left =
{
  "left",
  {
    ROBOTCTL_PHASE(135, 90, 135, 90, ROBOTCTL_RF, ROBOTCTL_LR),
    ROBOTCTL_PHASE(135, 45, 135, 45, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 45, 90, 45, ROBOTCTL_RF, ROBOTCTL_LR),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 135, 90, 135, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(45, 135, 45, 135, ROBOTCTL_RF, ROBOTCTL_LR),
    ROBOTCTL_PHASE(45, 90, 45, 90, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_RF, ROBOTCTL_LR)
  }
};

static const struct robotctl_gait_s g_right =
{
  "right",
  {
    ROBOTCTL_PHASE(45, 90, 45, 90, ROBOTCTL_RF, ROBOTCTL_LR),
    ROBOTCTL_PHASE(45, 135, 45, 135, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 135, 90, 135, ROBOTCTL_RF, ROBOTCTL_LR),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_LF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(45, 45, 90, 90, ROBOTCTL_RF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(135, 45, 135, 90, ROBOTCTL_RF, ROBOTCTL_LR),
    ROBOTCTL_PHASE(90, 90, 135, 90, ROBOTCTL_RF, ROBOTCTL_RR),
    ROBOTCTL_PHASE(90, 90, 90, 90, ROBOTCTL_RF, ROBOTCTL_LR)
  }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int robotctl_safe_interval(int period_ms)
{
  int interval_ms = period_ms / 100;

  if (interval_ms < 25)
    {
      interval_ms = 25;
    }
  else if (interval_ms > 30)
    {
      interval_ms = 30;
    }

  return interval_ms;
}

static int robotctl_safe_settle(int period_ms)
{
  int settle_ms = period_ms / 12;

  if (settle_ms < 150)
    {
      settle_ms = 150;
    }
  else if (settle_ms > 250)
    {
      settle_ms = 250;
    }

  return settle_ms;
}

static int robotctl_home_power_safe(struct robotctl_servo_ctx_s *ctx)
{
  enum robotctl_servo_e servo;
  int ret;
  int i;

  for (i = 0; i < ROBOTCTL_SERVO_COUNT; i++)
    {
      servo = g_home_order[i];
      printf("robotctl: home move %s %d -> %d\n",
             robotctl_servo_name(servo), ctx->angle[servo],
             g_robotctl_home[servo]);
      ret = robotctl_servo_move_one(ctx, servo, g_robotctl_home[servo],
                                    ROBOTCTL_HOME_INTERVAL_MS,
                                    ROBOTCTL_HOME_SETTLE_MS);
      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

static int robotctl_gait_power_safe(struct robotctl_servo_ctx_s *ctx,
                                    const struct robotctl_gait_s *gait,
                                    int steps, int period_ms)
{
  enum robotctl_servo_e servo;
  int interval_ms = robotctl_safe_interval(period_ms);
  int settle_ms = robotctl_safe_settle(period_ms);
  int ret;
  int cycle;
  int phase;
  int order;

  ret = robotctl_home_power_safe(ctx);
  if (ret < 0)
    {
      return ret;
    }

  for (cycle = 0; cycle < steps; cycle++)
    {
      for (phase = 0; phase < ROBOTCTL_PHASE_COUNT; phase++)
        {
          printf("robotctl: %s phase %d/%d\n", gait->name, phase + 1,
                 ROBOTCTL_PHASE_COUNT);

          for (order = 0; order < 2; order++)
            {
              servo = gait->phase[phase].order[order];
              if (ctx->angle[servo] == gait->phase[phase].target[servo])
                {
                  continue;
                }

              printf("robotctl: move %s %d -> %d\n",
                     robotctl_servo_name(servo), ctx->angle[servo],
                     gait->phase[phase].target[servo]);
              ret = robotctl_servo_move_one(
                ctx, servo, gait->phase[phase].target[servo], interval_ms,
                settle_ms);
              if (ret < 0)
                {
                  return ret;
                }
            }
        }
    }

  return robotctl_home_power_safe(ctx);
}

static int robotctl_gait_normal(struct robotctl_servo_ctx_s *ctx,
                                const struct robotctl_gait_s *gait,
                                int steps, int period_ms)
{
  int phase_ms = period_ms / ROBOTCTL_PHASE_COUNT;
  int ret;
  int cycle;
  int phase;

  ret = robotctl_servo_move_all(ctx, g_robotctl_home, 500);
  if (ret < 0)
    {
      return ret;
    }

  for (cycle = 0; cycle < steps; cycle++)
    {
      for (phase = 0; phase < ROBOTCTL_PHASE_COUNT; phase++)
        {
          printf("robotctl: %s phase %d/%d\n", gait->name, phase + 1,
                 ROBOTCTL_PHASE_COUNT);
          ret = robotctl_servo_move_all(ctx, gait->phase[phase].target,
                                        phase_ms);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  ret = robotctl_servo_move_all(ctx, g_robotctl_home, 500);
  if (ret < 0)
    {
      return ret;
    }

  return robotctl_servo_stop(ctx, false);
}

static int robotctl_parse_number(const char *text, int minimum, int maximum,
                                 int *result)
{
  char *end;
  long value;

  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || *end != '\0' || value < minimum || value > maximum)
    {
      return -EINVAL;
    }

  *result = (int)value;
  return 0;
}

static int robotctl_parse_motion_args(const struct robotctl_gait_s *gait,
                                      int argc, char *argv[], int *steps,
                                      int *period_ms)
{
  *steps = ROBOTCTL_DEFAULT_STEPS;
  *period_ms = gait == &g_left || gait == &g_right ?
               ROBOTCTL_DEFAULT_TURN_PERIOD : ROBOTCTL_DEFAULT_WALK_PERIOD;

  if (argc > 4)
    {
      return -EINVAL;
    }

  if (argc >= 3 &&
      robotctl_parse_number(argv[2], ROBOTCTL_MIN_STEPS,
                            ROBOTCTL_MAX_STEPS, steps) < 0)
    {
      return -EINVAL;
    }

  if (argc >= 4 &&
      robotctl_parse_number(argv[3], ROBOTCTL_MIN_PERIOD,
                            ROBOTCTL_MAX_PERIOD, period_ms) < 0)
    {
      return -EINVAL;
    }

  return 0;
}

static void robotctl_usage(void)
{
  printf("Usage: robotctl <command> [arguments]\n");
  printf("  help\n");
  printf("  status\n");
  printf("  home\n");
  printf("    Sequentially move servos to HOME pose.\n");
  printf("  off\n");
  printf("    Stop all servo PWM output; does not move to HOME.\n");
  printf("    Gear resistance can remain after PWM is stopped.\n");
  printf("  servo <lf|lr|rf|rr|tail> <angle>\n");
  printf("    Test one servo only; angle range is 0..180.\n");
  printf("  servo-off\n");
  printf("    Alias of off.\n");
  printf("  forward|backward|left|right [steps] [period_ms]\n");
  printf("    POWER_SAFE gait; steps 1..10, period_ms 800..3000.\n");
}

static void robotctl_status(void)
{
  printf("robotctl %s\n", ROBOTCTL_VERSION);
  printf("PWM device: %s\n", access(ROBOTCTL_PWM_PATH, F_OK) == 0 ?
         "available" : "unavailable");
  printf("I2C device: %s\n", access(ROBOTCTL_I2C_PATH, F_OK) == 0 ?
         "available" : "unavailable");
  printf("Motion mode: POWER_SAFE\n");
  printf("Max simultaneous servos with nonzero duty: 1\n");
  printf("PWM hardware state: not queryable across invocations\n");
  printf("OLED: not implemented\n");
}

static int robotctl_off(void)
{
  struct robotctl_servo_ctx_s ctx;
  int ret;

  ret = robotctl_servo_open(&ctx);
  if (ret < 0)
    {
      return ret;
    }

  ret = robotctl_servo_stop(&ctx, true);
  robotctl_servo_close(&ctx);
  return ret;
}

static int robotctl_home(void)
{
  struct robotctl_servo_ctx_s ctx;
  int ret;

  ret = robotctl_servo_open(&ctx);
  if (ret < 0)
    {
      return ret;
    }

  ret = robotctl_home_power_safe(&ctx);
  robotctl_servo_close(&ctx);
  return ret;
}

static int robotctl_single_servo(int argc, char *argv[])
{
  struct robotctl_servo_ctx_s ctx;
  int servo;
  int angle;
  int ret;

  if (argc != 4)
    {
      fprintf(stderr,
              "robotctl: servo requires <lf|lr|rf|rr|tail> <angle>\n");
      return -EINVAL;
    }

  servo = robotctl_servo_from_name(argv[2]);
  if (servo < 0 || robotctl_parse_number(argv[3], 0, 180, &angle) < 0)
    {
      fprintf(stderr, "robotctl: invalid servo name or angle 0..180\n");
      return -EINVAL;
    }

  ret = robotctl_servo_open(&ctx);
  if (ret < 0)
    {
      return ret;
    }

  printf("robotctl: move %s %d -> %d\n", robotctl_servo_name(servo),
         ctx.angle[servo], angle);
  ret = robotctl_servo_move_one(&ctx, servo, angle,
                                ROBOTCTL_HOME_INTERVAL_MS,
                                ROBOTCTL_HOME_SETTLE_MS);
  robotctl_servo_close(&ctx);
  return ret;
}

static int robotctl_motion(const struct robotctl_gait_s *gait,
                           int argc, char *argv[])
{
  struct robotctl_servo_ctx_s ctx;
  int period_ms;
  int steps;
  int ret;

  ret = robotctl_parse_motion_args(gait, argc, argv, &steps, &period_ms);
  if (ret < 0)
    {
      fprintf(stderr,
              "robotctl: motion requires steps 1..10 and period_ms "
              "800..3000\n");
      return ret;
    }

  ret = robotctl_servo_open(&ctx);
  if (ret < 0)
    {
      return ret;
    }

  if (g_motion_mode == ROBOTCTL_MOTION_POWER_SAFE)
    {
      ret = robotctl_gait_power_safe(&ctx, gait, steps, period_ms);
    }
  else
    {
      ret = robotctl_gait_normal(&ctx, gait, steps, period_ms);
    }

  robotctl_servo_close(&ctx);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  int ret;

  if (argc < 2 || strcmp(argv[1], "help") == 0)
    {
      robotctl_usage();
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      robotctl_status();
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "servo-off") == 0)
    {
      ret = robotctl_off();
    }
  else if (strcmp(argv[1], "home") == 0)
    {
      ret = robotctl_home();
    }
  else if (strcmp(argv[1], "servo") == 0)
    {
      ret = robotctl_single_servo(argc, argv);
    }
  else if (strcmp(argv[1], "forward") == 0)
    {
      ret = robotctl_motion(&g_forward, argc, argv);
    }
  else if (strcmp(argv[1], "backward") == 0)
    {
      ret = robotctl_motion(&g_backward, argc, argv);
    }
  else if (strcmp(argv[1], "left") == 0)
    {
      ret = robotctl_motion(&g_left, argc, argv);
    }
  else if (strcmp(argv[1], "right") == 0)
    {
      ret = robotctl_motion(&g_right, argc, argv);
    }
  else
    {
      fprintf(stderr, "robotctl: unknown command: %s\n", argv[1]);
      robotctl_usage();
      return EXIT_FAILURE;
    }

  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
