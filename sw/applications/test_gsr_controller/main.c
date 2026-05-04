#include "timer_sdk.h"
#include "soc_ctrl.h"
#include "REFs_ctrl.h"
#include "iDAC_ctrl.h"
#include "x-heep.h"
#include <stdio.h>

#include "VCO_sdk.h"
#include "GSR_sdk.h"
#include "GSR_controller.h"

#define PRINTF_IN_SIM  0
#define PRINTF_IN_FPGA 0
#define TARGET_SIM     1

#if TARGET_SIM && PRINTF_IN_SIM
    #define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#elif PRINTF_IN_FPGA && !TARGET_SIM
    #define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define PRINTF(...)
#endif

#define SYS_FCLK_HZ         10000000U
#define IREF_DEFAULT_CAL    255U
#define IDAC_DEFAULT_CAL    15U
#define VREF_DEFAULT_CAL    0b1111111111U

#define SAMPLE_ATTEMPT_LIMIT  10U
#define N_READ_STEPS          2U

#define GSR_VCO_SUPPLY_VOLTAGE_UV 800000U
#define GSR_VIN_MIN_UV            330000U

#define VCO_ACCEL_RATIO 100 // The ratio by which the VCO is accelerated in simulation to allow faster testing. The refresh rate and integration rate are divided by this factor in simulation mode.
volatile uint32_t debug __attribute__((section(".xheep_debug_mem")));

void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_irq_clear();
    timer_irq_disable();
    debug = 'wake';
    return;
}

static void hw_init(void) {
    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl, SYS_FCLK_HZ);

    REFs_calibrate(IREF_DEFAULT_CAL, IREF1);
    REFs_calibrate(VREF_DEFAULT_CAL, VREF);

    iDACs_enable(true, false);
    iDAC1_calibrate(IDAC_DEFAULT_CAL);

    enable_timer_interrupt();   // Enable the timer machine-level interrupt
    timer_irq_enable();
    timer_cycles_init();
    timer_start();
}

static uint32_t max_current_for_conductance_nS_test(uint32_t conductance_nS) {
    const uint32_t delta_v_min_uV = GSR_VCO_SUPPLY_VOLTAGE_UV - GSR_VIN_MIN_UV;
    return (uint32_t)(((uint64_t)conductance_nS * delta_v_min_uV) / 1000000ULL);
}

static int init_default_controller(gsr_controller_t *ctrl) {
    gsr_status_t st;

    st = gsr_set_default_settings(ctrl);
    if (st != GSR_STATUS_OK) {
        PRINTF("  FAIL: gsr_set_default_settings returned %d\n", st);
        return -1;
    }

    st = gsr_controller_init(ctrl);
    if (st != GSR_STATUS_OK) {
        PRINTF("  FAIL: gsr_controller_init returned %d\n", st);
        return -1;
    }

    return 0;
}

static uint32_t refresh_wait_cycles(uint32_t refresh_rate_Hz) {
#if TARGET_SIM
    return SYS_FCLK_HZ / (VCO_ACCEL_RATIO * refresh_rate_Hz);
#else
    return SYS_FCLK_HZ / refresh_rate_Hz;
#endif
}
static void wait_for_next_refresh(uint32_t refresh_rate_Hz) {
    uint32_t now;

    if (refresh_rate_Hz == 0U) return;
    now = timer_get_cycles();
    uint32_t wake_at = now + refresh_wait_cycles(refresh_rate_Hz);// - 5700U; // Subtract some cycles to account for the overhead of the timer operations and ensure we wake up shortly after the next refresh starts.
                                                                            // 5700 should be computed after profiling gsr_read_sample
    timer_irq_clear();
    timer_arm_set(wake_at);
    timer_irq_enable();
    debug = 'slep';
    asm volatile ("wfi");
}

static int wait_for_read_sample_status(gsr_controller_t *ctrl,
                                       uint32_t M,
                                       gsr_status_t *final_status) {
    uint32_t attempts = 0U;
    uint32_t read_sample_cycles = 0U;

    while (attempts < SAMPLE_ATTEMPT_LIMIT) {
        uint32_t refresh_rate_Hz = ctrl->config.current_refresh_rate_Hz;
        gsr_status_t st = gsr_read_sample(ctrl);
        debug = st;
        debug = ctrl->sample.G_nS;

        if (st == GSR_STATUS_OK) {
            *final_status = st;
            if (refresh_rate_Hz == 0U) {
                refresh_rate_Hz = ctrl->config.baseline_refresh_rate_Hz;
            }
            wait_for_next_refresh(refresh_rate_Hz);
            return 0;
        }

        if (st == GSR_STATUS_NOT_INITIALIZED ||
            st == GSR_STATUS_NO_NEW_SAMPLE ) {
            if (refresh_rate_Hz == 0U) {
                refresh_rate_Hz = ctrl->config.baseline_refresh_rate_Hz;
            }
            wait_for_next_refresh(refresh_rate_Hz);
            attempts++;
            continue;
        }

        if (st == GSR_STATUS_MISSED_UPDATE) { // poll again immediately 
            attempts++;
            continue;
        }

        PRINTF("  FAIL: gsr_read_sample(M=%lu) returned %d\n",
               (unsigned long)M,
               (int)st);
        return -1;
    }

    PRINTF("  FAIL: timed out waiting for gsr_read_sample(M=%lu)\n",
           (unsigned long)M);
    return -1;
}

