// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_sdk.h
// Author: Omar Shibli & Ismail Essaidi
// Date: 08/04/2026
// Description: Implementation of the Galvanic Skin Response (GSR) SDK functions

#ifndef GSR_SDK_H_
#define GSR_SDK_H_

#include <stdint.h>
#include <stdbool.h>
#include "VCO_sdk.h"

/*
This layer builds on top of the VCO SDK and converts reconstructed Vin
into skin conductance using the front-end current setting.
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

//Initialize the GSR front-end with the selected VCO channel, sampling rate, and current.
gsr_status_t gsr_init(vco_channel_t channel, uint32_t refresh_rate_Hz, uint8_t idac_val);

//Update the current used to calculate conductacnce based on the programming of the iDAC.
void gsr_update_current(uint8_t idac_val);

//Read one conductance sample in nS and optionally return the corresponding Vin.
gsr_status_t gsr_get_conductance_nS(uint32_t *conductance_nS, uint32_t *vin_uV_ret);

//Average multiple valid conductance samples to reduce noise.
gsr_status_t gsr_get_conductance_oversampled(uint32_t *conductance_nS, int oversample_ratio);

#endif /* GSR_SDK_H_ */