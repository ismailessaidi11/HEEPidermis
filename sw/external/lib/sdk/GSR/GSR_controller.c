#define GSR_IDAC_LSB_NA              40U
#define GSR_IDAC_MAX_CODE            255U
#define GSR_MAX_CURRENT_NA           ((uint32_t)GSR_IDAC_MAX_CODE * GSR_IDAC_LSB_NA)
#define GSR_VCO_SUPPLY_VOLTAGE_UV    800000U
#define GSR_VIN_MIN_UV               330000U
#define GUARD_IDC_NA                 50U // guard i_dc to prevent going out of range in the next conductance measurement; 500nA corresponds to 1 uS of change in conductance
#define VCO_VARIANCE                 3U // variance in the VCO frequency-to-voltage conversion, used for sensitivity estimation

#define min(a, b) (((a) < (b)) ? (a) : (b))

#include "GSR_controller.h"

// Update the baseline estimate using a simple exponential-style moving average.
static uint32_t calculate_baseline(uint32_t prev_baseline, uint32_t sample) {
    
    return (uint32_t)(((uint64_t)7U * prev_baseline + sample) / 8U);
}

// Reconfigure the iDAC current used for conductance measurement, with range checking based on the current limits.
static gsr_status_t controller_set_current(gsr_controller_t *ctrl, uint8_t idac_code) {
    uint32_t current_nA;

    if (ctrl == NULL) return GSR_STATUS_INVALID_ARGUMENT;
    current_nA = gsr_current_from_idac_code_nA(idac_code);
    if (ctrl->max_current_nA < GUARD_IDC_NA) { // TODO: handle that differently in the future
        return GSR_STATUS_OUT_OF_RANGE; // current limits are too low to safely update, this is a protective check to prevent underflow in the next check
    }
    // check validity of current range.
    if (current_nA > ctrl->max_current_nA - GUARD_IDC_NA) { 
        return GSR_STATUS_OUT_OF_RANGE;
    }

    // current configuration 
    ctrl->config.idac_code = idac_code;
    
    // Delegate actual hardware/current model update to the clean SDK
    gsr_update_current(idac_code);

    return GSR_STATUS_OK;
}

// Reconfigure the VCO refresh rate while keeping the selected channel unchanged.
static gsr_status_t controller_set_refresh(gsr_controller_t *ctrl, gsr_ctrl_mode_t mode) {

    uint32_t new_rate_Hz;
    switch (mode) {
        case GSR_CTRL_MODE_BASELINE:
            new_rate_Hz = ctrl->config.baseline_refresh_rate_Hz;
            break;
        case GSR_CTRL_MODE_PHASIC:
            new_rate_Hz = ctrl->config.phasic_refresh_rate_Hz;
            break;
        case GSR_CTRL_MODE_RECOVERY:
            new_rate_Hz = ctrl->config.recovery_refresh_rate_Hz;
            break;
        default:
            return GSR_STATUS_INVALID_ARGUMENT;
    }
    ctrl->config.current_refresh_rate_Hz = new_rate_Hz; // keep track of the current refresh rate in the controller state for reference
    return gsr_status_from_vco(vco_set_refresh_rate(new_rate_Hz));
}

static uint32_t max_current_for_conductance_nS(uint32_t conductance_nS) {
    const uint32_t delta_v_min_uV = GSR_VCO_SUPPLY_VOLTAGE_UV - GSR_VIN_MIN_UV;
    uint32_t max_current_nA = (uint32_t)(((uint64_t)conductance_nS * delta_v_min_uV) / 1000000ULL);

    return min(max_current_nA, GSR_MAX_CURRENT_NA); 
}

// TODO: Compute the frequency error of the VCO (based on allen deviation measurements)
static uint32_t gsr_get_frequency_error_Hz(uint32_t vin_uV, uint32_t integration_rate_Hz, uint8_t variance)
{

    // We use a fixed value for the frequency error based on the Allen deviation measurements of the VCO. 
    #ifdef ADEV_VAR
        return 0; // not implemented yet
    #else
        return integration_rate_Hz;
    #endif
}

