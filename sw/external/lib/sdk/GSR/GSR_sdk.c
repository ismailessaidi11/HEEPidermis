// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_sdk.c
// Author: Omar Shibli & Ismail Essaidi
// Date: 08/04/2026
// Description: Implementation of the Galvanic Skin Response (GSR) SDK functions
#include "GSR_sdk.h"
#include "iDAC_ctrl.h"

#define VCO_SUPPLY_VOLTAGE_UV 800000

static uint32_t current_nA = 0;

/*
Initialize the GSR measurement chain.

The selected iDAC code sets the injected current, and the VCO SDK is then
initialized with the requested channel and refresh rate.
*/
gsr_status_t gsr_init(vco_channel_t channel, uint32_t refresh_rate_Hz, uint8_t idac_val){
    
    current_nA = 40*idac_val;
    iDACs_set_currents(idac_val, 0);
    vco_status_t st = vco_initialize(channel, refresh_rate_Hz);
    if (st == VCO_STATUS_OK) return GSR_STATUS_OK;
    if (st == VCO_STATUS_INVALID_ARGUMENT) return GSR_STATUS_INVALID_ARGUMENT;
    return GSR_STATUS_NOT_INITIALIZED;

}

// Update the injected current used by the analog front-end
void gsr_update_current(uint8_t idac_val){
    
    current_nA = 40*idac_val;
    iDACs_set_currents(idac_val, 0);
    
}

/*
Read one GSR sample in nS.
The function first reconstructs Vin from the VCO measurement, then computes
conductance.
*/
gsr_status_t gsr_get_conductance_nS(uint32_t *conductance_nS, uint32_t* vin_uV_ret) {

    if (conductance_nS == 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    // Get the latest reconstructed front-end voltage from the VCO layer.
    uint32_t vin_uV = 0;
    vco_status_t st = vco_get_Vin_uV(&vin_uV);

    if (st == VCO_STATUS_NO_NEW_SAMPLE) return GSR_STATUS_NO_NEW_SAMPLE;
    if (st == VCO_STATUS_MISSED_UPDATE) return GSR_STATUS_MISSED_UPDATE;
    if (st != VCO_STATUS_OK) return GSR_STATUS_NOT_INITIALIZED;

    // Optionally expose Vin to the caller for monitoring or controller use.
    if (vin_uV_ret != 0){
        *vin_uV_ret = vin_uV;
    }
    if (vin_uV >= VCO_SUPPLY_VOLTAGE_UV) {
        return GSR_STATUS_OUT_OF_RANGE;
    }
    uint32_t dv_uV = VCO_SUPPLY_VOLTAGE_UV - vin_uV;
    *conductance_nS = (uint32_t)(((uint64_t)current_nA * 1000000ULL) / dv_uV);

    return GSR_STATUS_OK;

}

/*

Compute an averaged conductance estimate from multiple valid samples.

This improves robustness to sample-to-sample noise at the cost of latency.
Only valid samples are accumulated; NO_NEW_SAMPLE results are ignored.

*/
gsr_status_t gsr_get_conductance_oversampled(uint32_t *conductance_nS, int oversample_ratio){
    
    if (conductance_nS == 0 || oversample_ratio <= 0) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    uint64_t acc = 0;
    int valid_samples = 0;

    while (valid_samples < oversample_ratio) {
        uint32_t sample_nS = 0;
        gsr_status_t ret = gsr_get_conductance_nS(&sample_nS, 0);

        if (ret == GSR_STATUS_OK) {
            acc += sample_nS;
            valid_samples++;
        }
        else if (ret == GSR_STATUS_NO_NEW_SAMPLE) {
            // No new refresh yet, keep waiting
            continue;
        }
        else {
            return ret;
        }
    }

    *conductance_nS = (uint32_t)(acc / (uint64_t)oversample_ratio);
    return GSR_STATUS_OK;

}