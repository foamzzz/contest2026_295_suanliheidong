/****************************************************************************
 * board/contest_board/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_CONTEST_BOARD_INCLUDE_BOARD_H
#define __BOARD_CONTEST_BOARD_INCLUDE_BOARD_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Module pin map ***********************************************************
 *
 * The hardware description numbers the module pins as follows.  This table
 * records physical facts only; it does not assign any peripheral function.
 *
 *   Pin  1: GND                  Pin 22: IO14
 *   Pin  2: 3V3                  Pin 23: IO21
 *   Pin  3: EN                   Pin 24: IO47
 *   Pin  4: IO4                  Pin 25: IO48
 *   Pin  5: IO5                  Pin 26: IO45
 *   Pin  6: IO6                  Pin 27: IO0 (strapping)
 *   Pin  7: IO7                  Pin 28: IO35
 *   Pin  8: IO15                 Pin 29: IO36
 *   Pin  9: IO16                 Pin 30: IO37
 *   Pin 10: IO17                 Pin 31: IO38
 *   Pin 11: IO18                 Pin 32: IO39
 *   Pin 12: IO8                  Pin 33: IO40
 *   Pin 13: IO19                 Pin 34: IO41
 *   Pin 14: IO20                 Pin 35: IO42
 *   Pin 15: IO3                  Pin 36: RXD0 (chip default GPIO44)
 *   Pin 16: IO46                 Pin 37: TXD0 (chip default GPIO43)
 *   Pin 17: IO9                  Pin 38: IO2
 *   Pin 18: IO10                 Pin 39: IO1
 *   Pin 19: IO11                 Pin 40: GND
 *   Pin 20: IO12                 Pin 41: GND/EPAD
 *   Pin 21: IO13
 *
 * GND, 3V3, EN, and EPAD are not software GPIOs.  IO0 remains reserved as a
 * boot-strapping pin.  UART0 uses the ESP32-S3 Kconfig/IOMUX defaults and is
 * intentionally not overridden here.
 */

/* Clocking *****************************************************************/

/* The local ESP32-S3 board support uses a 40 MHz main crystal. */

#define BOARD_XTAL_FREQUENCY    40000000

#ifdef CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ
#  define BOARD_CLOCK_FREQUENCY \
    (CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ * 1000000)
#else
#  define BOARD_CLOCK_FREQUENCY 80000000
#endif

#endif /* __BOARD_CONTEST_BOARD_INCLUDE_BOARD_H */
