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


#define DSM_CLK_F_HZ 1
#define SYS_FCLK_HZ 10000000
// #define DSM_CLK_TOGGLE_CC (SYS_FCLK_HZ/(2*DSM_CLK_F_HZ))
#define DSM_CLK_TOGGLE_CC 500

#define GPIO_DATA 2
#define GPIO_CLK 3
#define GPIO_LED 0

#define SES_WG              16
#define SES_WINDOW_SIZE     8
#define SES_WO              (SES_WG-SES_WINDOW_SIZE)
#define SES_DECIM_FACTOR    32

// #define SES_SYSCLK_DIVISION (SYS_FCLK_HZ/DSM_CLK_F_HZ)
#define SES_SYSCLK_DIVISION (DSM_CLK_TOGGLE_CC*2)
#define SES_ACTIVATED_STAGES 0b001111


#define GRAPH_POINTS        50
#define GRAPH_POINT_WIDTH   ((1<<SES_WG) -1)/GRAPH_POINTS


#define SES_GAIN_STAGE_0 SES_WG
#define SES_GAIN_STAGE_1 0
#define SES_GAIN_STAGE_2 0
#define SES_GAIN_STAGE_3 0
#define SES_GAIN_STAGE_4 0
#define SES_GAIN_STAGE_5 0

#define SAMPLE_LENGHT_N 1000

// #define PLOT_GRAPH

/* By default, printfs are activated for FPGA and disabled for simulation. */
#define PRINTF_IN_FPGA  1
#define PRINTF_IN_SIM   0

#if TARGET_SIM && PRINTF_IN_SIM
        #define PRINTF(fmt, ...)    printf(fmt, ## __VA_ARGS__)
#elif PRINTF_IN_FPGA && !TARGET_SIM
    #define PRINTF(fmt, ...)    printf(fmt, ## __VA_ARGS__)
    #else
    #define PRINTF(...)
    #endif

uint8_t graph[GRAPH_POINTS];
volatile uint32_t    tick_idx    = 0;
volatile bool        state       = 0;
volatile uint32_t    bit_idx     = 0;

uint8_t bits[] = {
    1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, \
0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, \
1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, \
0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, \
1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, \
1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, \
0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, \
0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, \
0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, \
1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, \
0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, \
0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, \
1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, \
1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, \
1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, \
1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, \
1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, \
1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, \
1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, \
1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, \
1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, \
1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, \
1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, \
1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, \
1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, \
1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, \
1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, \
1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, \
1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, \
1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, \
0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, \
1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, \
1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 1, \
1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, \
0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, \
1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, \
1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, \
1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, \
1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, \
0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, \
0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, \
0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, \
1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, \
1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, \
0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, \
1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, \
0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, \
1, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, \
0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, \
0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, \
0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, \
1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, \
0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, \
0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, \
0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, \
0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, \
0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, \
0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, \
0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, \
0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, \
0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, \
0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, \
0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, \
0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, \
0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, \
0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, \
0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, \
0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, \
1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, \
0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, \
0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, \
0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, \
0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, \
0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, \
0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, \
1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, \
0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, \
1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, \
1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, \
1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, \
1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, \
0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, \
0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, \
0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, \
1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, \
0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, \
1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, \
1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, \
1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, \
0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, \
1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, \
1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, \
1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, \
};

void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_arm_stop();
    timer_irq_clear();

    if( state ){
        gpio_write(GPIO_DATA,   bits[bit_idx]);
        // gpio_write(GPIO_LED,    bits[bit_idx]);
        bit_idx = (bit_idx + 1) % sizeof(bits);
    }
    // gpio_write(GPIO_CLK, state);
    state = ~state;

    tick_idx ++;
    timer_cycles_init();
    timer_irq_enable();
    timer_arm_start(DSM_CLK_TOGGLE_CC);
    return;
}

bool detect_rising_edge(uint32_t status, uint32_t mask) {
    static bool prev_val = false;
    bool current_val = (status & mask) != 0;

    bool rising_edge = (!prev_val && current_val);
    prev_val = current_val;
    return rising_edge;
}


