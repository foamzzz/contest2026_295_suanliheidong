/*
 * Contest-local robot_move Tool Provider.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <netutils/cJSON.h>

#include "tools/tool_registry.h"
#include "robot_action_guard.h"
#include "robot_motion.h"
#include "robot_motion_tool.h"
#include "robot_world_state.h"

#define ROBOT_MOTION_TOOL_NAME "robot_move"

static bool g_robot_motion_tool_registered;

static unsigned long long robot_motion_tool_now_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (unsigned long long)ts.tv_sec * 1000ULL +
         (unsigned long long)ts.tv_nsec / 1000000ULL;
}

static char *robot_motion_tool_get_tools(void)
{
  static const char tools_json[] =
    "[{\"name\":\"robot_move\","
    "\"description\":\"" ROBOT_SIDE_EFFECT_TOOL_POLICY
    "Physical movement tool for the robot dog. Use only for explicit physical "
    "requests to move forward, backward, turn left, or turn right. Do not use "
    "it for figurative language, and never invent raw servo angles. The tool "
    "waits for one POWER_SAFE motion request to complete; a busy robot rejects "
    "the request.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"direction\":{\"type\":\"string\",\"enum\":[\"forward\","
    "\"backward\",\"left\",\"right\"]},"
    "\"steps\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1},"
    "\"period_ms\":{\"type\":\"integer\",\"minimum\":800,"
    "\"maximum\":3000}},\"required\":[\"direction\"]}}]";

  printf("[ROBOT-MOTION-TOOL] tools_json requested\n");
  return strdup(tools_json);
}

static int robot_motion_tool_number(cJSON *item, unsigned int minimum,
                                    unsigned int maximum,
                                    unsigned int *value)
{
  double number;

  if (item == NULL || !cJSON_IsNumber(item) || value == NULL)
    {
      return -EINVAL;
    }

  number = item->valuedouble;
  if (number < minimum || number > maximum || number != (double)(int)number)
    {
      return -EINVAL;
    }

  *value = (unsigned int)number;
  return 0;
}

static int robot_motion_tool_execute(const char *name, const char *input_json,
                                     char *output, size_t output_size)
{
  cJSON *root;
  cJSON *direction_item;
  cJSON *steps_item;
  cJSON *period_item;
  const char *direction_name;
  enum robot_motion_direction_e direction;
  unsigned int steps = ROBOT_MOTION_DEFAULT_STEPS;
  unsigned int period_ms;
  int ret;

  if (name == NULL || input_json == NULL || output == NULL ||
      output_size == 0 || strcmp(name, ROBOT_MOTION_TOOL_NAME) != 0)
    {
      return ERROR;
    }

  printf("[POWER-SAFE] mono_ms=%llu MOVE_TOOL_ENTER\n",
         robot_motion_tool_now_ms());
  printf("[ROBOT-MOTION-TOOL] execute input=%s\n", input_json);
  root = cJSON_Parse(input_json);
  if (root == NULL)
    {
      snprintf(output, output_size,
               "{\"ok\":false,\"error\":\"invalid json\"}");
      printf("[POWER-SAFE] mono_ms=%llu MOVE_TOOL_RETURN rc=%d\n",
             robot_motion_tool_now_ms(), -EINVAL);
      return ERROR;
    }

  direction_item = cJSON_GetObjectItem(root, "direction");
  direction_name = cJSON_GetStringValue(direction_item);
  ret = robot_motion_direction_from_name(direction_name, &direction);
  if (ret < 0)
    {
      cJSON_Delete(root);
      snprintf(output, output_size,
               "{\"ok\":false,\"error\":\"invalid direction\"}");
      printf("[POWER-SAFE] mono_ms=%llu MOVE_TOOL_RETURN rc=%d\n",
             robot_motion_tool_now_ms(), -EINVAL);
      return ERROR;
    }

  period_ms = robot_motion_default_period(direction);
  steps_item = cJSON_GetObjectItem(root, "steps");
  if (steps_item != NULL &&
      robot_motion_tool_number(steps_item, ROBOT_MOTION_MIN_STEPS, 1,
                               &steps) < 0)
    {
      cJSON_Delete(root);
      snprintf(output, output_size,
               "{\"ok\":false,\"error\":\"steps must be 1\"}");
      printf("[POWER-SAFE] mono_ms=%llu MOVE_TOOL_RETURN rc=%d\n",
             robot_motion_tool_now_ms(), -EINVAL);
      return ERROR;
    }

  period_item = cJSON_GetObjectItem(root, "period_ms");
  if (period_item != NULL &&
      robot_motion_tool_number(period_item, ROBOT_MOTION_MIN_PERIOD,
                               ROBOT_MOTION_MAX_PERIOD, &period_ms) < 0)
    {
      cJSON_Delete(root);
      snprintf(output, output_size,
               "{\"ok\":false,\"error\":\"period_ms must be 800..3000\"}");
      printf("[POWER-SAFE] mono_ms=%llu MOVE_TOOL_RETURN rc=%d\n",
             robot_motion_tool_now_ms(), -EINVAL);
      return ERROR;
    }

  printf("[ROBOT-MOTION-TOOL] power-safe synchronous execution\n");
  printf("[ROBOT-MOTION-TOOL] direction=%s steps=%u period_ms=%u\n",
         direction_name, steps, period_ms);
  direction_name = robot_motion_direction_name(direction);
  cJSON_Delete(root);
  robot_action_guard_begin(ROBOT_ACTION_MOTION, ROBOT_MOTION_TOOL_NAME);
  robot_world_state_set_motion_active(true);
  ret = robot_motion_submit_and_wait(
    ROBOT_MOTION_SOURCE_AGENT, direction, steps, period_ms,
    robot_motion_default_timeout(steps, period_ms), NULL);
  robot_world_state_set_motion_active(false);
  robot_action_guard_end(ROBOT_ACTION_MOTION, ROBOT_MOTION_TOOL_NAME,
                         ret);

  if (ret < 0)
    {
      if (ret == -EBUSY)
        {
          snprintf(output, output_size,
                   "{\"ok\":false,\"error\":\"motion_busy\"}");
        }
      else
        {
          snprintf(output, output_size,
                   "{\"ok\":false,\"error\":\"motion unavailable\","
                   "\"rc\":%d}", ret);
        }
      printf("[POWER-SAFE] mono_ms=%llu MOVE_TOOL_RETURN rc=%d\n",
             robot_motion_tool_now_ms(), ret);
      return ERROR;
    }

  printf("[POWER-SAFE] mono_ms=%llu MOVE_TOOL_RETURN rc=%d\n",
         robot_motion_tool_now_ms(), ret);
  printf("[ROBOT-MOTION-TOOL] completed rc=%d\n", ret);
  snprintf(output, output_size,
           "{\"ok\":true,\"completed\":true,\"direction\":\"%s\","
           "\"steps\":%u,\"period_ms\":%u}", direction_name, steps,
           period_ms);
  return OK;
}

int robot_motion_register_tool(void)
{
  if (g_robot_motion_tool_registered)
    {
      return 0;
    }

  tool_registry_register_provider("robot_motion",
                                  robot_motion_tool_get_tools,
                                  robot_motion_tool_execute);
  tool_registry_invalidate();
  g_robot_motion_tool_registered = true;
  printf("[ROBOT-MOTION-TOOL] provider registered\n");
  return 0;
}
