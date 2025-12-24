// Copyright 2024 EPFL
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1

#include <stdio.h>
#include <stdint.h>
#include "matrixMul8.h"
#include "x-heep.h"
#include "gpio.h"

/* By default, printfs are activated for FPGA and disabled for simulation. */

#define ENABLE_PRINTF           0
#define RETURN_ON_END           0
#define GPIO_TOGGLE_ON_ERROR    0
#define CHECK_ERRORS            0


#define GPIO_TOGGLE 0

#if ENABLE_PRINTF
    #define PRINTF(fmt, ...)    printf(fmt, ## __VA_ARGS__)
#else
    #define PRINTF(...)
#endif

int32_t m_c[SIZE*SIZE] = {0};
int32_t acc = 0;

int main()
{

    #if GPIO_TOGGLE_ON_ERROR

    gpio_result_t gpio_res;
    gpio_cfg_t pin_cfg = {
        .pin = GPIO_TOGGLE,
        .mode = GpioModeOutPushPull
    };
    gpio_config (pin_cfg);

    #endif


    while(1){

        for(int i = 0; i < SIZE; i++) {
            for(int j = 0; j < SIZE; j++) {
                acc = 0;
                for(int k = 0; k < SIZE; k++) {
                    acc+= m_a[i*SIZE+k] * m_b[k*SIZE+j];
                }
                m_c[i*SIZE+j] += acc;
            }

        }

        #if CHECK_ERRORS
            uint32_t errors = 0;
            for(int i = 0; i < SIZE*SIZE; i++){
                if( m_c[i] != m_exp[i] ) errors++;
                m_c[i] = 0; // Reset for the next iteration
            }
            if( errors == 0 ) {
                #if ENABLE_PRINTF
                    printf("\nok");
                #endif
                #if RETURN_ON_END
                    return 0;
                #endif
                #if GPIO_TOGGLE_ON_ERROR
                gpio_write(GPIO_TOGGLE, false);
                #endif
            }else{
                #if ENABLE_PRINTF
                printf("%d\n", errors);
                #endif
                #if RETURN_ON_END
                return 1;
                #endif
                #if GPIO_TOGGLE_ON_ERROR
                    gpio_write(GPIO_TOGGLE, true);
                    for(int i=0;i<5000000;i++) asm volatile("nop");
                #endif
            }
        #endif
    }

    return 1;
}
