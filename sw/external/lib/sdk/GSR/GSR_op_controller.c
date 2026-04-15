// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_op_controller.c
// Author: Ismail Essaidi
// Date: 08/04/2026
// Description: Operating-point request layer for the GSR front-end.

#include "GSR_op_controller.h"

#include <stddef.h>

#include "iDAC_ctrl.h"

#define GSR_OPCTRL_PROFILE_COUNT              3U
#define GSR_OPCTRL_DEFAULT_IDAC_CALIBRATION   0U

typedef struct {
    uint8_t idac_code;
} gsr_range_profile_t;

typedef struct {
    vco_channel_t channel;
    uint32_t refresh_rate_Hz;
} gsr_sensitivity_profile_t;

/*
 * The profile tables are the policy model of this layer. The application asks
 * for range/sensitivity labels; this table resolves those labels to concrete
 * GSR SDK configuration values.
 */
static const gsr_range_profile_t k_range_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .idac_code = 25U  }, /* 1000 nA */
    { .idac_code = 100U }, /* 4000 nA */
    { .idac_code = 200U }, /* 8000 nA */
};

static const gsr_sensitivity_profile_t k_sensitivity_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .channel = VCO_CHANNEL_P, .refresh_rate_Hz = 1U  },
    { .channel = VCO_CHANNEL_P, .refresh_rate_Hz = 5U  },
    { .channel = VCO_CHANNEL_P, .refresh_rate_Hz = 20U },
};

static bool gsr_opctrl_is_valid_request(const gsr_op_request_t *request) {
    if (request == NULL) {
        return false;
    }

    return (request->range <= GSR_RANGE_HIGH) &&
           (request->sensitivity <= GSR_SENSITIVITY_HIGH);
}

static gsr_opctrl_status_t gsr_opctrl_status_from_gsr(gsr_status_t status) {
    if (status == GSR_STATUS_OK) {
        return GSR_OPCTRL_OK;
    }
    if (status == GSR_STATUS_INVALID_ARGUMENT) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (status == GSR_STATUS_NOT_INITIALIZED) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }
    if (status == GSR_STATUS_OUT_OF_RANGE) {
        return GSR_OPCTRL_UNSATISFIABLE;
    }

    return GSR_OPCTRL_MEASUREMENT_ERROR;
}

static bool gsr_opctrl_current_allowed(const gsr_context_t *ctx, uint8_t idac_code) {
    uint32_t requested_current_nA;
    const gsr_sample_t *last_sample;

    if (ctx == NULL) {
        return false;
    }

    requested_current_nA = gsr_current_from_idac_code_nA(idac_code);
    last_sample = gsr_get_last_sample(ctx);

    /* Before the first valid sample, use the SDK's absolute current limit only. */
    if (last_sample == NULL || !last_sample->valid) {
        return requested_current_nA <= GSR_MAX_CURRENT_NA;
    }

    /* After a valid sample, honor the SDK limit derived from that conductance. */
    if (ctx->limits.max_current_nA <= GUARD_IDC_NA) {
        return false;
    }

    return requested_current_nA <= (ctx->limits.max_current_nA - GUARD_IDC_NA);
}

static gsr_range_t gsr_opctrl_highest_allowed_range(const gsr_context_t *ctx,
                                                    gsr_range_t requested_range) {
    gsr_range_t range = requested_range;

    while (!gsr_opctrl_current_allowed(ctx, k_range_profiles[(uint32_t)range].idac_code)) {
        if (range == GSR_RANGE_LOW) {
            break;
        }
        range = (gsr_range_t)((uint32_t)range - 1U);
    }

    return range;
}

