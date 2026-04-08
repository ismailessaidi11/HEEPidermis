// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: VCO_sdk.c
// Author: Ismail Essaidi
// Date: 08/04/2026
// Description: Implementation of the VCO SDK functions

#include "VCO_sdk.h"

#define TABLE_SIZE 25
#define SYS_FCLK_HZ 10000000

#define INTERPOLATE_FROM_LUT // comment if you want to read directly from LUT without interpolation (it gives less smooth results)

// TODO: check if 320mV is accurate + we can discard 820 mV point
uint32_t _table_Vin_uV[TABLE_SIZE] ={
    330000,
    340000, 360000, 380000, 400000, 420000, 
    440000, 460000, 480000, 500000, 520000, 
    540000, 560000, 580000, 600000, 620000, 
    640000, 660000, 680000, 700000, 720000, 
    740000, 760000, 780000, 800000
};
uint32_t _table_fosc_Hz[TABLE_SIZE] = {
    24000,
    26130, 31330, 37320, 45270, 55150,
    67270, 82680, 99870, 121190, 146020, 
    175270, 208990, 247770, 291780, 341260, 
    396650, 457900, 525140, 598560, 677660, 
    762750, 853760, 950200, 1051710
};

uint32_t _table_kvco_Hz_per_V[TABLE_SIZE] = {
    200000, 233333,   275000,  350000,
    450000, 550000,   650000,  800000,
    975000, 1175000,  1350000, 1575000,
    1825000, 2075000, 2400000, 2625000,
    2800000, 3075000, 3600000, 3900000,
    4025000, 4475000, 4750000, 4875000,
    5500000
};

/*
In this function we search for the value x based on fp(xp) LUT. 
The xp and fp arrays represent the known points of the function, while left and right are the values to return if x is out of bounds.
*/
uint32_t search_LUT(uint32_t x,
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
uint32_t linear_interp(uint32_t x,
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
        
    // 3. Linear Interpolation Formula
    // result = fp0 + (x_target - x0) * (fp1 - fp0) / (x1 - x0)
    uint32_t x0 = xp[low];
    uint32_t x1 = xp[high];
    // We multiply before dividing to keep precision.
    return fp[low] + (((fp[high] - fp[low]) * (x - x0)) / (x1 - x0));
}

vco_status_t vco_get_kvco_Hz_per_V(uint32_t vin_uV, uint32_t *kvco_Hz_per_V) {

    if (kvco_Hz_per_V == 0) {
        return VCO_STATUS_INVALID_ARGUMENT;
    }

    // left value is 0 because VCO stops oscillating below 330mV
    #ifdef INTERPOLATE_FROM_LUT
        *kvco_Hz_per_V = linear_interp(vin_uV, _table_Vin_uV, _table_kvco_Hz_per_V, 0, _table_kvco_Hz_per_V[TABLE_SIZE - 1]);
    #else 
        *kvco_Hz_per_V = search_LUT(vin_uV, _table_Vin_uV, _table_kvco_Hz_per_V, 0, _table_kvco_Hz_per_V[TABLE_SIZE - 1]);
    #endif

    return VCO_STATUS_OK;

}