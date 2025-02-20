/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Glenn Ruben Bakke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __NRF52_HAL
#define __NRF52_HAL

#include "py/mpconfig.h"
#include NRF5_HAL_H
#include "pin.h"
#include "hal_gpio.h"

#include "lib/oofatfs/ff.h"

typedef enum
{
  HAL_OK       = 0x00,
  HAL_ERROR    = 0x01,
  HAL_BUSY     = 0x02,
  HAL_TIMEOUT  = 0x03
} HAL_StatusTypeDef;

extern FIL* boot_output_file;

static inline uint32_t hal_tick_fake(void) {
	return 0;
}

#define mp_hal_ticks_ms hal_tick_fake // TODO: implement. Right now, return 0 always

extern const unsigned char mp_hal_status_to_errno_table[4];

NORETURN void mp_hal_raise(HAL_StatusTypeDef status);
void mp_hal_set_interrupt_char(int c); // -1 to disable

int mp_hal_stdin_rx_chr(void);
void mp_hal_stdout_tx_str(const char *str);
bool mp_hal_stdin_any(void);

#define mp_hal_pin_obj_t const pin_obj_t*
#define mp_hal_get_pin_obj(o)    pin_find(o)
#define mp_hal_pin_high(p)       hal_gpio_pin_high(p)
#define mp_hal_pin_low(p)        hal_gpio_pin_low(p)
#define mp_hal_pin_read(p)       hal_gpio_pin_read(p)
#define mp_hal_pin_write(p, v)   do { if (v) { mp_hal_pin_high(p); } else { mp_hal_pin_low(p); } } while (0)
#define mp_hal_pin_od_low(p)     mp_hal_pin_low(p)
#define mp_hal_pin_od_high(p)    mp_hal_pin_high(p)
#define mp_hal_pin_open_drain(p) hal_gpio_cfg_pin(p->port, p->pin, HAL_GPIO_MODE_INPUT, HAL_GPIO_PULL_DISABLED)


// TODO: empty implementation for now. Used by machine_spi.c:69
#define mp_hal_delay_us_fast(p)
#define mp_hal_ticks_us() (0)
#define mp_hal_ticks_cpu() (0)

#endif
