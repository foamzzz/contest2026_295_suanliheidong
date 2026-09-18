/*
 * Contest-local six-state OLED face renderer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <stdbool.h>
#include <string.h>

#include "robot_oled_expr.h"

static void robot_oled_pixel(uint8_t frame[ROBOT_OLED_HEIGHT]
                                             [ROBOT_OLED_ROW_BYTES],
                             int x, int y)
{
  if (x < 0 || x >= ROBOT_OLED_WIDTH || y < 0 || y >= ROBOT_OLED_HEIGHT)
    {
      return;
    }

#ifdef CONFIG_LCD_PACKEDMSFIRST
  frame[y][x >> 3] |= (uint8_t)(0x80u >> (x & 7));
#else
  frame[y][x >> 3] |= (uint8_t)(1u << (x & 7));
#endif
}

static void robot_oled_line(uint8_t frame[ROBOT_OLED_HEIGHT]
                                            [ROBOT_OLED_ROW_BYTES],
                            int x0, int y0, int x1, int y1)
{
  int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
  int sx = x0 < x1 ? 1 : -1;
  int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
  int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  for (;;)
    {
      robot_oled_pixel(frame, x0, y0);
      if (x0 == x1 && y0 == y1)
        {
          break;
        }

      int twice = 2 * error;
      if (twice >= dy)
        {
          error += dy;
          x0 += sx;
        }
      if (twice <= dx)
        {
          error += dx;
          y0 += sy;
        }
    }
}

static void robot_oled_rect(uint8_t frame[ROBOT_OLED_HEIGHT]
                                            [ROBOT_OLED_ROW_BYTES],
                            int left, int top, int right, int bottom,
                            bool filled)
{
  int x;
  int y;

  if (filled)
    {
      for (y = top; y <= bottom; y++)
        {
          for (x = left; x <= right; x++)
            {
              robot_oled_pixel(frame, x, y);
            }
        }
    }
  else
    {
      for (x = left; x <= right; x++)
        {
          robot_oled_pixel(frame, x, top);
          robot_oled_pixel(frame, x, bottom);
        }
      for (y = top; y <= bottom; y++)
        {
          robot_oled_pixel(frame, left, y);
          robot_oled_pixel(frame, right, y);
        }
    }
}

static void robot_oled_eye(uint8_t frame[ROBOT_OLED_HEIGHT]
                                           [ROBOT_OLED_ROW_BYTES],
                           int center_x, int center_y, int pupil_x,
                           int pupil_y)
{
  robot_oled_rect(frame, center_x - 13, center_y - 9,
                  center_x + 13, center_y + 9, false);
  robot_oled_rect(frame, center_x + pupil_x - 3, center_y + pupil_y - 4,
                  center_x + pupil_x + 3, center_y + pupil_y + 4, true);
}

static void robot_oled_closed_eye(uint8_t frame[ROBOT_OLED_HEIGHT]
                                                  [ROBOT_OLED_ROW_BYTES],
                                  int center_x, int center_y)
{
  robot_oled_line(frame, center_x - 12, center_y + 2,
                  center_x - 5, center_y - 3);
  robot_oled_line(frame, center_x - 5, center_y - 3,
                  center_x + 5, center_y - 3);
  robot_oled_line(frame, center_x + 5, center_y - 3,
                  center_x + 12, center_y + 2);
}

static void robot_oled_smile(uint8_t frame[ROBOT_OLED_HEIGHT]
                                             [ROBOT_OLED_ROW_BYTES],
                             int center_y, bool open)
{
  robot_oled_line(frame, 48, center_y, 54, center_y + 5);
  robot_oled_line(frame, 54, center_y + 5, 64, center_y + 7);
  robot_oled_line(frame, 64, center_y + 7, 74, center_y + 5);
  robot_oled_line(frame, 74, center_y + 5, 80, center_y);
  if (open)
    {
      robot_oled_line(frame, 53, center_y + 4, 75, center_y + 4);
    }
}

static void robot_oled_frown(uint8_t frame[ROBOT_OLED_HEIGHT]
                                             [ROBOT_OLED_ROW_BYTES])
{
  robot_oled_line(frame, 48, 52, 54, 47);
  robot_oled_line(frame, 54, 47, 64, 45);
  robot_oled_line(frame, 64, 45, 74, 47);
  robot_oled_line(frame, 74, 47, 80, 52);
}

static void robot_oled_cross_eye(uint8_t frame[ROBOT_OLED_HEIGHT]
                                                [ROBOT_OLED_ROW_BYTES],
                                 int center_x, int center_y)
{
  robot_oled_line(frame, center_x - 9, center_y - 9,
                  center_x + 9, center_y + 9);
  robot_oled_line(frame, center_x + 9, center_y - 9,
                  center_x - 9, center_y + 9);
}

void robot_oled_render_expression(enum robot_expression_e expression,
                                  uint8_t frame[ROBOT_OLED_HEIGHT]
                                                [ROBOT_OLED_ROW_BYTES])
{
  memset(frame, 0, ROBOT_OLED_HEIGHT * ROBOT_OLED_ROW_BYTES);

  switch (expression)
    {
      case ROBOT_EXPRESSION_LISTENING:
        robot_oled_rect(frame, 14, 17, 42, 43, true);
        robot_oled_rect(frame, 86, 17, 114, 43, true);
        robot_oled_rect(frame, 22, 23, 34, 37, false);
        robot_oled_rect(frame, 94, 23, 106, 37, false);
        robot_oled_rect(frame, 56, 47, 72, 57, true);
        break;

      case ROBOT_EXPRESSION_THINKING:
        robot_oled_eye(frame, 28, 30, 5, -3);
        robot_oled_eye(frame, 100, 30, 5, -3);
        robot_oled_line(frame, 52, 51, 76, 51);
        robot_oled_rect(frame, 83, 7, 91, 15, false);
        robot_oled_rect(frame, 94, 2, 102, 8, false);
        break;

      case ROBOT_EXPRESSION_HAPPY:
        robot_oled_closed_eye(frame, 28, 29);
        robot_oled_closed_eye(frame, 100, 29);
        robot_oled_smile(frame, 45, true);
        break;

      case ROBOT_EXPRESSION_SPEAKING:
        robot_oled_eye(frame, 28, 29, 0, 0);
        robot_oled_eye(frame, 100, 29, 0, 0);
        robot_oled_rect(frame, 53, 43, 75, 57, true);
        robot_oled_line(frame, 56, 48, 72, 48);
        break;

      case ROBOT_EXPRESSION_ERROR:
        robot_oled_cross_eye(frame, 28, 30);
        robot_oled_cross_eye(frame, 100, 30);
        robot_oled_frown(frame);
        break;

      case ROBOT_EXPRESSION_IDLE:
      default:
        robot_oled_eye(frame, 28, 30, 0, 0);
        robot_oled_eye(frame, 100, 30, 0, 0);
        robot_oled_smile(frame, 47, false);
        break;
    }
}
