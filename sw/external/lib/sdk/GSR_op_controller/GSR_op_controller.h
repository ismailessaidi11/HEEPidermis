// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_op_controller.h
// Author: Ismail Essaidi
// Date: 08/04/2026
// Description: Abstraction layer for GSR operating-point requests.

#ifndef GSR_OPCTRL_H
#define GSR_OPCTRL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GSR_RANGE_NARROW = 0,
    GSR_RANGE_MEDIUM = 1,
    GSR_RANGE_WIDE = 2,
} gsr_range_t;

typedef enum {
    GSR_SENSITIVITY_LOW = 0,
    GSR_SENSITIVITY_MEDIUM = 1,
    GSR_SENSITIVITY_HIGH = 2,
} gsr_sensitivity_t;

typedef enum {
    GSR_POWER_LOW = 0,
    GSR_POWER_BALANCED = 1,
    GSR_POWER_PERFORMANCE = 2,
} gsr_power_t;

typedef struct {
    gsr_range_t range;
    gsr_sensitivity_t sensitivity;
    gsr_power_t power;
} gsr_op_request_t;

typedef struct {
    gsr_op_request_t request;
    uint16_t iref1_calibration;
    uint16_t iref2_calibration;
    uint8_t vref_calibration;
    uint8_t idac1_calibration;
    uint8_t idac2_calibration;
    uint8_t idac1_current;
    uint8_t idac2_current;
    uint32_t idac_refresh_cycles;
    uint32_t vco_refresh_cycles;
    bool enable_vcop;
    bool enable_vcon;
} gsr_operating_point_t;

bool gsr_opctrl_plan(const gsr_op_request_t *request,
                     gsr_operating_point_t *operating_point);

bool gsr_opctrl_apply(const gsr_operating_point_t *operating_point);

bool gsr_opctrl_request(const gsr_op_request_t *request,
                        gsr_operating_point_t *operating_point);

const gsr_operating_point_t *gsr_opctrl_get_active(void);

void gsr_opctrl_shutdown(void);

#endif  // GSR_OPCTRL_H
