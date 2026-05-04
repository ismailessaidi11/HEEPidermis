// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: VCO_sdk.c
// Author: Omar Shibli & Ismail Essaidi
// Date: 08/04/2026
// Description: Implementation of the VCO SDK functions

#include "VCO_sdk.h"

#define PRINTF_IN_SIM  1
#define PRINTF_IN_FPGA 0
#define TARGET_SIM     1
#if TARGET_SIM && PRINTF_IN_SIM
    #define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#elif PRINTF_IN_FPGA && !TARGET_SIM
    #define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define PRINTF(...)
#endif

#define TABLE_SIZE 25
#define VCO_GAIN 1
#define VCO_ACCEL_RATIO 100 // The ratio by which the VCO is accelerated in simulation to allow faster testing. The refresh rate and integration rate are divided by this factor in simulation mode.
#define VCO_DECODER_PHASES 62u
#define VCO_READOUT_DELAY_CC 3u

// TODO: check if 320mV is accurate + we can discard 820 mV point
const uint32_t _table_Vin_uV[TABLE_SIZE] ={
    330000, 340000, 360000, 380000, 400000, 
    420000, 440000, 460000, 480000, 500000, 
    520000, 540000, 560000, 580000, 600000, 
    620000, 640000, 660000, 680000, 700000, 
    720000, 740000, 760000, 780000, 800000
};
const uint32_t _table_fosc_Hz[TABLE_SIZE] = {
    24000, 26130, 31330, 37320, 45270,
    55150, 67270, 82680, 99870, 121190,
    146020, 175270, 208990, 247770, 291780,
    341260, 396650, 457900, 525140, 598560, 
    677660, 762750, 853760, 950200, 1051710
};

const uint32_t _table_kvco_Hz_per_V[TABLE_SIZE] = {
    200000, 233333,   275000,  350000, 450000, 
    550000,   650000,  800000, 975000, 1175000,
    1350000, 1575000, 1825000, 2075000, 2400000, 
    2625000, 2800000, 3075000, 3600000, 3900000,
    4025000, 4475000, 4750000, 4875000, 5500000
};

#define SYS_FCLK_HZ 10000000
#define VCO_SUPPLY_VOLTAGE_UV    800000U

#define INTERPOLATE_FROM_LUT // comment if you want to read directly from LUT without interpolation (it gives less smooth results)

static uint32_t g_integration_rate_Hz = 0;
static vco_sdk_t vco_data;

/*  
This function initializes the VCO, it uses an enum to set the channel
used as either NONE, P Channel, N channel, or Pseudo Differential mode.
*/
vco_status_t vco_initialize(vco_channel_t channel, uint32_t integration_rate_Hz){
    
    //Check if valid refresh rate
    if (integration_rate_Hz == 0) {
        return VCO_STATUS_INVALID_ARGUMENT;
    }

    VCOp_enable(false);
    VCOn_enable(false);
    
    //Enable the used channel based on the specified input
    switch (channel)
    {
    case VCO_CHANNEL_NONE:
        break;
    case VCO_CHANNEL_P:
        VCOp_enable(true);
        break;
    case VCO_CHANNEL_N:
        VCOn_enable(true);
        break;
    case VCO_CHANNEL_DIFFERENTIAL:
        VCOp_enable(true);
        VCOn_enable(true);
        break;
    default:
        return VCO_STATUS_INVALID_CONFIGURATION;
    }

    // set the VCO refresh rate
    g_integration_rate_Hz = integration_rate_Hz;
    #if TARGET_SIM
        uint32_t refresh_rate_CC = (SYS_FCLK_HZ/(VCO_ACCEL_RATIO*integration_rate_Hz));
    #else
        uint32_t refresh_rate_CC = (SYS_FCLK_HZ/integration_rate_Hz);
    #endif
    VCO_set_refresh_rate(refresh_rate_CC);

    //initialize the VCO data 
    vco_data.refresh_cycles = refresh_rate_CC;
    vco_data.has_prev = 0;
    vco_data.last_counter_p = 0;
    vco_data.last_counter_n = 0;
    vco_data.last_timestamp = 0;
    vco_data.channel = channel;

    return VCO_STATUS_OK;
}

