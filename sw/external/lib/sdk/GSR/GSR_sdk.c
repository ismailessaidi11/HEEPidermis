// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_sdk.c
// Author: Omar Shibli & Ismail Essaidi
// Date: 08/04/2026
// Description: Measurement API for the Galvanic Skin Response (GSR) front-end.

#include "GSR_sdk.h"

#include <stddef.h>

#include "iDAC_ctrl.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))

static gsr_status_t gsr_status_from_vco(vco_status_t status) {
    if (status == VCO_STATUS_OK) {
        return GSR_STATUS_OK;
    }
    if (status == VCO_STATUS_NO_NEW_SAMPLE) {
        return GSR_STATUS_NO_NEW_SAMPLE;
    }
    if (status == VCO_STATUS_MISSED_UPDATE) {
        return GSR_STATUS_MISSED_UPDATE;
    }
    if (status == VCO_STATUS_INVALID_ARGUMENT) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }
    if (status == VCO_STATUS_OUT_OF_RANGE) {
        return GSR_STATUS_OUT_OF_RANGE;
    }
    return GSR_STATUS_NOT_INITIALIZED;
}

static uint32_t gsr_max_current_for_conductance_nS(uint32_t conductance_nS) {
    const uint32_t delta_v_min_uV = GSR_VCO_SUPPLY_VOLTAGE_UV - GSR_VIN_MIN_UV;
    uint32_t max_current_nA = (uint32_t)(((uint64_t)conductance_nS * delta_v_min_uV) / 1000000ULL);

    return min(max_current_nA, GSR_MAX_CURRENT_NA); 
}

static uint32_t gsr_get_conductance_nS(uint32_t current_nA, uint32_t vin_uV) { 
    const uint32_t dv_uV = GSR_VCO_SUPPLY_VOLTAGE_UV - vin_uV; // safe because division by zero is handled in the ranges handling of vco_get_Vin_uV function in vco_sdk.c
    return (uint32_t)(((uint64_t)current_nA * 1000000ULL) / dv_uV);
    
}

// TODO: Compute the frequency error of the VCO (based on allen deviation measurements)
static uint32_t gsr_get_frequency_error_Hz(uint32_t vin_uV, uint32_t refresh_rate_Hz, uint8_t variance)
{

    // We use a fixed value for the frequency error based on the Allen deviation measurements of the VCO. 
    #ifdef ADEV_VAR
        return 0; // not implemented yet
    #else
        return refresh_rate_Hz;
    #endif
}

