// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_sdk.h
// Author: Omar Shibli & Ismail Essaidi
// Date: 08/04/2026
/* Description: 
 * The GSR SDK owns measurement state and applies configs to hardware:
 * - reads Vin from the VCO SDK
 * - computes conductance, sensitivity (deltaG), and power
 * - tracks the active configuration (VCO channel, refresh rate, iDAC code) applies it  to the hardware
 * Note: Operating-point policy lives in GSR_op_controller.
 */

#ifndef GSR_SDK_H_
#define GSR_SDK_H_

#include <stdbool.h>
#include <stdint.h>

#include "VCO_sdk.h"

#define GSR_IDAC_LSB_NA              40U
#define GSR_IDAC_MAX_CODE            255U
#define GSR_MAX_CURRENT_NA           ((uint32_t)GSR_IDAC_MAX_CODE * GSR_IDAC_LSB_NA)
#define GSR_VCO_SUPPLY_VOLTAGE_UV    800000U
#define GSR_VIN_MIN_UV               330000U
#define GUARD_IDC_NA                 500U // guard i_dc to prevent going out of range in the next conductance measurement; corresponds to 1 uS of change in conductance
#define VCO_VARIANCE                 3U // variance in the VCO frequency-to-voltage conversion, used for sensitivity estimation

/* Status codes returned by the GSR SDK functions. */
typedef enum {
    GSR_STATUS_OK = 0,
    GSR_STATUS_NO_NEW_SAMPLE,
    GSR_STATUS_MISSED_UPDATE,
    GSR_STATUS_NOT_INITIALIZED,
    GSR_STATUS_INVALID_ARGUMENT,
    GSR_STATUS_OUT_OF_RANGE
} gsr_status_t;

/* Hardware configuration required by the measurement layer. */
typedef struct {
    vco_channel_t channel;       /* VCO path used to reconstruct Vin. */
    uint32_t integration_rate_Hz;    /* VCO sampling rate requested from the VCO SDK. */
    uint8_t idac_code;           /* Raw iDAC code used to set the injected current. */
} gsr_config_t;

/* Derived measurement limits for the active current setting. */
typedef struct {
    // uint32_t min_conductance_nS; /* Lowest conductance measurable with the active current and Vin range. */
    uint32_t max_current_nA;     /* Highest injectable current supported by the iDAC model and the last measured conductance. */
} gsr_limits_t;

/* One reconstructed GSR sample. */
typedef struct {
    uint32_t vin_uV;             /* Reconstructed front-end voltage from the VCO readout. */
    uint32_t conductance_nS;     /* Conductance computed from current_nA and vin_uV. */
    uint32_t current_nA;         /* Injected current used for this sample's conductance computation. */
    uint32_t timestamp_ticks;    /* Optional acquisition timestamp; 0 when no timer is connected. */
    uint32_t conductance_sensitivity_nS; /* Estimated conductance sensitivity (deltaG) around the current operation point, used for control decisions. */
    // uint32_t power_nW;                   /* Estimated power consumption of the measurement, used for control decisions. */
    /*
     * True only after a successful VCO read and conductance conversion.
     * False means the sample must not be used for control/math; the numeric
     * fields may contain old values or placeholders from a failed read.
     */
    bool valid;
} gsr_sample_t;

/* SDK context. */
typedef struct {
    gsr_config_t config; /* Active VCO/iDAC control values */
    gsr_limits_t limits; /* Limits derived from the active operation point. */
    gsr_sample_t last_sample;        /* Most recent sample returned by the measurement. */
    uint32_t current_nA;             /* Cached current converted from config.idac_code. */
    bool initialized;                /* True after the VCO/iDAC measurement chain is configured. */
} gsr_context_t;

/* Convert an iDAC code to injected current using the present front-end model. */
uint32_t gsr_current_from_idac_code_nA(uint8_t idac_code);

/* Update only the current used by the measurement conversion and iDAC block. */
gsr_status_t gsr_set_current(gsr_context_t *ctx, uint8_t idac_code);

/* Synchronize gsr_context with the new VCO/iDAC config, then apply it to the hardware (iDAC and VCO) */
gsr_status_t gsr_set_config(gsr_context_t *ctx,
                              const gsr_config_t *config);

/* Initialize the measurement chain with the specified VCO/iDAC configuration. */
gsr_status_t gsr_init(gsr_context_t *ctx,
                              const gsr_config_t *config);

/* Read one sample and store it in the context. */
gsr_status_t gsr_read_sample(gsr_context_t *ctx, gsr_sample_t *sample);

/* Average multiple valid samples. NO_NEW_SAMPLE is ignored while waiting. */
gsr_status_t gsr_read_oversampled(gsr_context_t *ctx,
                                          gsr_sample_t *sample,
                                          uint32_t oversample_ratio);

/* Return the last valid/attempted sample stored in the context. */
const gsr_sample_t *gsr_get_last_sample(const gsr_context_t *ctx);

/* Compatibility wrappers using a default static context. */
// gsr_status_t gsr_init(vco_channel_t channel, uint32_t integration_rate_Hz, uint8_t idac_val);
// void gsr_update_current(uint8_t idac_val);
// gsr_status_t gsr_get_conductance_nS(uint32_t *conductance_nS, uint32_t *vin_uV_ret);
// gsr_status_t gsr_get_conductance_oversampled(uint32_t *conductance_nS, int oversample_ratio);

#endif /* GSR_SDK_H_ */
