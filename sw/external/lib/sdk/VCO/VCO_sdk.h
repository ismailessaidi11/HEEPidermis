#ifndef VCO_SDK_H_
#define VCO_SDK_H_

#include <stdint.h>
#include <stdbool.h>

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
    VCO_STATUS_INVALID_CONFIGURATION
} vco_status_t;

/*
Internal state used by the VCO SDK to reconstruct frequency from
successive counter readings.
 */
typedef struct {
    uint32_t        refresh_cycles;      // VCO refresh period in system cycles
    uint32_t        last_timestamp;      // timer_get_cycles() at previous read
    bool            has_prev;            // false until first valid sample
    vco_channel_t   channel;             // channel configuration
} vco_sdk_t;

// Estimate local VCO sensitivity K_VCO = df/dV around a given Vin.
vco_status_t vco_get_kvco_Hz_per_V(uint32_t vin_uV, uint32_t *kvco_Hz_per_V);

// Interpolate Vin from a VCO oscillation frequency using the calibration table.
uint32_t __interpolate_Vin_uV(uint32_t f_target);

// Initialize the VCO path and configure its refresh rate.
vco_status_t vco_initialize(vco_channel_t channel, uint32_t refresh_rate_Hz);

// Read the latest Vin value reconstructed from the VCO frequency.
vco_status_t vco_get_Vin_uV(uint32_t *vin_uV);

#endif /* VCO_SDK_H_ */