gsr_opctrl_status_t gsr_opctrl_init(gsr_op_controller_t *ctrl,
                                    gsr_context_t *measurement_ctx) {
    if (ctrl == NULL || measurement_ctx == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    ctrl->measurement_ctx = measurement_ctx;
    ctrl->has_active_op = false;
    ctrl->initialized = true;

    return GSR_OPCTRL_OK;
}

/* Translate an application request into concrete SDK configuration fields. Will contain the control algorithm. */
gsr_opctrl_status_t gsr_opctrl_plan(const gsr_op_request_t *request,
                                    gsr_operating_point_t *operating_point) { 
    const gsr_range_profile_t *range_profile;
    const gsr_sensitivity_profile_t *sensitivity_profile;

    if (!gsr_opctrl_is_valid_request(request) || operating_point == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    range_profile = &k_range_profiles[(uint32_t)request->range];
    sensitivity_profile = &k_sensitivity_profiles[(uint32_t)request->sensitivity];

    operating_point->request = *request;
    operating_point->config.channel = sensitivity_profile->channel;
    operating_point->config.refresh_rate_Hz = sensitivity_profile->refresh_rate_Hz;
    operating_point->config.idac_code = range_profile->idac_code;

    return GSR_OPCTRL_OK;
}

gsr_opctrl_status_t gsr_opctrl_apply(gsr_op_controller_t *ctrl,
                                     const gsr_operating_point_t *operating_point) {
    gsr_status_t measurement_status;

    if (ctrl == NULL || operating_point == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->measurement_ctx == NULL) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    if (!gsr_opctrl_current_allowed(ctrl->measurement_ctx, operating_point->config.idac_code)) {
        return GSR_OPCTRL_UNSATISFIABLE;
    }

    if (ctrl->measurement_ctx->initialized) {
        measurement_status = gsr_set_config(ctrl->measurement_ctx, &operating_point->config);
    } else {
        measurement_status = gsr_init(ctrl->measurement_ctx, &operating_point->config);
    }

    if (measurement_status != GSR_STATUS_OK) {
        return gsr_opctrl_status_from_gsr(measurement_status);
    }

    ctrl->active_op = *operating_point;
    ctrl->last_request = operating_point->request;
    ctrl->has_active_op = true;

    return GSR_OPCTRL_OK;
}

gsr_opctrl_status_t gsr_opctrl_request(gsr_op_controller_t *ctrl,
                                       const gsr_op_request_t *request,
                                       gsr_operating_point_t *operating_point) {
    gsr_op_request_t resolved_request;
    gsr_operating_point_t planned_operating_point;
    gsr_opctrl_status_t status;

    if (ctrl == NULL || request == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->measurement_ctx == NULL) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }
    if (!gsr_opctrl_is_valid_request(request)) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    resolved_request = *request;
    resolved_request.range = gsr_opctrl_highest_allowed_range(ctrl->measurement_ctx,
                                                              request->range);

    status = gsr_opctrl_plan(&resolved_request, &planned_operating_point);
    if (status != GSR_OPCTRL_OK) {
        return status;
    }

    status = gsr_opctrl_apply(ctrl, &planned_operating_point);
    if (status != GSR_OPCTRL_OK) {
        return status;
    }

    if (operating_point != NULL) {
        *operating_point = planned_operating_point;
    }

    return GSR_OPCTRL_OK;
}

gsr_opctrl_status_t gsr_opctrl_read_sample(gsr_op_controller_t *ctrl,
                                           gsr_sample_t *sample) {
    gsr_status_t status;

    if (ctrl == NULL || sample == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->measurement_ctx == NULL || !ctrl->has_active_op) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    status = gsr_read_sample(ctrl->measurement_ctx, sample);
    return gsr_opctrl_status_from_gsr(status);
}

const gsr_operating_point_t *gsr_opctrl_get_active(const gsr_op_controller_t *ctrl) {
    if (ctrl == NULL || !ctrl->has_active_op) {
        return NULL;
    }

    return &ctrl->active_op;
}

gsr_opctrl_status_t gsr_opctrl_handle_range_event(gsr_op_controller_t *ctrl,
                                                  gsr_opctrl_range_event_t event,
                                                  gsr_operating_point_t *operating_point) {
    gsr_op_request_t request;
    const gsr_sample_t *last_sample;

    if (ctrl == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->measurement_ctx == NULL || !ctrl->has_active_op) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    if (event == GSR_OPCTRL_RANGE_EVENT_NONE) {
        if (operating_point != NULL) {
            *operating_point = ctrl->active_op;
        }
        return GSR_OPCTRL_OK;
    }

    last_sample = gsr_get_last_sample(ctrl->measurement_ctx);
    if (last_sample == NULL || !last_sample->valid) {
        gsr_sample_t sample;
        gsr_opctrl_status_t status = gsr_opctrl_read_sample(ctrl, &sample);
        if (status != GSR_OPCTRL_OK) {
            return status;
        }
    }

    request = ctrl->active_op.request;

    if (event == GSR_OPCTRL_RANGE_EVENT_VIN_TOO_LOW) {
        if (request.range == GSR_RANGE_LOW) {
            return GSR_OPCTRL_UNSATISFIABLE;
        }
        request.range = (gsr_range_t)((uint32_t)request.range - 1U);
    } else if (event == GSR_OPCTRL_RANGE_EVENT_VIN_TOO_HIGH) {
        if (request.range == GSR_RANGE_HIGH) {
            return GSR_OPCTRL_UNSATISFIABLE;
        }
        request.range = (gsr_range_t)((uint32_t)request.range + 1U);
    } else {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    return gsr_opctrl_request(ctrl, &request, operating_point);
}

void gsr_opctrl_shutdown(gsr_op_controller_t *ctrl) {
    if (ctrl != NULL) {
        ctrl->has_active_op = false;
        ctrl->initialized = false;
    }
}
