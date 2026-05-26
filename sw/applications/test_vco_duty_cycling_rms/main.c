#include "timer_sdk.h"
#include "soc_ctrl.h"
#include "REFs_ctrl.h"
#include "iDAC_ctrl.h"
#include "x-heep.h"

#include "GSR_controller.h"

#define SYS_FCLK_HZ      10000000U
#define IREF_DEFAULT_CAL 255U
#define IDAC_DEFAULT_CAL 15U
#define VREF_DEFAULT_CAL 0b1111111111U

#define N_VALID_SAMPLES  5U
#define DBG_SAMPLE_TAG   0x00U
#define DBG_PHASE_TAG    0xA0U
#define DBG_DONE_TAG     0xAFU

volatile uint32_t debug __attribute__((section(".xheep_debug_mem")));

void __attribute__((aligned(4), interrupt)) handler_irq_timer(void) {
    vco_handle_timer_irq();
    debug = 'wake';
}
#ifndef DUTY_SEQ_ID
#define DUTY_SEQ_ID 1
#endif

static const uint8_t duty_seq_1[] = {8U, 4U, 2U, 1U};
static const uint8_t duty_seq_2[] = {1U, 2U, 4U, 8U};
static const uint8_t duty_seq_3[] = {4U, 1U, 8U, 2U};
static const uint8_t duty_seq_4[] = {2U, 8U, 1U, 4U};

#if DUTY_SEQ_ID == 1
#define DUTY_SEQ duty_seq_1
#elif DUTY_SEQ_ID == 2
#define DUTY_SEQ duty_seq_2
#elif DUTY_SEQ_ID == 3
#define DUTY_SEQ duty_seq_3
#elif DUTY_SEQ_ID == 4
#define DUTY_SEQ duty_seq_4
#else
#error "Invalid DUTY_SEQ_ID"
#endif

static void debug_mark(uint8_t tag, uint32_t value) {
    debug = ((uint32_t)tag << 24) | (value & 0x00FFFFFFU);
}

static void hw_init(void) {
    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl, SYS_FCLK_HZ);

    REFs_calibrate(IREF_DEFAULT_CAL, IREF1);
    REFs_calibrate(VREF_DEFAULT_CAL, VREF);

    iDACs_enable(true, false);
    iDAC1_calibrate(IDAC_DEFAULT_CAL);

    enable_timer_interrupt();
    timer_irq_enable();
    timer_cycles_init();
    timer_start();
}

static int init_controller(gsr_controller_t *ctrl) {
    gsr_status_t st;

    st = gsr_set_default_settings(ctrl);
    if (st != GSR_STATUS_OK) {
        debug_mark(0xE1U, (uint32_t)st);
        return -1;
    }

    st = gsr_controller_init(ctrl);
    if (st != GSR_STATUS_OK) {
        debug_mark(0xE2U, (uint32_t)st);
        return -1;
    }

    ctrl->mode = GSR_CTRL_MODE_PHASIC;
    ctrl->config.idac_code = 20U;
    ctrl->config.current_refresh_rate_Hz = ctrl->config.baseline_refresh_rate_Hz;
    return 0;
}

static int measure(gsr_controller_t *ctrl, uint8_t n_samples) {
    gsr_status_t st;
    const gsr_sample_t *sample;
    uint8_t samples_cnt = 0;

    while (samples_cnt < n_samples) {
        st = gsr_read_sample(ctrl);  // attempt tap/read after event
        // debug_mark(samples_cnt, (uint32_t)st);

        if (st == GSR_STATUS_OK) {
            sample = gsr_get_last_sample(ctrl);
            if (sample == NULL || !sample->valid) {
                debug_mark((uint8_t)(0xE1), 0U);
                return -1;
            }
            debug_mark(DBG_SAMPLE_TAG, sample->G_nS);
            samples_cnt++;
        } else if (st == GSR_STATUS_MISSED_UPDATE || st == GSR_STATUS_NO_NEW_SAMPLE) {
            continue;
        } else {
            debug_mark(0xF0U, (uint32_t)st);
            return -1;
        }
    }
    return 0;
}

static int set_duty_and_measure(gsr_controller_t *ctrl, uint8_t duty_cycle_code, gsr_ctrl_mode_t mode) {
    gsr_status_t st;

    ctrl->config.duty_cycle_code = duty_cycle_code;
    ctrl->mode = mode;
    st = gsr_controller_set_config(ctrl);
    if (st != GSR_STATUS_OK) {
        debug_mark(0xE3U, (uint32_t)st);
        return -1;
    }
    debug_mark(DBG_PHASE_TAG, duty_cycle_code);

    return measure(ctrl, N_VALID_SAMPLES);
}

int main(void) {
    gsr_controller_t ctrl;

    debug = 'Init';
    hw_init();

    if (init_controller(&ctrl) != 0) {
        return -1;
    }

    for (uint8_t i = 0; i < 4U; ++i) {
        if (set_duty_and_measure(&ctrl, DUTY_SEQ[i], GSR_CTRL_MODE_PHASIC) != 0) {
            return -1;
        }
    }
    
    debug_mark(DBG_DONE_TAG, 0U);
    return 0;
}
