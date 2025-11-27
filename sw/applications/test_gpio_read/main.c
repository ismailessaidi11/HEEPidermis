// Copyright EPFL contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include "core_v_mini_mcu.h"
#include "gpio.h"
#include "x-heep.h"
#include "pad_control.h"
#include "pad_control_regs.h"

#define GPIO_OUT 0
#define GPIO_IN  5

/* By default, printfs are activated for FPGA and disabled for simulation. */
#define PRINTF_ENABLE  1

#if PRINTF_ENABLE
    #define PRINTF(fmt, ...)    printf(fmt, ## __VA_ARGS__)
#else
    #define PRINTF(...)
#endif


int main(int argc, char *argv[])
{

    // Setup pad_control
    pad_control_t pad_control;
    pad_control.base_addr = mmio_region_from_addr((uintptr_t)PAD_CONTROL_START_ADDRESS);
    // Switch pad mux to GPIO_5 (muxed with dsm_in)
    pad_control_set_mux(&pad_control, (ptrdiff_t)PAD_CONTROL_PAD_MUX_DSM_IN_REG_OFFSET, 1);

    // Setup gpio
    gpio_result_t gpio_res;
    gpio_cfg_t pin_in = {
        .pin = GPIO_IN,
        .mode = GpioModeIn,
        .en_input_sampling = true,
        .en_intr = false,
        };
    gpio_res = gpio_config(pin_in);
    if (gpio_res != GpioOk) {
        return EXIT_FAILURE;
    }

    // Set output gpio
    gpio_cfg_t pin_out = { .pin = GPIO_OUT, .mode = GpioModeOutPushPull };
    gpio_config (pin_out);

    volatile static bool readval = 0;
    static bool oldval = 0;

    PRINTF("Hello!\n");

    while(1){
        gpio_read(GPIO_IN, &readval);
        gpio_write(GPIO_OUT, readval);
    }

    PRINTF("Failed.\n");
    return EXIT_FAILURE;
}
