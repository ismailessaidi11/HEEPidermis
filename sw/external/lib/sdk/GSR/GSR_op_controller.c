// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_op_controller.c
// Author: Ismail Essaidi
// Date: 08/04/2026
// Description: Abstraction layer for GSR operating-point requests.

#include "GSR_op_controller.h"

#include <stddef.h>

#include "REFs_ctrl.h"
#include "VCO_decoder.h"
#include "iDAC_ctrl.h"

static gsr_operating_point_t g_active_operating_point;
static bool g_has_active_operating_point = false;

static bool gsr_opctrl_is_valid_request(const gsr_op_request_t *request) {
    if (request == NULL) {
        return false;
    }

    if (request->range > GSR_RANGE_WIDE) {
        return false;
    }

    if (request->sensitivity > GSR_SENSITIVITY_HIGH) {
        return false;
    }

    if (request->power > GSR_POWER_PERFORMANCE) {
        return false;
    }

    return true;
}

bool gsr_opctrl_plan(const gsr_op_request_t *request,
                     gsr_operating_point_t *operating_point) {
    static const uint16_t k_iref_calibration[3] = {384u, 512u, 640u};
    static const uint8_t k_vref_calibration[3] = {10u, 15u, 20u};
    static const uint8_t k_idac1_current[3] = {60u, 100u, 140u};
    static const uint8_t k_idac2_current[3] = {30u, 50u, 70u};
    static const uint32_t k_idac_refresh_cycles[3] = {4000u, 2000u, 1000u};
    static const uint32_t k_vco_refresh_cycles[3] = {200u, 100u, 50u};
    static const bool k_enable_vcon[3] = {false, false, true};

    if (!gsr_opctrl_is_valid_request(request) || operating_point == NULL) {
        return false;
    }

    operating_point->request = *request;
    operating_point->iref1_calibration = k_iref_calibration[request->sensitivity];
    operating_point->iref2_calibration = k_iref_calibration[request->sensitivity];
    operating_point->vref_calibration = k_vref_calibration[request->sensitivity];
    operating_point->idac1_calibration = 16u;
    operating_point->idac2_calibration = 16u;
    operating_point->idac1_current = k_idac1_current[request->range];
    operating_point->idac2_current = k_idac2_current[request->range];
    operating_point->idac_refresh_cycles = k_idac_refresh_cycles[request->power];
    operating_point->vco_refresh_cycles = k_vco_refresh_cycles[request->power];
    operating_point->enable_vcop = true;
    operating_point->enable_vcon = k_enable_vcon[request->power];

    return true;
}

bool gsr_opctrl_apply(const gsr_operating_point_t *operating_point) {
    if (operating_point == NULL) {
        return false;
    }

    REFs_calibrate(operating_point->iref1_calibration, IREF1);
    REFs_calibrate(operating_point->iref2_calibration, IREF2);
    REFs_calibrate(operating_point->vref_calibration, VREF);

    iDACs_enable(true, true);
    iDAC1_calibrate(operating_point->idac1_calibration);
    iDAC2_calibrate(operating_point->idac2_calibration);
    iDACs_set_refresh_rate(operating_point->idac_refresh_cycles);
    iDACs_set_currents(operating_point->idac1_current, operating_point->idac2_current);

    VCOp_enable(operating_point->enable_vcop);
    VCOn_enable(operating_point->enable_vcon);
    VCO_set_refresh_rate(operating_point->vco_refresh_cycles);

    g_active_operating_point = *operating_point;
    g_has_active_operating_point = true;

    return true;
}

bool gsr_opctrl_request(const gsr_op_request_t *request,
                        gsr_operating_point_t *operating_point) {
    gsr_operating_point_t planned_operating_point;

    if (!gsr_opctrl_plan(request, &planned_operating_point)) {
        return false;
    }

    if (!gsr_opctrl_apply(&planned_operating_point)) {
        return false;
    }

    if (operating_point != NULL) {
        *operating_point = planned_operating_point;
    }

    return true;
}

const gsr_operating_point_t *gsr_opctrl_get_active(void) {
    if (!g_has_active_operating_point) {
        return NULL;
    }

    return &g_active_operating_point;
}

void gsr_opctrl_shutdown(void) {
    VCOn_enable(false);
    VCOp_enable(false);
    iDACs_enable(false, false);
    g_has_active_operating_point = false;
}
