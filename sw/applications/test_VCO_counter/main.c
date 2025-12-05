// Copyright 2025 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// Author: David Mallasen
// Description: Test application for the VCO counter in the VCO decoder

#include "VCO_decoder.h"
#include "timer_sdk.h"
#include "soc_ctrl.h"

#define VCO_FS_HZ 1
#define SYS_FCLK_HZ 1000000
#define VCO_UPDATE_CC (SYS_FCLK_HZ/VCO_FS_HZ)

uint8_t graph[50];

void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_arm_stop();
    timer_irq_clear();
    return;
}

int main() {

    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl,SYS_FCLK_HZ);

    timer_cycles_init();         // Init the timer SDK for clock cycles

    // Enable the VCOp and VCOn
    VCOp_enable(true);
    VCOn_enable(true);

    // Set the VCO refresh rate to 1000 cycles
    VCO_set_refresh_rate(VCO_UPDATE_CC);

    int i=0;
    uint32_t diff, last_diff, new_val, last_val = 0;
    VCO_set_counter_limit(VCO_UPDATE_CC);


    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();

    printf("here we go!\n");

    timer_cycles_init();
    timer_start();

    while(1){
        new_val = VCOp_get_coarse();
        diff =  new_val - last_val;
        printf("\n%d.%d kHz", diff/1000, diff%1000);
        // printf("\n%d kHz", diff/1000);
        printf("  | %d", new_val);

        // if(diff > 0 && diff < 1000000){
        //     graph[(last_dif/10000] = 0x20; //space
        //     graph[last_diff/10000 +1] = 0x20; //space
        //     graph[diff/10000] = '*';
        //     graph[diff/10000 +1] = 0;
        //     printf("\n%s\t%d kHz",graph,diff/1000);
        // }
        // last_diff = diff;
        i++;
        last_val = new_val;
        // for (int i = 0 ; i < 200 ; i++) {
        //     asm volatile ("nop");
        // }

        timer_cycles_init();
        timer_irq_enable();
        timer_arm_start(VCO_UPDATE_CC); // 50 cycles for taking into account initialization
        asm volatile ("wfi");
        timer_irq_clear();

    }


    // while(1){
    //     new_val = VCOp_get_coarse();
    //     // if(diff > 0 && diff < 1000000){
    //     //     for( int i =0; i < diff/10000; i++){
    //     //         printf(" ");
    //     //     }
    //     // }
    //     printf("*");
    //     i++;
    //     last_val = new_val;
    //     for (int i = 0 ; i < 200 ; i++) {
    //         asm volatile ("nop");
    //     }

    //     timer_cycles_init();
    //     timer_irq_enable();
    //     timer_arm_start(VCO_UPDATE_CC); // 50 cycles for taking into account initialization
    //     asm volatile ("wfi");
    //     timer_irq_clear();

    // }


    // timer_arm_start(1000000);
    // while(1){
    //     CSR_CLEAR_BITS(CSR_REG_MSTATUS, 0x8);
    //     wait_for_interrupt();
    //     CSR_SET_BITS(CSR_REG_MSTATUS, 0x8);
    //     // Set the VCO counter limit to 100 cycles
    //     new_val = VCOp_get_coarse();
    //     printf("%d: %d\t%d\n",i, new_val, new_val - last_val);
    //     i++;
    //     last_val = new_val;
    // }





    // // Trigger a manual refresh
    // VCO_trigger();


    // Wait a bit for the first refresh at least
    // for (int i = 0 ; i < 2000 ; i++) {
    //     asm volatile ("nop");
    // }

    // CHECK manually in the waveforms the vco_counter_overflow signal
    // in the tb_system. It should have one-cycle pulses every 100+1
    // cycles.

    return 0;
}