/*
This function sets the refresh rate of the VCO. It updates both the global variable and the hardware VCO register.
*/
vco_status_t vco_set_refresh_rate(uint32_t integration_rate_Hz) {
    if (integration_rate_Hz == 0) {
        return VCO_STATUS_INVALID_ARGUMENT;
    }

    #if TARGET_SIM
        uint32_t refresh_rate_CC = (SYS_FCLK_HZ/(VCO_ACCEL_RATIO*integration_rate_Hz));
    #else
        uint32_t refresh_rate_CC = (SYS_FCLK_HZ/integration_rate_Hz);
    #endif
    // update the global variables
    g_integration_rate_Hz = integration_rate_Hz;
    vco_data.refresh_cycles = refresh_rate_CC;

    // update the hardware register
    VCO_set_refresh_rate(refresh_rate_CC);
    VCO_trigger();
    
    vco_data.config_changed = true; // set the configuration changed flag since changing config biases timestamp that we use in vco_get_Vin_uV.
    return VCO_STATUS_OK;
}

/*
In this function we search for the value x based on fp(xp) LUT. 
The xp and fp arrays represent the known points of the function, while left and right are the values to return if x is out of bounds.
*/
static uint32_t search_LUT(uint32_t x,
    uint32_t *xp,
    uint32_t *fp,
                        uint32_t left, uint32_t right)
{
    // 1. Handle Out-of-Bound
    if (x < xp[0]) return left;
    if (x >= xp[TABLE_SIZE - 1]) return right;
    
    // 2. Binary Search to find the interval [low, high]
    uint8_t low = 0, high = TABLE_SIZE - 1;
    while (low < high - 1) {
        uint8_t mid = low + (high - low) / 2;
        if (xp[mid] < x) low = mid;
        else high = mid;
    }
    // 3. Return lower bound value. We could return high or low. // TODO check which one gives better results.
    return fp[low];
}

/*
In this function we perform a linear interpolation of the value x based on fp(xp) LUT. 
The xp and fp arrays represent the known points of the function, while left and right are the values to return if x is out of bounds.
*/
static uint32_t linear_interp(uint32_t x,
                        uint32_t *xp,
                        uint32_t *fp,
                        uint32_t left, uint32_t right)
{
    // 1. Handle Out-of-Bound
    if (x <= xp[0]) return left;
    if (x >= xp[TABLE_SIZE - 1]) return right;
    
    // 2. Binary Search to find the interval [low, high]
    uint8_t low = 0, high = TABLE_SIZE - 1;
    while (low < high - 1) {
        uint8_t mid = low + (high - low) / 2;
        if (xp[mid] < x) low = mid;
        else high = mid;
    }
        
    // 3. Linear Interpolation Formula
    // result = fp0 + (x_target - x0) * (fp1 - fp0) / (x1 - x0)
    uint32_t x0 = xp[low];
    uint32_t x1 = xp[high];
    // We multiply before dividing to keep precision.
    return fp[low] + (((fp[high] - fp[low]) * (x - x0)) / (x1 - x0));
}

// Interpolate Vin from a VCO oscillation frequency using the calibration table.
static uint32_t interpolate_Vin_uV(uint32_t f_target){
    return linear_interp(f_target, _table_fosc_Hz, _table_Vin_uV, _table_Vin_uV[0], _table_Vin_uV[TABLE_SIZE - 1]);
}

