// Copyright 2024 EPFL
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1

#include <stdio.h>
#include <stdint.h>
#include "matrixMul8.h"
#include "x-heep.h"

/* By default, printfs are activated for FPGA and disabled for simulation. */
#define ENABLE_PRINTF  1

#if ENABLE_PRINTF
    #define PRINTF(fmt, ...)    printf(fmt, ## __VA_ARGS__)
#else
    #define PRINTF(...)
#endif

#define REFERENCE_CLOCK_Hz 15*1000*1000
#define UART_BAUDRATE 9600


int32_t m_c[SIZE*SIZE];
uint32_t round = 0;

int main()
{
    while(1){
        for(int i = 0; i < SIZE; i++) {
            for(int j = 0; j < SIZE; j++) {
                int32_t acc = 0;
                for(int k = 0; k < SIZE; k++) {
                    acc+= m_a[i*SIZE+k] * m_b[k*SIZE+j];
                }
                m_c[i*SIZE+j] += acc;
            }
        }

        round++;
        printf("%d\n\r",round);
    }

    return 1;
}
