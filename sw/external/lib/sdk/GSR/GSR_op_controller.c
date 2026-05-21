// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_op_controller.c
// Author: Ismail Essaidi
// Date: 08/04/2026
// Description: Operating-point request layer for the GSR front-end.

#include "GSR_op_controller.h"

#include <stddef.h>
#define GSR_IDAC_LSB_NA              40U
#define GUARD_IDC_NA                 (GSR_IDAC_LSB_NA * 3) // guard i_dc to prevent going out of range in the next conductance measurement; 12nA corresponds to 255 nS of change in conductance
#define GSR_IDAC_MAX_CODE            255U

static bool gsr_opctrl_is_valid_request(const gsr_op_request_t *request) {
    if (request == NULL) {
        return false;
    }

    return (request->range <= HIGH) &&
           (request->resolution <= HIGH) &&
           (request->power <= HIGH);
}

static gsr_opctrl_status_t gsr_opctrl_status_from_gsr(gsr_status_t status) {
    if (status == GSR_STATUS_OK) {
        return GSR_OPCTRL_OK;
    }
    if (status == GSR_STATUS_INVALID_ARGUMENT) {
        return GSR_OPCTRL_INVALID_ARGUMENT;
    }
    if (status == GSR_STATUS_NOT_INITIALIZED) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }
    if (status == GSR_STATUS_OVERFLOW) { 
        return GSR_OPCTRL_MEASUREMENT_OVERFLOW;
    }
    if (status == GSR_STATUS_UNDERFLOW) { 
        return GSR_OPCTRL_MEASUREMENT_UNDERFLOW;
    }

    return GSR_OPCTRL_MEASUREMENT_ERROR; // NO_NEW_SAMPLE or MISSED_UPDATE 
}

