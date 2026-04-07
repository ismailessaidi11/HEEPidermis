// Copyright 2025 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// Author: David Mallasen
// Description: Test application for the VCO counter in the VCO decoder

#include "VCO_decoder.h"
#include "timer_sdk.h"
#include "soc_ctrl.h"

#define VCO_FS_HZ 10
#define SYS_FCLK_HZ 10000000
#define VCO_UPDATE_CC (SYS_FCLK_HZ/VCO_FS_HZ)

#define VCO_SUPPLY_FROM_LDO 1
#define VCO_CAL_FROM_LDO_ADD_HZ    20
#define VCO_CAL_FROM_LDO_ADD_uV    5700

#define COMPUTE_AVG         0
#define MOVING_AVG_WINDOW   10

#define PSEUDO_DIFF_MODE    1

uint32_t window[MOVING_AVG_WINDOW];

#define TABLE_SIZE 25
uint32_t table_Vin_uV[TABLE_SIZE] ={340000, 360000, 380000, 400000, 420000, 440000, 460000, 480000, 500000, 520000, 540000, 560000, 580000, 600000, 620000, 640000, 660000, 680000, 700000, 720000, 740000, 760000, 780000, 800000, 820000};
uint32_t table_fosc_Hz[TABLE_SIZE] = {26130, 31330, 37320, 45270, 55150, 67270, 82680, 99870, 121190, 146020, 175270, 208990, 247770, 291780, 341260, 396650, 457900, 525140, 598560, 677660, 762750, 853760, 950200, 1051710, 1158000};
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

    // Enable the VCOp (and VCOn)
    VCOp_enable(true);
    VCOn_enable(true);

    // Set the VCO refresh rate to 1000 cycles
    VCO_set_refresh_rate(VCO_UPDATE_CC);
    VCO_set_counter_limit(VCO_UPDATE_CC);

    uint32_t i=0;
    uint32_t coarse_p, coarse_n, index = 0;

    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();

    printf("=== Test VCO variability ===\n");

    timer_cycles_init();
    timer_start();

    while(1){
        coarse_p    = VCOp_get_coarse();
        coarse_n    = VCOn_get_coarse();

        printf("\n%d\t%d\t%d", index, coarse_p, coarse_n);
        index++;

        timer_cycles_init();
        timer_irq_enable();
        timer_arm_start(VCO_UPDATE_CC); // 50 cycles for taking into account initialization
        asm volatile ("wfi");
        timer_irq_clear();

    }

    return 0;
}