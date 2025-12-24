// Copyright 2025 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// Author: Juan Sapriza
// Description: Test application for the iDACs

#include "timer_sdk.h"
#include "soc_ctrl.h"
#include "cheep.h"
#include "pad_control.h"
#include "pad_control_regs.h"
#include "gpio.h"
#include "REFs_ctrl.h"

#define COOL_DOWN_MSEC 500
#define SYS_FCLK_HZ 10000000
#define COOL_DOWN_CC ((SYS_FCLK_HZ/1000)*COOL_DOWN_MSEC)

#define GPIO_DSM_IN 5
#define GPIO_DSM_CLK 4
#define SHUNT_RES_OHM 55600

#define IREF_NOMINAL_NA 400
#define IREF_CALIB_LSB_NA 4
#define IREF_CALIB_BITS 10
#define IREF_CALIB_MAX 32
#define IREF_CALIB_HLF 16
#define IREF_EXPECTED_NA(val) (IREF_NOMINAL_NA - (IREF_CALIB_HLF)*IREF_CALIB_LSB_NA + val*IREF_CALIB_LSB_NA)

#define VREF_NOMINAL_MV 800
#define VREF_CALIB_LSB_MV 8
#define VREF_CALIB_BITS 10
#define VREF_CALIB_MAX 32
#define VREF_CALIB_HLF 16
#define VREF_EXPECTED_MV(val) VREF_NOMINAL_MV - (VREF_CALIB_HLF)*VREF_CALIB_LSB_MV + val*VREF_CALIB_LSB_MV

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

uint16_t codes[] = {0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023};

void update_refs(uint8_t val){
    val %= sizeof(codes)/2;
    uint16_t code = codes[val];
    REFs_calibrate( code,    IREF1 );
    REFs_calibrate( code,    IREF2 );
    REFs_calibrate( code,    VREF );
    printf("%d = %d\t %d nA \t%d nA \t%d mV\n", val, code, IREF_EXPECTED_NA(val), IREF_EXPECTED_NA(val), VREF_EXPECTED_MV(val));
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

    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();

    printf("=== test REFs linear GPIO ===\n");

    timer_cycles_init();
    timer_start();

    uint8_t gpio_high;

    while(1){

        gpio_read(GPIO_DSM_IN, &gpio_high);
        if(gpio_high){
            val++;
            printf("UP\n");
            update_refs(val);
            LAUNCH_TIMER();
        }
        gpio_read(GPIO_DSM_CLK, &gpio_high);
        if(gpio_high){
            val--;
            printf("DN\n");
            update_refs(val);
            LAUNCH_TIMER();
        }
    }

    return 0;
}