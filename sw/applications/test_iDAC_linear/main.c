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

#define DAC_UPDATE_HZ 2
#define SYS_FCLK_HZ 100000000
#define DAC_UPDATE_CC (SYS_FCLK_HZ/DAC_UPDATE_HZ)


#define LAUNCH_TIMER()          \
    do {                              \
        timer_cycles_init();          \
        timer_irq_enable();           \
        timer_arm_start(DAC_UPDATE_CC); \
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
    uint32_t voltage_drop_A_mV = (current_A_nA*390)/1000;
    iDACs_set_currents( val, 255-val);
    printf("%d = A:%d.%d (ΔV:%d mV)| B: %d.%d uA\n", val, current_A_nA/1000, current_A_nA%1000, voltage_drop_A_mV, current_B_nA/1000, current_B_nA%1000);
}

int main() {

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


    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();

    printf("=== test iDAC linear ===\n");

    timer_cycles_init();
    timer_start();

    while(1){
        for( val = 0; val < 255; val++ ){
            update_dacs(val);
            LAUNCH_TIMER();
        }
        for( val = 255; val >= 0; val-- ){
            update_dacs(val);
            LAUNCH_TIMER();
        }
    }

    return 0;
}