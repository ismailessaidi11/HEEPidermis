#ifndef GSR_SDK_H_
#define GSR_SDK_H_

#include <stdint.h>
#include <stdbool.h>
#include "VCO_sdk.h"
#include "VCO_dlc_sdk.h"

/*
This layer builds on top of the VCO SDK and converts reconstructed Vin
into skin conductance using the front-end current setting.

Define GSR_USE_VCO_DLC at compile time to use the dLC-based backend instead
of the direct VCO polling backend. The public API is identical except that
gsr_init requires an additional gsr_dlc_config_t parameter.
*/

/* Status codes returned by the GSR SDK functions. */
typedef enum {
    GSR_STATUS_OK = 0,
    GSR_STATUS_NO_NEW_SAMPLE,
    GSR_STATUS_MISSED_UPDATE,
    GSR_STATUS_NOT_INITIALIZED,
    GSR_STATUS_INVALID_ARGUMENT,
    GSR_STATUS_OUT_OF_RANGE
} gsr_status_t;

typedef struct {
    const dlc_config_t *dlc_cfg;
    uint8_t            *results_buf;
    uint16_t            buf_size;
    uint32_t            input_samples;
} gsr_dlc_config_t;

//Initialize the GSR front-end with the selected VCO channel, sampling rate, current, and dLC config.
gsr_status_t gsr_init_dlc(vco_channel_t channel, uint32_t refresh_rate_Hz, uint8_t idac_val, const gsr_dlc_config_t *dlc_cfg);

//Initialize the GSR front-end with the selected VCO channel, sampling rate, and current.
gsr_status_t gsr_init(vco_channel_t channel, uint32_t refresh_rate_Hz, uint8_t idac_val);

//Update the current used to calculate conductacnce based on the programming of the iDAC.
void gsr_update_current(uint8_t idac_val);

// Convert an iDAC code to injected current using the present front-end model.
uint32_t gsr_current_from_idac_code_nA(uint8_t idac_code);

//Read one conductance sample in nS and optionally return the corresponding Vin.
gsr_status_t gsr_get_conductance_nS(uint32_t *conductance_nS, uint32_t *vin_uV_ret);

//Average multiple valid conductance samples to reduce noise.
gsr_status_t gsr_get_conductance_oversampled(uint32_t *conductance_nS, uint32_t *vin_uV_ret, int oversample_ratio);

#endif /* GSR_SDK_H_ */
