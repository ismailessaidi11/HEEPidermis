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
#define IDAC_DEFAULT_CAL    0U
#define VREF_DEFAULT_CAL    0b1111111111U

#define OVERSAMPLE_RATIO      4U
#define N_CTRL_STEPS          20000000U
#define SAMPLE_ATTEMPT_LIMIT  200000U
#define N_READ_STEPS          4U

#define GSR_VCO_SUPPLY_VOLTAGE_UV 800000U
#define GSR_VIN_MIN_UV            330000U

volatile uint32_t debug __attribute__((section(".xheep_debug_mem")));

void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    timer_arm_stop();
    timer_irq_clear();
}

static void hw_init(void) {
    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl, SYS_FCLK_HZ);

    timer_cycles_init();

    REFs_calibrate(IREF_DEFAULT_CAL, IREF1);
    REFs_calibrate(VREF_DEFAULT_CAL, VREF);

    iDACs_enable(true, false);
    iDAC1_calibrate(IDAC_DEFAULT_CAL);

    enable_timer_interrupt();
    timer_irq_enable();
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
    return SYS_FCLK_HZ / (1000U * refresh_rate_Hz);
#else
    return SYS_FCLK_HZ / refresh_rate_Hz;
#endif
}

static void wait_for_next_refresh(uint32_t refresh_rate_Hz) {
    if (refresh_rate_Hz == 0U) {
        return;
    }

    timer_cycles_init();
    timer_irq_enable();
    timer_arm_start(refresh_wait_cycles(refresh_rate_Hz));
    asm volatile ("wfi");
    timer_irq_clear();
}

static int wait_for_read_sample_status(gsr_controller_t *ctrl,
                                       uint32_t oversample_ratio,
                                       gsr_status_t *final_status) {
    uint32_t attempts = 0U;

    while (attempts < SAMPLE_ATTEMPT_LIMIT) {
        uint32_t refresh_rate_Hz = ctrl->config.current_refresh_rate_Hz;
        gsr_status_t st = gsr_read_sample(ctrl, oversample_ratio);
        debug = st;
        PRINTF("%d: Vin=%lu, G=%lu\n",
            (int)st,
            (unsigned long)ctrl->sample.vin_uV,
            (unsigned long)ctrl->sample.G_nS);
        debug = ctrl->sample.G_nS;

        if (st == GSR_STATUS_OK) {
            *final_status = st;
            return 0;
        }

        if (st == GSR_STATUS_NOT_INITIALIZED ||
            st == GSR_STATUS_NO_NEW_SAMPLE ||
            st == GSR_STATUS_MISSED_UPDATE) {
            if (refresh_rate_Hz == 0U) {
                refresh_rate_Hz = ctrl->config.baseline_refresh_rate_Hz;
            }
            wait_for_next_refresh(refresh_rate_Hz);
            attempts++;
            debug = attempts;
            continue;
        }

        PRINTF("  FAIL: gsr_read_sample(osr=%lu) returned %d\n",
               (unsigned long)oversample_ratio,
               (int)st);
        return -1;
    }

    PRINTF("  FAIL: timed out waiting for gsr_read_sample(osr=%lu)\n",
           (unsigned long)oversample_ratio);
    return -1;
}

static int test_controller_read_sample_single(void) {
    gsr_controller_t ctrl;
    gsr_status_t st = GSR_STATUS_NOT_INITIALIZED;
    const gsr_sample_t *sample;

    PRINTF("GSR controller read_sample (osr=1)\n");

    if (init_default_controller(&ctrl) != 0) {
        return -1;
    }

    uint8_t steps_done = 0;
    while (steps_done<N_READ_STEPS) {
        debug = steps_done; // For debugging: track the number of attempts/reads
        if (wait_for_read_sample_status(&ctrl, 1U, &st) != 0) {
            return -1;
        }
        sample = gsr_get_last_sample(&ctrl);
        if (sample == NULL || !sample->valid) {
            PRINTF("  FAIL: no valid sample stored after gsr_read_sample(osr=1)\n");
            return -1;
        }
        PRINTF("%d: Vin=%lu, G=%lu\n",
            steps_done,
            (unsigned long)sample->vin_uV,
            (unsigned long)sample->G_nS);

        if (sample->current_nA != gsr_current_from_idac_code_nA(ctrl.config.idac_code)) {
            PRINTF("  FAIL: sample current (%lu nA) does not match config (%lu nA)\n",
                (unsigned long)sample->current_nA,
                (unsigned long)gsr_current_from_idac_code_nA(ctrl.config.idac_code));
            return -1;
        }
        steps_done++;
    }
    

    PRINTF("GSR controller read_sample PASS (osr=1)\n");
    return 0;
}

static int test_set_config_controller()
{
    gsr_controller_t ctrl;
    const gsr_sample_t *sample;
    gsr_status_t st = GSR_STATUS_NOT_INITIALIZED;
    PRINTF("GSR controller set_config\n");

    if (init_default_controller(&ctrl) != 0) {
        return -1;
    }
    debug = 'set';

    uint8_t steps_done = 0;
    while (steps_done<N_READ_STEPS) {
    
        if (wait_for_read_sample_status(&ctrl, 1U, &st) != 0) {
            return -1;
        }
        sample = gsr_get_last_sample(&ctrl);
        if (sample == NULL || !sample->valid) {
            PRINTF("  FAIL: no valid sample stored after gsr_read_sample(osr=1)\n");
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
    
    ctrl.config.idac_code = 20U; // Set to a different value than default to test the update
    ctrl.mode = GSR_CTRL_MODE_PHASIC; // Also update refresh rate

    st = gsr_controller_set_config(&ctrl);
    if (st != GSR_STATUS_OK) {
        PRINTF("  FAIL: gsr_set_config returned %d\n", st);
        return -1;
    }
    PRINTF("  gsr_set_config applied new settings: idac_code=%d, phasic_refresh_rate_Hz=%d\n", ctrl.config.idac_code, ctrl.config.phasic_refresh_rate_Hz);
    debug = 'new';
    steps_done = 0;
    // After setting new config, read a sample to verify the new current is applied
    while (steps_done<N_READ_STEPS) {
    
        if (wait_for_read_sample_status(&ctrl, 1U, &st) != 0) {
            return -1;
        }
        sample = gsr_get_last_sample(&ctrl);
        if (sample == NULL || !sample->valid) {
            PRINTF("  FAIL: no valid sample stored after gsr_read_sample(osr=1)\n");
            return -1;
        }
        PRINTF("%d: Vin=%lu, G=%lu\n",
            steps_done,
            (unsigned long)sample->vin_uV,
            (unsigned long)sample->G_nS);

        if (sample->current_nA != gsr_current_from_idac_code_nA(ctrl.config.idac_code)) {
            PRINTF("  FAIL: sample current (%lu nA) does not match config (%lu nA)\n",
                (unsigned long)sample->current_nA,
                (unsigned long)gsr_current_from_idac_code_nA(ctrl.config.idac_code));
            return -1;
        }
        steps_done++;
    }

    PRINTF("GSR controller set_config PASS\n");
    return 0;
}


int main(void)
{
    debug = 'Init';

    hw_init();

    int failures = 0;

    // failures += (test_controller_read_sample_single() != 0) ? 1 : 0;
    failures += (test_set_config_controller() != 0) ? 1 : 0;

    if (failures == 0) {
        debug = 'pass';
        PRINTF("=== ALL TESTS PASSED ===\n");
    } else {
        PRINTF("=== %d TEST(S) FAILED ===\n", failures);
    }
    debug = 'stop';

    return failures;
}
