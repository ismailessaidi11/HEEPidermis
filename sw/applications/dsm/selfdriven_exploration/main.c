// Copyright EPFL contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "core_v_mini_mcu.h"
#include "gpio.h"
#include "x-heep.h"
#include "timer_sdk.h"
#include "soc_ctrl.h"
#include "pad_control.h"
#include "pad_control_regs.h"
#include "SES_filter.h"


#define SYS_FCLK_HZ         10000000

#define GPIO_DATA           2
#define GPIO_CLK            3
#define GPIO_LED            0

#define SES_WG              16
#define SES_WINDOW_SIZE     6
#define SES_DECIM_FACTOR    32

#define SES_SYSCLK_DIVISION 512  // ⚠️ NEEDS TO BE < 1024
#define DSM_F_S             (SYS_FCLK_HZ/SES_SYSCLK_DIVISION)
#define SES_ACTIVATED_STAGES 0b001111


#define GRAPH_POINTS        50
#define GRAPH_POINT_WIDTH   ((1<<SES_WG) -1)/GRAPH_POINTS

#define SAMPLE_LENGHT_N     2200

// #define PLOT_GRAPH               // ⚠️ BE CAREFUL! YOU WILL INTRODUCE ALIASING!
#define PRINT_DURING_SAMPLE      // ⚠️ BE CAREFUL! YOU WILL INTRODUCE ALIASING!
// #define PRINT_AT_END

#define HEADER(g, w, f, a) printf("\nfclk:%d Hz, Wg:%d,Ww:%d,DF:%d,AS:%d",DSM_F_S,g,w,f,a );


uint8_t             graph   [GRAPH_POINTS];
volatile uint32_t   bit_idx = 0;

uint8_t bits[] = {
    1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1,
    1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1,
    1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0,
    1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1,
    0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0,
    1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0
};

void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_arm_stop();
    timer_irq_clear();

    gpio_write(GPIO_DATA,   bits[bit_idx]);
    bit_idx = (bit_idx + 1) % sizeof(bits);

    timer_cycles_init();
    timer_irq_enable();
    timer_arm_start(SES_SYSCLK_DIVISION);
    return;
}

int main(int argc, char *argv[])
{
    static uint16_t ses_output [SAMPLE_LENGHT_N];
    uint32_t        sample_idx  = 0;
    uint32_t        output      = 0;
    uint8_t         point, last_point;

    /* ====================================
       CONFIGURE THE TIMERS
       ==================================== */
    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl,SYS_FCLK_HZ);
    timer_cycles_init();         // Init the timer SDK for clock cycles
    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();
    timer_cycles_init();
    timer_start();

    /* ====================================
       CONFIGURE THE PAD MUXES
       ==================================== */
    pad_control_t pad_control;
    pad_control.base_addr = mmio_region_from_addr((uintptr_t)PAD_CONTROL_START_ADDRESS);
    pad_control_set_mux(&pad_control, (ptrdiff_t)PAD_CONTROL_PAD_MUX_LC_XING_REG_OFFSET,   1);
    pad_control_set_mux(&pad_control, (ptrdiff_t)PAD_CONTROL_PAD_MUX_LC_DIR_REG_OFFSET,     1);

    /* ====================================
       CONFIGURE THE GPIOs
       ==================================== */
    gpio_result_t gpio_res;
    gpio_cfg_t pin_clk = { .pin = GPIO_CLK, .mode = GpioModeIn };
    if (gpio_config (pin_clk) != GpioOk) printf("Gpio initialization failed!\n");
    gpio_cfg_t pin_data = { .pin = GPIO_DATA, .mode = GpioModeOutPushPull };
    if (gpio_config (pin_data) != GpioOk) printf("Gpio initialization failed!\n");
    gpio_cfg_t pin_led = { .pin = GPIO_LED, .mode = GpioModeOutPushPull };
    if (gpio_config (pin_led) != GpioOk) printf("Gpio initialization failed!\n");

    /* ====================================
       LAUNCH THE DATA MODULATION
       ==================================== */

    for( uint8_t i =0; i < sizeof(graph); i++ ) graph[i] = ' ';
    graph[sizeof(graph)-1] = 0;

    timer_cycles_init();
    timer_irq_enable();
    timer_arm_start(SES_SYSCLK_DIVISION);


    uint8_t wgs[] = { 16 };         // Gain of the first stage
    uint8_t ass[] = { 1, 3, 7, 15, 31, 63 };       // Mask of activated stages
    uint8_t wws[] = { 6 };            // Window lenght (2^x) samples
    uint8_t dfs[] = { 2 };    // Decimation factor

    for( uint8_t g=0; g<sizeof(wgs); g++ ){
        for( uint8_t a=0; a<sizeof(ass); a++ ){
            for( uint8_t w=0; w<sizeof(wws); w++ ){
                for( uint8_t f=0; f<sizeof(dfs); f++ ){

                    /* ====================================
                    CONFIGURE THE SES FILTER
                    ==================================== */
                    SES_set_control_reg(false);

                    SES_set_gain(0, wgs[g]);
                    SES_set_gain(1, 0);
                    SES_set_gain(2, 0);
                    SES_set_gain(3, 0);
                    SES_set_gain(4, 0);
                    SES_set_gain(5, 0);

                    // Set SES filter parameters
                    SES_set_window_size(wws[w]);
                    SES_set_decim_factor(dfs[f]);
                    SES_set_activated_stages(ass[a]);
                    SES_set_sysclk_division(SES_SYSCLK_DIVISION);

                    // Start the SES filter
                    SES_set_control_reg(true);

                    // Wait for the SES filter to be ready
                    uint32_t status;
                    do{ status = SES_get_status();
                    } while (status != 0b11);


                    /* ====================================
                    ACQUIRE DATA
                    ==================================== */

                    #ifdef PRINT_DURING_SAMPLE
                    HEADER(wgs[g], wws[w], dfs[f], ass[a]);
                    #endif

                    sample_idx = 0;
                    while(1){

                        if( SES_get_status() == 3 ){
                            output = SES_get_filtered_output();
                            #ifdef PRINT_DURING_SAMPLE
                            printf("\n%d\t%d", sample_idx, ses_output[sample_idx]);
                            #endif
                            #ifdef PLOT_GRAPH
                            graph[last_point]   = ' ';
                            graph[last_point+1] = ' ';
                            point = output/1000;
                            graph[point]        = '*';
                            graph[point+1]      = 0;
                            last_point = point;
                            printf("\n%s", graph);
                            #endif
                            #ifdef PRINT_AT_END
                            ses_output[sample_idx] = output;
                            if(sample_idx == SAMPLE_LENGHT_N){
                                SES_set_control_reg(false);
                                break;
                            }
                            #endif
                            sample_idx++;
                        }
                    }

                    #ifdef PRINT_AT_END
                    HEADER(wgs[g], wws[w], dfs[f], ass[a]);
                    for( sample_idx =0; sample_idx < SAMPLE_LENGHT_N; sample_idx++ ){
                        printf("\n%d\t%d", sample_idx, ses_output[sample_idx]);
                    }
                    #endif


                }
            }
        }
    }




    return EXIT_SUCCESS;
}
