/****************************************************************************
 * board/contest_board/src/board_oled.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>

#if defined(CONFIG_LCD_SSD1306) && defined(CONFIG_LCD_SSD1306_I2C) && \
    defined(CONFIG_ESP32S3_I2C0)

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/ssd1306.h>

#include "esp32s3_i2c.h"

static struct lcd_dev_s *g_oled;

int board_oled_initialize(void)
{
  struct i2c_master_s *i2c;

  if (g_oled != NULL)
    {
      return OK;
    }

  i2c = esp32s3_i2cbus_initialize(0);
  if (i2c == NULL)
    {
      return -ENODEV;
    }

  g_oled = ssd1306_initialize(i2c, NULL, 0);
  if (g_oled == NULL)
    {
      return -ENODEV;
    }

  return g_oled->setpower(g_oled, CONFIG_LCD_MAXPOWER);
}

struct lcd_dev_s *board_oled_getdev(void)
{
  return g_oled;
}

#else

int board_oled_initialize(void)
{
  return -ENODEV;
}

struct lcd_dev_s *board_oled_getdev(void)
{
  return NULL;
}

#endif
