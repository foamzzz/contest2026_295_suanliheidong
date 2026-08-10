/****************************************************************************
 * board/contest_board/src/board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/board.h>

#ifdef CONFIG_INPUT_BUTTONS_LOWER
#  include <nuttx/input/buttons.h>
#endif

#ifdef CONFIG_ESP32S3_LEDC
#  include "esp32s3_board_ledc.h"
#endif

#ifdef CONFIG_ESP32S3_I2S
#  include "esp32s3_i2s.h"
#endif

#include <arch/board/board.h>

#include "esp32s3_gpio.h"

#include "contest_board.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: contest_board_bringup
 ****************************************************************************/

int contest_board_bringup(void)
{
  int ret = 0;

#ifdef CONFIG_ARCH_CHIP_ESP32S3
  /* Initialize the verified board LED as a safe, low-level GPIO output.
   * The electrical LED polarity is intentionally not assumed here.
   */

  esp32s3_configgpio(LED_GPIO_PIN, OUTPUT_FUNCTION_2);
  esp32s3_gpiowrite(LED_GPIO_PIN, 0);
#endif

#ifdef CONFIG_INPUT_BUTTONS_LOWER
  ret = btn_lower_initialize("/dev/buttons");
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_I2C_DRIVER
  ret = board_i2c_init();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_ESP32S3_LEDC
  ret = esp32s3_pwm_setup();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_ESP32S3_I2S0
  if (esp32s3_i2sbus_initialize(ESP32S3_I2S0) == NULL)
    {
      return -ENODEV;
    }
#endif

#ifdef CONFIG_ESP32S3_I2S1
  if (esp32s3_i2sbus_initialize(ESP32S3_I2S1) == NULL)
    {
      return -ENODEV;
    }
#endif

  return 0;
}