// Compute the conductance sensitivity (delta G) of the VCO around a given Vin, based on the i_dc and refresh rate.
static uint32_t gsr_get_conductance_sensitivity_nS(uint32_t conductance_nS, uint32_t vin_uV, uint32_t current_nA, uint32_t integration_rate_Hz)
{
    uint32_t frequency_error_Hz = gsr_get_frequency_error_Hz(vin_uV, integration_rate_Hz, VCO_VARIANCE);
    uint32_t kvco_Hz_per_V = vco_get_kvco_Hz_per_V(vin_uV);

    // Note: 32-bit arithmetic is safe from overflow because
    // kvco_Hz_per_V < 6'000'000 Hz/V (from measurements in scripts/plotter)
    // current_nA < 10'200 nA (based on front-end settings)
    // frequency_error_Hz < 10'000 Hz (based on measurements in scripts/plotter)
    // conductance_nS < 500'000 nS (very conservative upper bound for skin conductance)
    // worst case denom = 7 * 10^10  = 2^37 < 2^63, so the 64-bit intermediate is safe from overflow
    // worst case numer = 10'000 * 500'000 * 500'000 = 2.5 * 10^15 = 2^51 < 2^63, so the 64-bit intermediate is safe from overflow 
    uint64_t denom = kvco_Hz_per_V * current_nA + frequency_error_Hz * conductance_nS;

    if (denom == 0ULL) return 0; // avoid division by zero, sensitivity is effectively zero if the VCO frequency doesn't change with voltage or if there is no current
    
    uint64_t numer = frequency_error_Hz * conductance_nS * conductance_nS;

    uint32_t delta_G_nS = (uint32_t)(numer / denom);

    return delta_G_nS;
}

/*
Detect whether the current sample indicates the onset of a phasic event using either
deviation from the baseline or slope magnitude
*/
static bool event_detected(const gsr_controller_t *ctrl){

    uint32_t amp = (ctrl->sample.G_nS >= ctrl->sample.baseline_nS)
             ? (ctrl->sample.G_nS - ctrl->sample.baseline_nS)
             : (ctrl->sample.baseline_nS - ctrl->sample.G_nS);
    uint32_t slope_abs = (ctrl->sample.slope_nS >= 0) ? (uint32_t)ctrl->sample.slope_nS : (uint32_t)(-ctrl->sample.slope_nS);

    return (amp >= ctrl->amplitude_threshold_nS) || (slope_abs >= ctrl->slope_threshold_nS);

}

// Check whether the signal has returned close enough to baseline
static bool signal_settled(const gsr_controller_t *ctrl) {

    uint32_t amp = (ctrl->sample.G_nS >= ctrl->sample.baseline_nS)
             ? (ctrl->sample.G_nS - ctrl->sample.baseline_nS)
             : (ctrl->sample.baseline_nS - ctrl->sample.G_nS);
    uint32_t slope_abs = (ctrl->sample.slope_nS >= 0) ? (uint32_t)ctrl->sample.slope_nS : (uint32_t)(-ctrl->sample.slope_nS);

    return (amp <= ctrl->settle_threshold_nS) && (slope_abs <= ctrl->settle_threshold_nS);

}