// Compute the conductance sensitivity (delta G) of the VCO around a given Vin, based on the i_dc and refresh rate.
static uint32_t gsr_get_conductance_sensitivity_nS(uint32_t conductance_nS, uint32_t vin_uV, uint32_t current_nA, uint32_t refresh_rate_Hz)
{
    uint32_t frequency_error_Hz = gsr_get_frequency_error_Hz(vin_uV, refresh_rate_Hz, VCO_VARIANCE);
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

uint32_t gsr_current_from_idac_code_nA(uint8_t idac_code) {
    return (uint32_t)idac_code * GSR_IDAC_LSB_NA;
}

gsr_status_t gsr_set_config(gsr_context_t *ctx,
                              const gsr_measurement_config_t *config) {
    
    if (ctx == NULL || config == NULL || config->refresh_rate_Hz == 0U || config->idac_code == 0U) return GSR_STATUS_INVALID_ARGUMENT;
    
    gsr_status_t ret;

    ctx->config = *config;

    // setup the config of the iDAC Hardware registers
    ret = gsr_set_current(ctx, config->idac_code);
    if (ret != GSR_STATUS_OK) return ret;

    // setup the config of the VCO Hardware registers
    ret = gsr_status_from_vco(vco_set_refresh_rate(config->refresh_rate_Hz));
    if (ret != GSR_STATUS_OK) return ret;

    return ret;
}

gsr_status_t gsr_init(gsr_context_t *ctx,
                              const gsr_measurement_config_t *config) {
    
    if (ctx == NULL || config == NULL || config->refresh_rate_Hz == 0U || config->idac_code == 0U) return GSR_STATUS_INVALID_ARGUMENT;
    
    gsr_status_t ret;
    
    ctx->initialized = false;

    gsr_sample_t init_sample = {
        .vin_uV = 0U,
        .conductance_nS = 1U,
        .current_nA = gsr_current_from_idac_code_nA(config->idac_code),
        .timestamp_ticks = 0U,
        .conductance_sensitivity_nS = 0U,
        .valid = false
    };
    ctx->last_sample = init_sample;

    ret = gsr_set_config(ctx, config);
    if (ret != GSR_STATUS_OK) return ret;

    ret = gsr_status_from_vco(vco_initialize(config->channel, config->refresh_rate_Hz));
    if (ret != GSR_STATUS_OK) return ret;

    ctx->initialized = true;
    return GSR_STATUS_OK;
}

gsr_status_t gsr_set_current(gsr_context_t *ctx, uint8_t idac_code) {

    if (ctx == NULL || !ctx->initialized) return GSR_STATUS_INVALID_ARGUMENT;
    uint32_t current_nA = gsr_current_from_idac_code_nA(idac_code);

    // check validity of current range
    if (current_nA > ctx->limits.max_current_nA - GUARD_IDC_NA) { 
        return GSR_STATUS_OUT_OF_RANGE;
    }

    // current configuration 
    ctx->config.idac_code = idac_code;
    ctx->current_nA = current_nA;
    
    // After changing the current, the conductance measurement limit must be updated. (do I ?)
    // ctx->limits.min_conductance_nS = gsr_get_conductance_nS(ctx->current_nA, GSR_VIN_MIN_UV);

    // update the iDAC hardware registers with the new current
    iDACs_set_currents(idac_code, 0U);

    return GSR_STATUS_OK;
}

gsr_status_t gsr_read_sample(gsr_context_t *ctx, gsr_sample_t *sample) {
    uint32_t vin_uV = 0U;
    gsr_status_t ret;

    if (ctx == NULL || sample == NULL) return GSR_STATUS_INVALID_ARGUMENT;

    if (!ctx->initialized) return GSR_STATUS_NOT_INITIALIZED;

    ret = gsr_status_from_vco(vco_get_Vin_uV(&vin_uV));
    if (ret != GSR_STATUS_OK) { // OUT_OF_RANGE or NOT_INITIALIZED or MISSED_UPDATE or NO_NEW_SAMPLE
        ctx->last_sample.valid = false;
        *sample = ctx->last_sample; // return the last sample with valid=false to indicate the read failure, and the caller can check the status for details
        return ret;
    }

    sample->vin_uV = vin_uV;
    sample->current_nA = ctx->current_nA;
    sample->timestamp_ticks = 0U;

    sample->conductance_nS = gsr_get_conductance_nS(sample->current_nA, sample->vin_uV);

    sample->conductance_sensitivity_nS = gsr_get_conductance_sensitivity_nS(sample->conductance_nS, sample->vin_uV, 
                                                                            sample->current_nA, ctx->config.refresh_rate_Hz);
    sample->valid = true;

    // update max current limit based on the new conductance measurement
    ctx->limits.max_current_nA = gsr_max_current_for_conductance_nS(sample->conductance_nS);

    ctx->last_sample = *sample;
    return ret;
}

gsr_status_t gsr_read_oversampled(gsr_context_t *ctx,
                                          gsr_sample_t *sample,
                                          uint32_t oversample_ratio) {
    gsr_sample_t latest = {0};
    uint64_t conductance_acc = 0U;
    uint64_t vin_acc = 0U;
    uint32_t valid_samples = 0U;

    if (ctx == NULL || sample == NULL || oversample_ratio == 0U) {
        return GSR_STATUS_INVALID_ARGUMENT;
    }

    while (valid_samples < oversample_ratio) {
        gsr_status_t status = gsr_read_sample(ctx, &latest);

        if (status == GSR_STATUS_OK) {
            conductance_acc += latest.conductance_nS;
            vin_acc += latest.vin_uV;
            valid_samples++;
        } else if (status == GSR_STATUS_NO_NEW_SAMPLE) {
            continue;
        } else {
            ctx->last_sample.valid = false;
            *sample = ctx->last_sample;
            return status;
        }
    }

    *sample = latest;
    sample->conductance_nS = (uint32_t)(conductance_acc / oversample_ratio);
    sample->vin_uV = (uint32_t)(vin_acc / oversample_ratio);
    sample->valid = true;

    ctx->limits.max_current_nA = gsr_max_current_for_conductance_nS(sample->conductance_nS);
    ctx->last_sample = *sample;

    return GSR_STATUS_OK;
}

const gsr_sample_t *gsr_get_last_sample(const gsr_context_t *ctx) {
    if (ctx == NULL) {
        return NULL;
    }

    return &ctx->last_sample;
}

// BACKWARD COMPATIBILITY !!! 
// gsr_status_t gsr_init(vco_channel_t channel, uint32_t refresh_rate_Hz, uint8_t idac_val) {
//     gsr_measurement_config_t config = {
//         .channel = channel,
//         .refresh_rate_Hz = refresh_rate_Hz,
//         .idac_code = idac_val,
//     };

//     return gsr_init(&g_default_context, &config);
// }

// void gsr_update_current(uint8_t idac_val) {
//     (void)gsr_set_current(&g_default_context, idac_val);
// }

// gsr_status_t gsr_get_conductance_nS(uint32_t *conductance_nS, uint32_t *vin_uV_ret) {
//     gsr_sample_t sample = {0};
//     gsr_status_t status;

//     if (conductance_nS == NULL) {
//         return GSR_STATUS_INVALID_ARGUMENT;
//     }

//     status = gsr_read_sample(&g_default_context, &sample);
//     if (status != GSR_STATUS_OK) {
//         return status;
//     }

//     *conductance_nS = sample.conductance_nS;
//     if (vin_uV_ret != NULL) {
//         *vin_uV_ret = sample.vin_uV;
//     }

//     return GSR_STATUS_OK;
// }

// gsr_status_t gsr_get_conductance_oversampled(uint32_t *conductance_nS, int oversample_ratio) {
//     gsr_sample_t sample = {0};
//     gsr_status_t status;

//     if (conductance_nS == NULL || oversample_ratio <= 0) {
//         return GSR_STATUS_INVALID_ARGUMENT;
//     }

//     status = gsr_read_oversampled(&g_default_context, &sample, (uint32_t)oversample_ratio);
//     if (status != GSR_STATUS_OK) {
//         return status;
//     }

//     *conductance_nS = sample.conductance_nS;
//     return GSR_STATUS_OK;
// }
