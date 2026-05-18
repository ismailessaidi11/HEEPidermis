#include "config_profiles.h"

// A lower idc provides a wider range of skin conductance measurement
const gsr_range_profile_t k_range_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .idac_code = 30U }, // = 1.2 uA (low range)
    { .idac_code = 20U }, // = 0.8 uA
    { .idac_code = 10U  },  // = 0.4 uA (high range)
};

const gsr_range_2_profile_t k_range_2_profiles[GSR_OPCTRL_PROFILE_COUNT] = { // we aim for an i_dc close to the max because that is our implicit low power condition  
    { .range = 1U }, // = i_dc,max - 1 * GUARD_IDC_NA (low range) 
    { .range = 2U }, // = i_dc,max - 2 * GUARD_IDC_NA (mid range)
    { .range = 4U  },  // = i_dc,max - 4 * GUARD_IDC_NA (high range)
};

// For now: resolution -> higher refresh rate (higher OSR) -> mode corresponding to higher refresh rate in the controller config
const gsr_resolution_profile_t k_resolution_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .mode = GSR_CTRL_MODE_BASELINE }, // lowest OSR (or refresh rate)
    { .mode = GSR_CTRL_MODE_RECOVERY }, // medium OSR 
    { .mode = GSR_CTRL_MODE_PHASIC },   // highest OSR 
};

const gsr_refresh_profile_t k_refresh_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .refresh_rate_Hz = 2U },  // low refresh rate for baseline tracking
    { .refresh_rate_Hz = 5U },  // medium refresh rate for recovery tracking
    { .refresh_rate_Hz = 10U }, // high refresh rate for phasic event capture
};

const gsr_power_profile_t k_power_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .duty_cycle_code = 4U }, // The VCO is ON 1/4 of the time
    { .duty_cycle_code = 2U }, // The VCO is ON 1/2 of the time
    { .duty_cycle_code = 1U }, // The VCO is always ON 
};
