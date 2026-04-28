// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_sdk.h
// Author: Omar Shibli & Ismail Essaidi
// Date: 08/04/2026
/* Description: 
 * The GSR SDK owns measurement state and applies configs to the GSR front-end:
 * - reads Vin from the VCO SDK
 * - computes conductance, sensitivity (deltaG), and power
 * - adjusts sampling rate depending on whether the signal is in baseline tracking, phasic event capture, or recovery after an event
 * - tracks the active configuration (VCO channel, refresh rate, iDAC code) applies it  to the hardware
 * Note: Operating-point policy lives in GSR_op_controller.
*/

#ifndef GSR_CONTROLLER_H_
#define GSR_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>
#include "GSR_sdk.h"


// Operating modes of the adaptive controller.
typedef enum {
    GSR_CTRL_MODE_INIT = 0,
    GSR_CTRL_MODE_BASELINE,
    GSR_CTRL_MODE_PHASIC,
    GSR_CTRL_MODE_RECOVERY
} gsr_ctrl_mode_t;

/* Hardware configuration required by the measurement layer. */
typedef struct {
    vco_channel_t channel;       /* VCO path used to reconstruct Vin. */
    uint32_t baseline_refresh_rate_Hz;
    uint32_t phasic_refresh_rate_Hz;
    uint32_t recovery_refresh_rate_Hz;
    uint32_t current_refresh_rate_Hz;
    uint8_t idac_code;           /* Raw iDAC code used to set the injected current. */
} gsr_config_t;

/* One reconstructed GSR sample. */
typedef struct {
    uint32_t G_nS;          /* Conductance computed from current_nA and vin_uV. */
    uint32_t prev_G_nS;     /* Previous conductance value. */
    uint32_t vin_uV;        /* Reconstructed front-end voltage from the VCO readout. */
    uint32_t baseline_nS;
    int32_t slope_nS;
    
    uint32_t current_nA;         /* Injected current used for this sample's conductance computation. */
    uint32_t conductance_sensitivity_nS; /* Estimated conductance sensitivity (deltaG) around the current operation point, used for control decisions. */
    uint32_t timestamp_ticks;    /* Optional acquisition timestamp; 0 when no timer is connected. */
    // uint32_t power_nW;                   /* Estimated power consumption of the measurement, used for control decisions. */
    /*
     * True only after a successful VCO read and conductance conversion.
     * False means the sample must not be used for control/math; the numeric
     * fields may contain old values or placeholders from a failed read.
     */
    bool valid;
} gsr_sample_t;

//Controller state and configuration parameters.
typedef struct {
    gsr_ctrl_mode_t mode;
    gsr_config_t config;
    gsr_sample_t sample;

    uint32_t amplitude_threshold_nS;
    uint32_t slope_threshold_nS;
    uint32_t settle_threshold_nS;
    uint32_t recovery_count_required;
    uint32_t recovery_counter;
    uint32_t max_current_nA;     /* Highest injectable current supported by the iDAC model and the last measured conductance. */

    bool initialized;
} gsr_controller_t;


// Initialize the controller state and underlying GSR front-end.
gsr_status_t gsr_controller_init(gsr_controller_t *ctrl);

//Populate the controller with a default parameter set.
gsr_status_t gsr_set_default_settings(gsr_controller_t *ctrl);

// Update the controller configuration and apply it to the hardware.
gsr_status_t gsr_controller_set_config(gsr_controller_t *ctrl);

/* Read one sample (if M>=1) and store it in the controller. Or (if M>1) Average multiple valid samples. NO_NEW_SAMPLE is ignored while waiting. */
gsr_status_t gsr_read_sample(gsr_controller_t *ctrl, uint32_t M);

/* Return the last valid/attempted sample stored in the context. */
const gsr_sample_t *gsr_get_last_sample(const gsr_controller_t *ctrl);

//Execute one controller update step from the latest available sample.
gsr_status_t gsr_controller_step(gsr_controller_t *ctrl);

#endif /* GSR_CONTROLLER_H_ */