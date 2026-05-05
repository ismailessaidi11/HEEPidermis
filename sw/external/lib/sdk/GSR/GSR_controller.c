#define VIN_LOW_UV                    560000U
#define VIN_HIGH_UV                   680000U

#include "GSR_controller.h"

/* 
Estimate the minimum detectable conductance variation at the current
operating point using the local VCO sensitivity and the active sampling rate.
*/
gsr_status_t estimate_deltaG_min_nS(gsr_controller_t *ctrl) {

    uint32_t kvco_Hz_per_V;

    if (ctrl == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    if (ctrl->G_nS == 0 || ctrl->current_refresh_rate_Hz == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    vco_status_t vst = vco_get_kvco_Hz_per_V(ctrl->vin_uV, &kvco_Hz_per_V);
    if (vst != VCO_STATUS_OK) {
        return GSR_STATUS_NOT_INITIALIZED;
    }

    /* Ts = 1 / fs
       i_nA = 40 * idac_code
       A_nS = Ts * K_VCO * i = (K_VCO * i_nA) / fs
       This is numerically in nS
    */
    uint64_t i_nA = (uint64_t)40U * (uint64_t)ctrl->idac_code;
    uint64_t A_nS = ((uint64_t)kvco_Hz_per_V * i_nA) / (uint64_t)ctrl->current_refresh_rate_Hz;

    if (A_nS <= (uint64_t)ctrl->G_nS) {
        return GSR_STATUS_OUT_OF_RANGE;
    }

    ctrl->deltaG_min_nS =
        (uint32_t)(((uint64_t)ctrl->G_nS * (uint64_t)ctrl->G_nS) / (A_nS - (uint64_t)ctrl->G_nS));

    return GSR_STATUS_OK;
}

// Update the baseline estimate using a simple exponential-style moving average.
static uint32_t calculate_baseline(uint32_t prev_baseline, uint32_t sample) {
    
    return (uint32_t)(((uint64_t)7U * prev_baseline + sample) / 8U);
}

// Reconfigure the VCO refresh rate while keeping the selected channel unchanged.
static gsr_status_t controller_set_refresh(vco_channel_t channel, uint32_t refresh_rate_Hz) {

    vco_status_t st = vco_initialize(channel, refresh_rate_Hz);

    if (st == VCO_STATUS_OK) return GSR_STATUS_OK;
    if (st == VCO_STATUS_INVALID_ARGUMENT) return GSR_STATUS_INVALID_ARGUMENT;
    return GSR_STATUS_NOT_INITIALIZED;

}

/*
Detect whether the current sample indicates the onset of a phasic event using either
deviation from the baseline or slope magnitude
*/
static bool event_detected(const gsr_controller_t *ctrl){

    uint32_t amp = (ctrl->G_nS >= ctrl->baseline_nS)
             ? (ctrl->G_nS - ctrl->baseline_nS)
             : (ctrl->baseline_nS - ctrl->G_nS);
    uint32_t slope_abs = (ctrl->slope_nS >= 0) ? (uint32_t)ctrl->slope_nS : (uint32_t)(-ctrl->slope_nS);

    return (amp >= ctrl->amplitude_threshold_nS) || (slope_abs >= ctrl->slope_threshold_nS);

}

// Check whether the signal has returned close enough to baseline
static bool signal_settled(const gsr_controller_t *ctrl) {

    uint32_t amp = (ctrl->G_nS >= ctrl->baseline_nS)
             ? (ctrl->G_nS - ctrl->baseline_nS)
             : (ctrl->baseline_nS - ctrl->G_nS);
    uint32_t slope_abs = (ctrl->slope_nS >= 0) ? (uint32_t)ctrl->slope_nS : (uint32_t)(-ctrl->slope_nS);

    return (amp <= ctrl->settle_threshold_nS) && (slope_abs <= ctrl->settle_threshold_nS);

}

/*
Adjust the injected current during baseline mode to keep Vin inside the
desired operating window.
*/
static void retune_current_for_baseline(gsr_controller_t *ctrl) {
    uint32_t vin_uV = ctrl->vin_uV;
    int32_t new_code;

    /* Already in the desired operating region */
    if ((vin_uV >= VIN_LOW_UV) && (vin_uV <= VIN_HIGH_UV)) {
        return;
    }

    new_code = (int32_t)ctrl->idac_code;

    /*
     * Vin = VDD - I/G
     * If Vin is too low, current is too high -> decrease code
     * If Vin is too high, current is too low -> increase code
     */
    if (vin_uV < VIN_LOW_UV && new_code>1) {
        new_code--;
        ctrl->idac_code = (uint8_t)new_code;
        gsr_update_current(ctrl->idac_code);
    } else if (vin_uV > VIN_HIGH_UV && new_code<250) {
        new_code++;
        ctrl->idac_code = (uint8_t)new_code;
        gsr_update_current(ctrl->idac_code);
    }
}

// Load a default controller configuration for standard GSR operation.
gsr_status_t gsr_set_default_settings(gsr_controller_t *ctrl) {

    if (ctrl == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    ctrl->channel = VCO_CHANNEL_P;
    ctrl->baseline_refresh_rate_Hz = 2;
    ctrl->phasic_refresh_rate_Hz = 20;
    ctrl->recovery_refresh_rate_Hz = 5;
    ctrl->idac_code = 20;
    ctrl->amplitude_threshold_nS = 80;
    ctrl->slope_threshold_nS = 40;
    ctrl->settle_threshold_nS = 25;
    ctrl->recovery_count_required = 8;
    ctrl->deltaG_min_nS = 0;

    return GSR_STATUS_OK;
}

// Initialize controller state variables and start the GSR front-end.
gsr_status_t gsr_controller_init(gsr_controller_t *ctrl) {

    if (ctrl == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    ctrl->mode = GSR_CTRL_MODE_INIT;

    ctrl->G_nS = 0;
    ctrl->vin_uV = 0;
    ctrl->prev_G_nS = 0;
    ctrl->baseline_nS = 0;
    ctrl->slope_nS = 0;
    ctrl->recovery_counter = 0;
    ctrl->initialized = false;

    return gsr_init(ctrl->channel, ctrl->baseline_refresh_rate_Hz, ctrl->idac_code);

}

/*
Execute one control step.

Sequence:
1. read the latest conductance sample
2. update signal estimates (current value, slope, baseline)
3. evaluate event / settling conditions
4. switch mode and refresh rate if needed
*/
gsr_status_t gsr_controller_step(gsr_controller_t *ctrl) {

    uint32_t sample_nS = 0;
    uint32_t vin_uV;
    gsr_status_t st;

    if (ctrl == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    st = gsr_get_conductance_nS(&sample_nS, &vin_uV);
    if (st != GSR_STATUS_OK) {
        return st;
    }

    ctrl->vin_uV = vin_uV;
    ctrl->prev_G_nS = ctrl->G_nS;
    ctrl->G_nS = sample_nS;

    if (!ctrl->initialized) {
        ctrl->baseline_nS = ctrl->G_nS;
        ctrl->prev_G_nS = ctrl->G_nS;
        ctrl->slope_nS = 0;
        ctrl->mode = GSR_CTRL_MODE_BASELINE;
        ctrl->current_refresh_rate_Hz = ctrl->baseline_refresh_rate_Hz;
        ctrl->initialized = true;
        return GSR_STATUS_OK;
    }

    // Slope is expressed in nS/s by multiplying the sample difference by fs.
    ctrl->slope_nS = ((int32_t)ctrl->G_nS - (int32_t)ctrl->prev_G_nS) * (int32_t)ctrl->current_refresh_rate_Hz;

    // Only baseline mode is allowed to slowly adapt the tonic reference.
    if (ctrl->mode == GSR_CTRL_MODE_BASELINE) {
        ctrl->baseline_nS = calculate_baseline(ctrl->baseline_nS, ctrl->G_nS);
    }

    switch (ctrl->mode) {

    case GSR_CTRL_MODE_INIT:
        ctrl->mode = GSR_CTRL_MODE_BASELINE;
        break;

    case GSR_CTRL_MODE_BASELINE:
        //This line was removed since Esmail will be working on this.
        // retune_current_for_baseline(ctrl);

        if (event_detected(ctrl)) {
            st = controller_set_refresh(ctrl->channel, ctrl->phasic_refresh_rate_Hz);
            if (st != GSR_STATUS_OK) return st;

            ctrl->current_refresh_rate_Hz = ctrl->phasic_refresh_rate_Hz;
            ctrl->mode = GSR_CTRL_MODE_PHASIC;
            ctrl->recovery_counter = 0;
        }
        break;
    
    // Phasic mode increases sampling rate to better capture fast events.
    case GSR_CTRL_MODE_PHASIC:
        if (signal_settled(ctrl)) {
            st = controller_set_refresh(ctrl->channel, ctrl->recovery_refresh_rate_Hz);
            if (st != GSR_STATUS_OK) return st;

            ctrl->current_refresh_rate_Hz = ctrl->recovery_refresh_rate_Hz;
            ctrl->mode = GSR_CTRL_MODE_RECOVERY;
            ctrl->recovery_counter = 0;
        }
        break;

    // Recovery mode uses an intermediate sampling rate until the signal is stable again.
    case GSR_CTRL_MODE_RECOVERY:
        if (!signal_settled(ctrl)) {
            st = controller_set_refresh(ctrl->channel, ctrl->phasic_refresh_rate_Hz);
            if (st != GSR_STATUS_OK) return st;

            ctrl->current_refresh_rate_Hz = ctrl->phasic_refresh_rate_Hz;
            ctrl->mode = GSR_CTRL_MODE_PHASIC;
            ctrl->recovery_counter = 0;
            break;
        }

        ctrl->recovery_counter++;

        if (ctrl->recovery_counter >= ctrl->recovery_count_required) {
            retune_current_for_baseline(ctrl);

            st = controller_set_refresh(ctrl->channel, ctrl->baseline_refresh_rate_Hz);
            if (st != GSR_STATUS_OK) return st;

            ctrl->current_refresh_rate_Hz = ctrl->baseline_refresh_rate_Hz;
            ctrl->mode = GSR_CTRL_MODE_BASELINE;
            ctrl->recovery_counter = 0U;
        }
        break;

    default:
        return GSR_STATUS_NOT_INITIALIZED;
    }

    return GSR_STATUS_OK;
}