// Copyright 2025 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// Author: Juan Sapriza
// Related references:
// Phase-domain LC - uses the phase of the oscillation: https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=8641302
// IF-TEM - Performs integration in the analog domain, instead of time: https://arxiv.org/pdf/2202.02015

#include "VCO_decoder.h"
#include "pad_control.h"
#include "pad_control_regs.h"
#include "gpio.h"

#include "timer_sdk.h"
#include "soc_ctrl.h"

#define OVF_CNT 100000

#define GPIO_DIR    3
#define GPIO_XING   2

#define SYS_FCLK_HZ 10000000

volatile uint8_t state =0;

void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_arm_stop();
    timer_irq_clear();

    // gpio_write(GPIO_DIR,   state);
    state = ~state;

    timer_cycles_init();
    timer_irq_enable();
    timer_arm_start(SYS_FCLK_HZ/2);
    timer_irq_clear();
    return;
}


int main() {
    uint32_t count, last_count = 0;

    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl,SYS_FCLK_HZ);

    timer_cycles_init();         // Init the timer SDK for clock cycles
    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();
    timer_arm_start(SYS_FCLK_HZ/2);


    pad_control_t pad_control;
    pad_control.base_addr = mmio_region_from_addr((uintptr_t)PAD_CONTROL_START_ADDRESS);
    pad_control_set_mux(&pad_control, (ptrdiff_t)PAD_CONTROL_PAD_MUX_LC_XING_REG_OFFSET,   1);
    pad_control_set_mux(&pad_control, (ptrdiff_t)PAD_CONTROL_PAD_MUX_LC_DIR_REG_OFFSET,     1);

    gpio_cfg_t pin_data = { .pin = GPIO_DIR, .mode = GpioModeOutPushPull };
    if (gpio_config (pin_data) != GpioOk) return 1;
    gpio_cfg_t pin_led = { .pin = GPIO_XING, .mode = GpioModeOutPushPull };
    if (gpio_config (pin_led) != GpioOk) return 1;


    // Enable the VCOp
    VCOp_enable(true);
    // Because the HW overflow is not working, we will sense the VCO each cycle to
    // detect the overflow "manually"
    VCO_set_refresh_rate(1);

    printf("=== test VCO overflow ===\n");
    gpio_write(GPIO_XING,   0);
    while(1){
        count = VCOp_get_coarse();
        if( count - last_count >= OVF_CNT){
            gpio_write(GPIO_XING,   1);
            for (int i = 0 ; i < 5000 ; i++) asm volatile ("nop");
            gpio_write(GPIO_XING,   0);
            last_count = count;
        } else {
            if( count < last_count ){
                last_count = 0;
            }
        }

    }

    return 0;
}