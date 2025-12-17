// Copyright 2025 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// Author: Juan Sapriza
// Description: Test application for the iDACs

#include "timer_sdk.h"
#include "soc_ctrl.h"
#include "iDAC_ctrl.h"
#include "VCO_decoder.h"
#include "cheep.h"
#include "pad_control.h"
#include "pad_control_regs.h"
#include "REFs_ctrl.h"
#include "gpio.h"

#define COOL_DOWN_MSEC 250
#define SYS_FCLK_HZ 10000000
#define COOL_DOWN_CC ((SYS_FCLK_HZ/1000)*COOL_DOWN_MSEC)

#define GPIO_DSM_IN 5
#define GPIO_DSM_CLK 4
#define SHUNT_RES_OHM 55600

#define LAUNCH_TIMER()          \
    do {                              \
        timer_cycles_init();          \
        timer_irq_enable();           \
        timer_arm_start(COOL_DOWN_CC); \
        asm volatile ("wfi");         \
        timer_irq_clear();            \
    } while (0)


void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_arm_stop();
    timer_irq_clear();
    return;
}

void update_dacs(val){
    uint32_t current_A_nA = 40*val;
    uint32_t current_B_nA = 40*(255-val);
    uint32_t voltage_drop_A_mV = (current_A_nA*SHUNT_RES_OHM)/1000000;
    iDACs_set_currents( val, 255-val);
    printf("%d = A:%d.%d (ΔV:%d mV)| B: %d.%d uA\n", val, current_A_nA/1000, current_A_nA%1000, voltage_drop_A_mV, current_B_nA/1000, current_B_nA%1000);
}

int main() {

    // Setup pad_control
    pad_control_t pad_control;
    pad_control.base_addr = mmio_region_from_addr((uintptr_t)PAD_CONTROL_START_ADDRESS);
    // Switch pad mux to GPIO_5 (muxed with dsm_in)
    pad_control_set_mux(&pad_control, (ptrdiff_t)PAD_CONTROL_PAD_MUX_DSM_IN_REG_OFFSET, 1);
    pad_control_set_mux(&pad_control, (ptrdiff_t)PAD_CONTROL_PAD_MUX_DSM_CLK_REG_OFFSET, 1);

    // Setup gpio
    gpio_result_t gpio_res;
    gpio_cfg_t pin_in = {
        .pin = GPIO_DSM_IN,
        .mode = GpioModeIn,
        .en_input_sampling = true,
        .en_intr = false,
        };
    gpio_res = gpio_config(pin_in);
    if (gpio_res != GpioOk) {
        return 1;
    }

    gpio_cfg_t pin_clk = {
        .pin = GPIO_DSM_CLK,
        .mode = GpioModeIn,
        .en_input_sampling = true,
        .en_intr = false,
        };
    gpio_res = gpio_config(pin_clk);
    if (gpio_res != GpioOk) {
        return 1;
    }



    static uint32_t val = 0;

    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl,SYS_FCLK_HZ);

    timer_cycles_init();         // Init the timer SDK for clock cycles

    iDACs_enable(true, true);
    VCOp_enable(true);
    VCOn_enable(true);

    // Set the calibration values
    iDAC1_calibrate(16);
    iDAC2_calibrate(16);

    REFs_calibrate( 0, IREF1 );

    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();

    printf("here we go!\n");

    timer_cycles_init();
    timer_start();

    uint8_t gpio_high;

    while(1){

        gpio_read(GPIO_DSM_IN, &gpio_high);
        if(gpio_high){
            val++;
            printf("UP\n");
            update_dacs(val);
            LAUNCH_TIMER();
        }
        gpio_read(GPIO_DSM_CLK, &gpio_high);
        if(gpio_high){
            val--;
            printf("DN\n");
            update_dacs(val);
            LAUNCH_TIMER();
        }
    }

    return 0;
}