gsr_opctrl_status_t gsr_opctrl_init(gsr_op_controller_t *ctrl,
                                    gsr_controller_t *controller) {
    if (ctrl == NULL || controller == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    ctrl->controller = controller;
    ctrl->current_request = (gsr_op_request_t){
        .range = LOW,
        .resolution = LOW,
        .power = HIGH
    };
    ctrl->has_valid_op = false;
    ctrl->initialized = true;

    return GSR_OPCTRL_OK;
}

static bool range_to_idac_code(const gsr_op_controller_t *ctrl,
                               request_levels_t range,
                               uint8_t *idac_code)
{
    uint32_t max_current_nA;
    uint32_t target_current_nA;
    uint32_t target_code;
    uint32_t low_target_nA;
    uint32_t medium_target_nA;
    uint32_t high_target_nA;

    if (ctrl == NULL || idac_code == NULL || ctrl->controller == NULL) {
        return false;
    }

    if ((uint32_t)range >= GSR_OPCTRL_PROFILE_COUNT) {
        return false;
    }

    max_current_nA = ctrl->controller->max_current_nA;
   
    if (max_current_nA <= GUARD_IDC_NA) {
        return false;
    }

    low_target_nA = max_current_nA - GUARD_IDC_NA;

    /* 1/4 away from i_dc,max => keep 3/4 of i_dc,max. */
    medium_target_nA = max_current_nA - (max_current_nA >> 2);

    /* 1/2 away from i_dc,max => keep 1/2 of i_dc,max. */
    high_target_nA = max_current_nA >> 1;

    /* LOW >= MEDIUM >= HIGH. */
    if (medium_target_nA > low_target_nA) {
        medium_target_nA = low_target_nA;
    }

    if (high_target_nA > medium_target_nA) {
        high_target_nA = medium_target_nA;
    }

    switch (range) {
    case LOW:
        target_current_nA = low_target_nA;
        break;
    case MEDIUM:
        target_current_nA = medium_target_nA;
        break;
    case HIGH:
        target_current_nA = high_target_nA;
        break;
    default:
        return false;
    }

    target_code = target_current_nA / GSR_IDAC_LSB_NA;

    if (target_code == 0U) {
        return false;
    }

    if (target_code > GSR_IDAC_MAX_CODE) {
        target_code = GSR_IDAC_MAX_CODE;
    }

    *idac_code = (uint8_t)target_code;
    return true;
}

static gsr_opctrl_status_t controller_adjust_range_if_needed(
    gsr_op_controller_t *ctrl,
    gsr_controller_t *operating_point)
{
    uint8_t target_code;
    int32_t range;

    if (ctrl == NULL || operating_point == NULL || ctrl->controller == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    for (range = (int32_t)ctrl->current_request.range; range >= (int32_t)LOW; range--) {
        if (range_to_idac_code(ctrl, (request_levels_t)range, &target_code)) {
            operating_point->config.idac_code = target_code;

            /* Store the actually achieved range request. */
            ctrl->current_request.range = (request_levels_t)range;

            return GSR_OPCTRL_OK;
        }
    }

    return GSR_OPCTRL_UNSATISFIABLE;
}


static gsr_opctrl_status_t gsr_opctrl_apply_current(
    gsr_op_controller_t *ctrl,
    const gsr_controller_t *operating_point)
{
    gsr_status_t status;

    if (ctrl == NULL || operating_point == NULL) return GSR_OPCTRL_INVALID_REQUEST;
    if (!ctrl->initialized || ctrl->controller == NULL) return GSR_OPCTRL_NOT_INITIALIZED;

    status = gsr_controller_set_current(
        ctrl->controller,
        operating_point->config.idac_code
    );

    return gsr_opctrl_status_from_gsr(status);
}

/*
 * Preventively retune the injected current after a valid conductance sample.
 *
 * The latest sample updates ctrl->controller->max_current_nA, which estimates
 * the largest i_dc that can be used without pushing the VCO outside its valid
 * operating region. This function computes a range-dependent target current
 * below that limit, then applies only the iDAC/current change.
 *
 * This is a normal-operation guard-margin adjustment, not an emergency recovery
 * path. Directional out-of-range events are handled separately with gsr_opctrl_recover_underflow and gsr_opctrl_recover_overflow
 */
gsr_opctrl_status_t gsr_opctrl_adjust_range(gsr_op_controller_t *ctrl) {
    gsr_opctrl_status_t status;
    gsr_controller_t operating_point;

    if (ctrl == NULL || ctrl->controller == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    operating_point = *(ctrl->controller);

    status = controller_adjust_range_if_needed(ctrl, &operating_point);
    if (status != GSR_OPCTRL_OK) {
        return status;
    }

    return gsr_opctrl_apply_current(ctrl, &operating_point);
}

/* Translate an application request into concrete controller configuration fields with preconfigured profiles 
* (TODO: will be replaced with dynamic profiling) 
*/
gsr_opctrl_status_t gsr_opctrl_plan(const gsr_op_request_t *request,
                                    gsr_controller_t *operating_point) {
    if (!gsr_opctrl_is_valid_request(request) || operating_point == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    /* 
    * resolve the request into concrete config fields using the preconfigured profiles in config_profiles.h 
    */
    operating_point->config.channel = VCO_CHANNEL_P;
    // Initial static range profile. This may be overwritten by dynamic range adjustment.
    operating_point->config.idac_code = k_range_profiles[(uint32_t)request->range].idac_code;
    /*
    * We want to control refresh rate because it impacts resolution. 
    * However the way it is implemented now in GSR_controller is by changing modes (which makes sense but is a bit rigid)
    * So to change refresh rate we select the corresponding mode.
    * NOTE 1: In GSR_controller we should decouple refresh rate and modes because what if we want high refresh rates for baseline? 
    * NOTE 2: preconfigured values are in config_profiles.h.
    * TLDR: For now: resolution -> refresh rate (through OSR) -> mode with higher refresh rate in the controller config
    */
    operating_point->mode = k_resolution_profiles[(uint32_t)request->resolution].mode;

    operating_point->config.baseline_refresh_rate_Hz = k_refresh_profiles[0].refresh_rate_Hz;
    operating_point->config.recovery_refresh_rate_Hz = k_refresh_profiles[1].refresh_rate_Hz;
    operating_point->config.phasic_refresh_rate_Hz =  k_refresh_profiles[2].refresh_rate_Hz;

    // placeholder for now because duty cycling the VCO is not implemented yet.
    operating_point->config.duty_cycle_code = k_power_profiles[(uint32_t)request->power].duty_cycle_code;
    
    return GSR_OPCTRL_OK;
}

/* Apply an already planned operating point through the GSR controller. */
gsr_opctrl_status_t gsr_opctrl_apply(gsr_op_controller_t *ctrl,
                                     const gsr_controller_t *operating_point) {
    gsr_status_t status;

    if (ctrl == NULL || operating_point == NULL) return GSR_OPCTRL_INVALID_REQUEST;
    if (!ctrl->initialized || ctrl->controller == NULL) return GSR_OPCTRL_NOT_INITIALIZED;

    // apply the resolved operating point to the controller
    ctrl->controller->config = operating_point->config;
    ctrl->controller->mode = operating_point->mode;

    status = gsr_controller_set_config(ctrl->controller);
    if (status != GSR_STATUS_OK) {
        return gsr_opctrl_status_from_gsr(status);
    }

    return GSR_OPCTRL_OK;
}

gsr_opctrl_status_t gsr_opctrl_request(gsr_op_controller_t *ctrl,
                                       const gsr_op_request_t *request,
                                       gsr_controller_t *operating_point) {
    gsr_controller_t planned_operating_point;
    gsr_opctrl_status_t status;

    if (ctrl == NULL || request == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->controller == NULL) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    planned_operating_point = *(ctrl->controller);

    status = gsr_opctrl_plan(request, &planned_operating_point);
    if (status != GSR_OPCTRL_OK) {
        return status;
    }

    ctrl->current_request = *request;
 
    if (ctrl->has_valid_op) { // only allow adjustement after a valid sample read. To allow for conservative first read.
        status = controller_adjust_range_if_needed(ctrl, &planned_operating_point);
        if (status != GSR_OPCTRL_OK) {
            return status;
        }
    }

    if (operating_point != NULL) {
        *operating_point = planned_operating_point;
    }
    return gsr_opctrl_apply(ctrl, &planned_operating_point);
}

static gsr_opctrl_status_t gsr_opctrl_recover_underflow(gsr_op_controller_t *ctrl)
{
    uint8_t code;

    if (ctrl == NULL || ctrl->controller == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    code = ctrl->controller->config.idac_code;

    if (code <= 1U) {
        return GSR_OPCTRL_UNSATISFIABLE;
    }

    /* Halve current. Round up so nonzero codes remain nonzero. */
    code = (code + 1U) >> 1;

    return gsr_opctrl_status_from_gsr(
        gsr_controller_set_current(ctrl->controller, code)
    );
}

static gsr_opctrl_status_t gsr_opctrl_recover_overflow(gsr_op_controller_t *ctrl)
{
    uint8_t code;
    uint32_t next_code;
    uint32_t max_code;

    if (ctrl == NULL || ctrl->controller == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    code = ctrl->controller->config.idac_code;

    max_code = ctrl->controller->max_current_nA / GSR_IDAC_LSB_NA;
    if (max_code > GSR_IDAC_MAX_CODE) {
        max_code = GSR_IDAC_MAX_CODE;
    }

    if (code >= max_code) {
        return GSR_OPCTRL_UNSATISFIABLE;
    }

    /* Double current, saturating at max_code. */
    next_code = (uint32_t)code << 1;
    if (next_code > max_code) {
        next_code = max_code;
    }

    if (next_code == code) {
        return GSR_OPCTRL_UNSATISFIABLE;
    }

    return gsr_opctrl_status_from_gsr(
        gsr_controller_set_current(ctrl->controller, (uint8_t)next_code)
    );
}

gsr_opctrl_status_t gsr_opctrl_read_sample(gsr_op_controller_t *ctrl,
                                           gsr_sample_t *sample) {
    gsr_opctrl_status_t status;

    if (ctrl == NULL || sample == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->controller == NULL) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    status = gsr_opctrl_status_from_gsr(gsr_read_sample(ctrl->controller)); 
    
    if (status == GSR_OPCTRL_MEASUREMENT_UNDERFLOW) { // VIN too low ==> decrease i_dc
        ctrl->has_valid_op = false;
        status = gsr_opctrl_recover_underflow(ctrl);
        if (status == GSR_OPCTRL_UNSATISFIABLE) {
            return GSR_OPCTRL_UNSATISFIABLE;
        }
        return GSR_OPCTRL_MEASUREMENT_UNDERFLOW;
    } else if (status == GSR_OPCTRL_MEASUREMENT_OVERFLOW) { // VIN too high ==> increase i_dc
        ctrl->has_valid_op = false;
        status = gsr_opctrl_recover_overflow(ctrl);
        if (status == GSR_OPCTRL_UNSATISFIABLE) {
            return GSR_OPCTRL_UNSATISFIABLE;
        }
        return GSR_OPCTRL_MEASUREMENT_OVERFLOW;
    } else if (status != GSR_OPCTRL_OK) { // NOT_INITIALIZED, INVALID_ARGUMENT or MEASUREMENT_ERROR (which includes NO_NEW_SAMPLE and MISSED_UPDATE)
        ctrl->has_valid_op = false;
        return status;
    }
    ctrl->has_valid_op = true;
    
    *sample = ctrl->controller->sample;

    // After a successful read, we want to adjust i_dc closest as possible to i_dc max while respecting the range request.
    status = gsr_opctrl_adjust_range(ctrl);

    return status;
}

void gsr_opctrl_shutdown(gsr_op_controller_t *ctrl) {
    if (ctrl != NULL) {
        ctrl->has_valid_op = false;
        ctrl->initialized = false;
    }
}