// Stupid waiting loop (should be timer based for real application)
static void wait_cycles_busy(uint32_t cycles)
{
    uint32_t start = timer_get_cycles();
    while ((uint32_t)(timer_get_cycles() - start) < cycles) {
    }
}

/*
This test function tests:
- reading samples with the default controller configuration
- changing the controller configuration (current, refresh rate)
- timer based sampling
- sleeping the VCO and waking it up again then read sample 
- tests duty cycling the VCO to save power and read samples 
*/
static int test_all()
{
    gsr_controller_t ctrl;
    const gsr_sample_t *sample;
    gsr_status_t st = GSR_STATUS_NOT_INITIALIZED;
    PRINTF("GSR controller set_config\n");
    debug = 'set';
    if (init_default_controller(&ctrl) != 0) {
        return -1;
    }
    
    debug ='wait';
    timer_wait_us(5700); // for VCO to start (tuned to have the second read attempt happen at the second refresh so that we have 2 samples to differentiate)
    debug = 'tst1';

    /* 
    * ----------------------------Test 1: basic config + sample reading (timer based)-----------------------------
    */
    uint8_t steps_done = 0;
    while (steps_done<N_READ_STEPS) {
    
        if (wait_for_read_sample_status(&ctrl, 1U, &st) != 0) {
            return -1;
        }
        sample = gsr_get_last_sample(&ctrl);
        if (sample == NULL || !sample->valid) {
            PRINTF("  FAIL: no valid sample stored after gsr_read_sample(M=1)\n");
            return -1;
        }

        if (sample->current_nA != gsr_current_from_idac_code_nA(ctrl.config.idac_code)) {
            PRINTF("  FAIL: sample current (%lu nA) does not match config (%lu nA)\n",
                (unsigned long)sample->current_nA,
                (unsigned long)gsr_current_from_idac_code_nA(ctrl.config.idac_code));
            return -1;
        }
        steps_done++;
    }
    /* 
    * ----------------------------Test 2: updated config (refresh rate + current) + sample reading (timer based)-----------------------------
    */
    ctrl.config.idac_code = 15U; // Set to a different value than default to test the update
    ctrl.mode = GSR_CTRL_MODE_PHASIC; // Also update refresh rate
    PRINTF("new: idac_code=%d, max_uidc%d\n", ctrl.config.idac_code, ctrl.max_current_nA);
    st = gsr_controller_set_config(&ctrl);
    if (st != GSR_STATUS_OK) {
        PRINTF("  FAIL: gsr_set_config returned %d\n", st);
        return -1;
    }
    debug = 'tst2';
    steps_done = 0;
    // After setting new config, read a sample to verify the new current is applied
    while (steps_done<N_READ_STEPS) {
    
        if (wait_for_read_sample_status(&ctrl, 1U, &st) != 0) {
            return -1;
        }
        sample = gsr_get_last_sample(&ctrl);
        if (sample == NULL || !sample->valid) {
            PRINTF("  FAIL: no valid sample stored after gsr_read_sample(M=1)\n");
            return -1;
        }

        if (sample->current_nA != gsr_current_from_idac_code_nA(ctrl.config.idac_code)) {
            PRINTF("  FAIL: sample current (%lu nA) does not match config (%lu nA)\n",
                (unsigned long)sample->current_nA,
                (unsigned long)gsr_current_from_idac_code_nA(ctrl.config.idac_code));
            return -1;
        }
        steps_done++;
    }
    /* 
    * ----------------------------Test 3: turn VCO off and on + read -----------------------------
    */
    debug = 'tst3';
    vco_enable(ctrl.config.channel, false);
    timer_wait_us(5500);  // just wait some time for the lols
    debug = 'on';
    vco_enable(ctrl.config.channel, true);
    timer_wait_us(1000);  // time it takes for VCO to start TODO: determine this (1ms --> 1'000 cc): it depends if it was successfull or not 
    steps_done = 0;
    while (steps_done<N_READ_STEPS) {
        
        if (wait_for_read_sample_status(&ctrl, 1U, &st) != 0) {
            return -1;
        }
        sample = gsr_get_last_sample(&ctrl);
        if (sample == NULL || !sample->valid) {
            PRINTF("  FAIL: no valid sample stored after gsr_read_sample(M=1)\n");
            return -1;
        }

        if (sample->current_nA != gsr_current_from_idac_code_nA(ctrl.config.idac_code)) {
            PRINTF("  FAIL: sample current (%lu nA) does not match config (%lu nA)\n",
                (unsigned long)sample->current_nA,
                (unsigned long)gsr_current_from_idac_code_nA(ctrl.config.idac_code));
            return -1;
        }
        steps_done++;
    }
    PRINTF("GSR controller set_config PASS\n");

    /* 
    * ----------------------------Test 4: test duty cycling VCO  -----------------------------
    */
    timer_wait_us(10000);
    uint32_t on_cycles;
    uint32_t total_cycles;
    uint32_t off_cycles;

    PRINTF("GSR controller duty_cycle\n");
    debug = 'tst4';

    on_cycles = refresh_wait_cycles(ctrl.config.current_refresh_rate_Hz);

    /* LOW power: 25% duty cycle => 1 refresh ON, 3 refresh periods OFF. */
    ctrl.config.D = 64U;
    total_cycles = (on_cycles * 255U) / ctrl.config.D;
    off_cycles = total_cycles - on_cycles;
    debug = 'L25';
    vco_enable(ctrl.config.channel, false);
    wait_cycles_busy(off_cycles);
    debug = 'on';
    vco_enable(ctrl.config.channel, true);
    st = gsr_read_sample(&ctrl); /* first tap: seed timestamp / start interval */
    debug = st;
    wait_cycles_busy(on_cycles);
    st = gsr_read_sample(&ctrl); /* second tap: get conductance */
    if (st != GSR_STATUS_OK) {
        PRINTF("  FAIL: low-duty second read returned %d\n", st);
        return -1;
    }
    sample = gsr_get_last_sample(&ctrl);
    if (sample == NULL || !sample->valid) {
        PRINTF("  FAIL: no valid low-duty sample\n");
        return -1;
    }
    debug = sample->G_nS;

    /* MID power: 50% duty cycle => 1 refresh ON, 1 refresh period OFF. */
    ctrl.config.D = 128U;
    total_cycles = (on_cycles * 255U) / ctrl.config.D;
    off_cycles = total_cycles - on_cycles;
    debug = 'M50';
    vco_enable(ctrl.config.channel, false);
    wait_cycles_busy(off_cycles);
    vco_enable(ctrl.config.channel, true);
    st = gsr_read_sample(&ctrl);
    debug = st;
    wait_cycles_busy(on_cycles);
    st = gsr_read_sample(&ctrl);
    if (st != GSR_STATUS_OK) {
        PRINTF("  FAIL: mid-duty second read returned %d\n", st);
        return -1;
    }
    sample = gsr_get_last_sample(&ctrl);
    if (sample == NULL || !sample->valid) {
        PRINTF("  FAIL: no valid mid-duty sample\n");
        return -1;
    }
    debug = sample->G_nS;

    /* HIGH power: 100% duty cycle => no OFF gap. */
    ctrl.config.D = 255U;
    total_cycles = on_cycles;
    off_cycles = 0U;
    debug = 'H10';
    vco_enable(ctrl.config.channel, true);
    st = gsr_read_sample(&ctrl);
    debug = st;
    wait_cycles_busy(on_cycles);
    st = gsr_read_sample(&ctrl);
    if (st != GSR_STATUS_OK) {
        PRINTF("  FAIL: high-duty second read returned %d\n", st);
        return -1;
    }
    sample = gsr_get_last_sample(&ctrl);
    if (sample == NULL || !sample->valid) {
        PRINTF("  FAIL: no valid high-duty sample\n");
        return -1;
    }
    debug = sample->G_nS;

    vco_enable(ctrl.config.channel, false);
    return 0;
}

int main(void)
{
    debug = 'Init';

    hw_init();

    int failures = 0;

    failures += (test_all() != 0) ? 1 : 0;

    if (failures == 0) {
        debug = 'pass';
        PRINTF("=== ALL TESTS PASSED ===\n");
    } else {
        PRINTF("=== %d TEST(S) FAILED ===\n", failures);
    }
    debug = 'stop';

    return failures;
}
