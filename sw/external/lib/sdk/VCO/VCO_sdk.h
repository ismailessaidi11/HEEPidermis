// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: VCO_sdk.c
// Author: Omar Shibli & Ismail Essaidi
// Date: 08/04/2026
// Description: Implementation of the VCO SDK functions


#ifndef VCO_SDK_H_
#define VCO_SDK_H_

#include <stdint.h>
#include <stdbool.h>
#include "VCO_decoder.h"
#include "timer_sdk.h"
/*
This file provides the low-level interface to the VCO-based front-end.
It is used to configure the VCO channel, counter updates, and the conversion
of the counter to voltage.
*/

//Selects which VCO path is used for measurement.
typedef enum {
    VCO_CHANNEL_NONE           = 0b00,
    VCO_CHANNEL_P              = 0b01,
    VCO_CHANNEL_N              = 0b10,
    VCO_CHANNEL_DIFFERENTIAL   = 0b11
} vco_channel_t;

//Status codes returned by the VCO SDK functions.
typedef enum {
    VCO_STATUS_OK = 0,
    VCO_STATUS_NO_NEW_SAMPLE,
    VCO_STATUS_MISSED_UPDATE,
    VCO_STATUS_NOT_INITIALIZED,
    VCO_STATUS_INVALID_ARGUMENT,
    VCO_STATUS_INVALID_CONFIGURATION,
    VCO_STATUS_OUT_OF_RANGE
} vco_status_t;

/*
Internal state used by the VCO SDK to reconstruct frequency from
successive counter readings.
 */
typedef struct {
    uint32_t        integration_time_CC; // VCO integration period in system cycles (refresh between 2 measurements)
    uint32_t        off_cycles;          // sleep window in system cycles
    uint32_t        last_counter_p;      // previous coarse/count value
    uint32_t        last_counter_n;      // previous coarse/count value for second channel if running in differential mode
    uint32_t        last_timestamp;      // timer_get_cycles() at previous read
    uint8_t         duty_cycle_code;     // D in [1,255]
    bool            has_prev;            // false until first valid sample
    bool            config_changed;      // true if the configuration has changed or VCO got disabled
    // bool            sample_ready;        // true when one ON window completed and can be consumed by vco_get_Vin_uV
    bool            vco_enabled;         // true when the selected VCO channel is currently enabled
    vco_channel_t   channel;             // channel configuration
} vco_sdk_t;

// Initialize the VCO path and configure its refresh rate.
vco_status_t vco_initialize(vco_channel_t channel, uint32_t integration_rate_Hz);

// sets new refresh rate for the VCO
vco_status_t vco_set_refresh_rate(uint32_t integration_rate_Hz);

// Estimate local VCO sensitivity K_VCO = df/dV around a given Vin.
uint32_t vco_get_kvco_Hz_per_V(uint32_t vin_uV);

// Read the latest Vin value reconstructed from the VCO frequency.
vco_status_t vco_get_Vin_uV(uint32_t *vin_uV);

// Enable or disable the VCO.
vco_status_t vco_enable(vco_channel_t channel, bool enable);

// applies duty cycling to the VCO by setting its duty cycle D (between 0 and 255 representing D=1)
vco_status_t vco_duty_cycle(vco_channel_t channel, uint8_t D);

// Indicates whether a duty-cycled ON window completed and a fresh sample may be read.
// bool vco_sample_ready(void);

// Advance the duty-cycle state machine from the timer IRQ handler.
void vco_handle_timer_irq(void);

#endif /* VCO_SDK_H_ */