int main(int argc, char *argv[])
{

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
    if (gpio_config (pin_clk) != GpioOk) PRINTF("Gpio initialization failed!\n");
    gpio_cfg_t pin_data = { .pin = GPIO_DATA, .mode = GpioModeOutPushPull };
    if (gpio_config (pin_data) != GpioOk) PRINTF("Gpio initialization failed!\n");
    gpio_cfg_t pin_led = { .pin = GPIO_LED, .mode = GpioModeOutPushPull };
    if (gpio_config (pin_led) != GpioOk) PRINTF("Gpio initialization failed!\n");

    /* ====================================
       CONFIGURE THE SES FILTER
       ==================================== */
    SES_set_control_reg(false);
    printf("\n---\n%d", SES_get_status());


    // Set SES filter parameters
    SES_set_window_size(SES_WINDOW_SIZE);
    SES_set_decim_factor(SES_DECIM_FACTOR);
    SES_set_sysclk_division(SES_SYSCLK_DIVISION);
    SES_set_activated_stages(SES_ACTIVATED_STAGES);

    SES_set_gain(0, SES_GAIN_STAGE_0);
    SES_set_gain(1, SES_GAIN_STAGE_1);
    SES_set_gain(2, SES_GAIN_STAGE_2);
    SES_set_gain(3, SES_GAIN_STAGE_3);
    SES_set_gain(4, SES_GAIN_STAGE_4);
    SES_set_gain(5, SES_GAIN_STAGE_5);


    // Start the SES filter
    SES_set_control_reg(true);

    // Wait for the SES filter to be ready
    uint32_t status;
    do{ status = SES_get_status();
    } while (status != 0b11);


    uint32_t ses_output [SAMPLE_LENGHT_N];

    /* ====================================
       MAIN LOOP: toggle the clk and data
       ==================================== */
    printf("\nhere we go!\n");
    printf("fclk:%d Hz, Wg:%d,Ww:%d,DF:%d,AS:%d(%d,%d,%d,%d,%d,%d)",DSM_CLK_F_HZ,SES_WG,SES_WINDOW_SIZE,SES_DECIM_FACTOR,SES_ACTIVATED_STAGES,SES_GAIN_STAGE_0,SES_GAIN_STAGE_1,SES_GAIN_STAGE_2,SES_GAIN_STAGE_3,SES_GAIN_STAGE_4,SES_GAIN_STAGE_5 );

    uint32_t    sample_idx  = 0;

    uint8_t point, last_point;
    int32_t output = 0;

    for( uint8_t i =0; i < sizeof(graph); i++ ){
        graph[i] = ' ';
    }
    graph[64] = 0;

    timer_cycles_init();
    timer_irq_enable();
    timer_arm_start(DSM_CLK_TOGGLE_CC);

    uint32_t i=0;

    while(1){

        // // if( tick_idx%SES_DECIM_FACTOR == 0 ){
        status = SES_get_status();
        // printf("\n%d\t", status);
        ses_output[sample_idx] = SES_get_filtered_output();
        printf("\n%d\t%d", i,ses_output[sample_idx]);
        // if(status == 3) printf("<=========");
        i++;

        if(i == 1000){
            SES_set_control_reg(false);
            break;
        }



        // if (detect_rising_edge(status, 0b10)) {

        //     sample_idx = tick_idx/SES_DECIM_FACTOR;

        //     #ifdef PLOT_GRAPH
        //     graph[last_point]   = ' ';
        //     graph[last_point+1] = ' ';
        //     point = ses_output[sample_idx]/500;
        //     graph[point]        = '*';
        //     graph[point+1]      = 0;
        //     last_point = point;
        //     // printf("\n%d %d %d", point, ses_output[sample_idx], GRAPH_POINT_WIDTH);
        //     printf("\n%s", graph);
        //     #else
        //     printf("\n%d\t%d", sample_idx, ses_output[sample_idx]);
        //     #endif

        //     if( sample_idx >= SAMPLE_LENGHT_N ){
        //         SES_set_control_reg(false);
        //         break;
        //     }
        // }
    }

    // for( sample_idx =0; sample_idx < SAMPLE_LENGHT_N; sample_idx++ ){
    //     printf("\n%d, %d", sample_idx, ses_output[sample_idx]);
    // }

    PRINTF("Failed.\n");
    return EXIT_FAILURE;
}