// Estimate local VCO sensitivity K_VCO = df/dV around a given Vin.
uint32_t vco_get_kvco_Hz_per_V(uint32_t vin_uV) {

    // left value is 0 because VCO stops oscillating below 330mV
    #ifdef INTERPOLATE_FROM_LUT
        return linear_interp(vin_uV, _table_Vin_uV, _table_kvco_Hz_per_V, 0, _table_kvco_Hz_per_V[TABLE_SIZE - 1]);
    #else 
        return search_LUT(vin_uV, _table_Vin_uV, _table_kvco_Hz_per_V, 0, _table_kvco_Hz_per_V[TABLE_SIZE - 1]);
    #endif
}

/*
This function return the frequency read from the counter of the VCO based 
on the setup that was initialized.
*/
vco_status_t vco_get_Vin_uV(uint32_t* vin_uV){

    //make sure the VCO is properly initialized
    if (vin_uV == 0){
        return VCO_STATUS_INVALID_ARGUMENT;
    }

    if (g_integration_rate_Hz == 0 || vco_data.channel == VCO_CHANNEL_NONE) {
        return VCO_STATUS_NOT_INITIALIZED;
    }

    uint32_t now = timer_get_cycles();
    uint32_t elapsed_cycles = now - vco_data.last_timestamp;

    uint32_t readout_delay = (vco_data.refresh_cycles > VCO_READOUT_DELAY_CC)
                           ? VCO_READOUT_DELAY_CC
                           : 0u;
    uint64_t sample_ready_cycles = (uint64_t)vco_data.refresh_cycles + readout_delay;
    uint64_t missed_ready_cycles =
        ((uint64_t)vco_data.refresh_cycles * 2u) + readout_delay;

    // check that before the other 
    if (vco_data.config_changed) {
        vco_data.last_timestamp = now; // reset timestamp because it was biased by config change
        elapsed_cycles = 0; // if the configuration has changed, we consider that we just got a new sample to allow the function to return the new value as soon as it's ready, and not wait for 2 refresh cycles which would be the case if we consider that we just read a sample, since the previous counter values are stale and might give wrong frequency readings until we get 2 new samples.
        vco_data.config_changed = false;
    }

    // Make sure the delayed refresh train has latched the decoder count.
    if ((uint64_t)elapsed_cycles < sample_ready_cycles) {
        return VCO_STATUS_NO_NEW_SAMPLE;   // no new refresh yet
    }

    if ((uint64_t)elapsed_cycles >= missed_ready_cycles) {
        vco_data.last_timestamp = now;
        vco_data.has_prev = true;
        return VCO_STATUS_MISSED_UPDATE;   // missed one or more updates
    }

    if (!vco_data.has_prev) {
        vco_data.last_timestamp += vco_data.refresh_cycles;
        vco_data.has_prev = true;
        return VCO_STATUS_NO_NEW_SAMPLE;   // discard the first partial interval
    }

    uint32_t decoder_count = VCO_get_count();
    uint32_t frequency_Hz = (uint32_t)(((uint64_t)decoder_count * g_integration_rate_Hz) / VCO_DECODER_PHASES);

    *vin_uV  = interpolate_Vin_uV(frequency_Hz);

    vco_data.last_timestamp += vco_data.refresh_cycles;

    return VCO_STATUS_OK;
}

/*
This function enables/disables the VCO and handles vco_data
*/
vco_status_t vco_enable(vco_channel_t channel, bool enable)
{
    switch (channel)
    {
    case VCO_CHANNEL_NONE:
        break;
    case VCO_CHANNEL_P:
        VCOp_enable(enable);
        break;
    case VCO_CHANNEL_N:
        VCOn_enable(enable);
        break;
    case VCO_CHANNEL_DIFFERENTIAL:
        VCOp_enable(enable);
        VCOn_enable(enable);
        break;
    default:
        return VCO_STATUS_INVALID_CONFIGURATION;
    }
    vco_data.config_changed = true; // set the configuration changed flag since we are disabling the VCO, this will make sure that when we enable it again, we don't use old counter values that might be stale and give wrong frequency readings until we get 2 new samples.
    return VCO_STATUS_OK;
}