// Load a default controller configuration for standard GSR operation.
gsr_status_t gsr_set_default_settings(gsr_controller_t *ctrl) {

    if (ctrl == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    ctrl->config.channel = VCO_CHANNEL_P;
    ctrl->config.baseline_refresh_rate_Hz = 1;
    ctrl->config.phasic_refresh_rate_Hz = 20;
    ctrl->config.recovery_refresh_rate_Hz = 5;
    ctrl->config.idac_code = 20;
    ctrl->amplitude_threshold_nS = 80;
    ctrl->slope_threshold_nS = 40;
    ctrl->settle_threshold_nS = 25;
    ctrl->recovery_count_required = 8;

    return GSR_STATUS_OK;
}

// Update the controller configuration and apply it to the hardware. 
gsr_status_t gsr_controller_set_config(gsr_controller_t *ctrl) {
    
    if (ctrl == NULL) return GSR_STATUS_INVALID_ARGUMENT;
    
    gsr_status_t ret;
    gsr_config_t *config = &ctrl->config;
    if (config->baseline_refresh_rate_Hz == 0U || config->phasic_refresh_rate_Hz == 0U || config->recovery_refresh_rate_Hz == 0U || config->idac_code == 0U) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    // setup the config of the iDAC Hardware registers
    ret = controller_set_current(ctrl, config->idac_code);
    if (ret != GSR_STATUS_OK) return ret;
    
    // setup the config of the VCO Hardware registers
    ret = controller_set_refresh(ctrl, ctrl->mode);
    if (ret != GSR_STATUS_OK) return ret;

    return ret;
}


// Initialize controller state variables and start the GSR front-end.
gsr_status_t gsr_controller_init(gsr_controller_t *ctrl) {

    if (ctrl == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    ctrl->mode = GSR_CTRL_MODE_INIT;
    gsr_sample_t init_sample = {
        .G_nS = 0U,
        .prev_G_nS = 0U,
        .vin_uV = 0U,
        .baseline_nS = 0U,
        .slope_nS = 0,
        .current_nA = gsr_current_from_idac_code_nA(ctrl->config.idac_code),
        .conductance_sensitivity_nS = 0U,
        .timestamp_ticks = 0U,
        .valid = false
    };
    ctrl->sample = init_sample;

    ctrl->recovery_counter = 0;
    ctrl->max_current_nA = GSR_MAX_CURRENT_NA;

    ctrl->initialized = false;

    ctrl->config.current_refresh_rate_Hz = ctrl->config.baseline_refresh_rate_Hz; // initialize the current refresh rate to the baseline rate
    return gsr_init(ctrl->config.channel, ctrl->config.current_refresh_rate_Hz, ctrl->config.idac_code);

}
gsr_status_t gsr_read_sample(gsr_controller_t *ctrl, uint32_t oversample_ratio) {
    uint32_t new_vin_uV = 0U;
    uint32_t new_conductance_nS = 0U;
    gsr_status_t ret;

    if (ctrl == NULL || oversample_ratio == 0U) return GSR_STATUS_INVALID_ARGUMENT;

    if (oversample_ratio > 1) {
        ret = gsr_get_conductance_oversampled(&new_conductance_nS, &new_vin_uV, oversample_ratio);
    } else {
        ret = gsr_get_conductance_nS(&new_conductance_nS, &new_vin_uV);
    }
    if (ret != GSR_STATUS_OK) { // OUT_OF_RANGE or NOT_INITIALIZED or MISSED_UPDATE or NO_NEW_SAMPLE
        // Need to implememt correct strategy in gcase OUT_OF_RANGE
        ctrl->sample.valid = false;
        return ret;
    }

    ctrl->sample.prev_G_nS = ctrl->sample.G_nS;
    ctrl->sample.G_nS = new_conductance_nS;
    ctrl->sample.vin_uV = new_vin_uV;
    ctrl->sample.current_nA = gsr_current_from_idac_code_nA(ctrl->config.idac_code);
    ctrl->sample.timestamp_ticks = 0U;
    ctrl->sample.conductance_sensitivity_nS = gsr_get_conductance_sensitivity_nS(new_conductance_nS, new_vin_uV, 
                                                                            ctrl->sample.current_nA, ctrl->config.current_refresh_rate_Hz);
    ctrl->sample.valid = true;
    
    if (!ctrl->initialized) {
        ctrl->sample.baseline_nS = ctrl->sample.G_nS;
        ctrl->sample.prev_G_nS = ctrl->sample.G_nS;
        ctrl->sample.slope_nS = 0;
        ctrl->mode = GSR_CTRL_MODE_BASELINE;
        ctrl->config.current_refresh_rate_Hz = ctrl->config.baseline_refresh_rate_Hz;
        ctrl->max_current_nA = max_current_for_conductance_nS(new_conductance_nS);
        ctrl->initialized = true;
        return GSR_STATUS_OK;
    }
    // Slope is expressed in nS/s by multiplying the sample difference by fs.
    ctrl->sample.slope_nS = ((int32_t)ctrl->sample.G_nS - (int32_t)ctrl->sample.prev_G_nS) * (int32_t)ctrl->config.current_refresh_rate_Hz;

    // Only baseline mode is allowed to slowly adapt the tonic reference.
    if (ctrl->mode == GSR_CTRL_MODE_BASELINE) {
        ctrl->sample.baseline_nS = calculate_baseline(ctrl->sample.baseline_nS, ctrl->sample.G_nS);
    }
    // update max current limit based on the new conductance measurement
    ctrl->max_current_nA = max_current_for_conductance_nS(new_conductance_nS);

    return ret;
}

const gsr_sample_t *gsr_get_last_sample(const gsr_controller_t *ctrl) {
    if (ctrl == NULL) return NULL;

    return &ctrl->sample;
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

    st = gsr_read_sample(ctrl, 1);
    if (st != GSR_STATUS_OK) return st;

    switch (ctrl->mode) {

        case GSR_CTRL_MODE_INIT:
            ctrl->mode = GSR_CTRL_MODE_BASELINE;
            break;

        case GSR_CTRL_MODE_BASELINE:
            //This line was removed since Esmail will be working on this.
            // retune_current_for_baseline(ctrl);

            if (event_detected(ctrl)) {
                st = controller_set_refresh(ctrl, GSR_CTRL_MODE_PHASIC);
                if (st != GSR_STATUS_OK) return st;

                ctrl->config.current_refresh_rate_Hz = ctrl->config.phasic_refresh_rate_Hz;
                ctrl->mode = GSR_CTRL_MODE_PHASIC;
                ctrl->recovery_counter = 0;
            }
            break;
        
        // Phasic mode increases sampling rate to better capture fast events.
        case GSR_CTRL_MODE_PHASIC:
            if (signal_settled(ctrl)) {
                st = controller_set_refresh(ctrl, GSR_CTRL_MODE_RECOVERY);
                if (st != GSR_STATUS_OK) return st;

                ctrl->config.current_refresh_rate_Hz = ctrl->config.recovery_refresh_rate_Hz;
                ctrl->mode = GSR_CTRL_MODE_RECOVERY;
                ctrl->recovery_counter = 0;
            }
            break;

        // Recovery mode uses an intermediate sampling rate until the signal is stable again.
        case GSR_CTRL_MODE_RECOVERY:
            if (!signal_settled(ctrl)) {
                st = controller_set_refresh(ctrl, GSR_CTRL_MODE_PHASIC);
                if (st != GSR_STATUS_OK) return st;

                ctrl->config.current_refresh_rate_Hz = ctrl->config.phasic_refresh_rate_Hz;
                ctrl->mode = GSR_CTRL_MODE_PHASIC;
                ctrl->recovery_counter = 0;
                break;
            }

            ctrl->recovery_counter++;

            if (ctrl->recovery_counter >= ctrl->recovery_count_required) {
                // retune_current_for_baseline(ctrl); // CHANGE: removed since Esmail will be working on this.

                st = controller_set_refresh(ctrl, GSR_CTRL_MODE_BASELINE);
                if (st != GSR_STATUS_OK) return st;

                ctrl->config.current_refresh_rate_Hz = ctrl->config.baseline_refresh_rate_Hz;
                ctrl->mode = GSR_CTRL_MODE_BASELINE;
                ctrl->recovery_counter = 0U;
            }
            break;

        default:
            return GSR_STATUS_NOT_INITIALIZED;
    }

    return GSR_STATUS_OK;
}