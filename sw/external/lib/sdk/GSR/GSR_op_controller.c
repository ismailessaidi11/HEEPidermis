// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_op_controller.c
// Author: Ismail Essaidi
// Date: 08/04/2026
// Description: Operating-point request layer for the GSR front-end.

#include "GSR_op_controller.h"

#include <stddef.h>

#define GSR_OPCTRL_PROFILE_COUNT 3U

typedef struct {
    uint8_t idac_code;
} gsr_range_profile_t;

typedef struct {
    uint32_t refresh_rate_Hz;
} gsr_sensitivity_profile_t;

static const gsr_range_profile_t k_range_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .idac_code = 5U  },
    { .idac_code = 100U },
    { .idac_code = 200U },
};

static const gsr_sensitivity_profile_t k_sensitivity_profiles[GSR_OPCTRL_PROFILE_COUNT] = {
    { .refresh_rate_Hz = 1U  },
    { .refresh_rate_Hz = 5U  },
    { .refresh_rate_Hz = 20U },
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

gsr_opctrl_status_t gsr_opctrl_init(gsr_op_controller_t *ctrl,
                                    gsr_controller_t *controller) {
    if (ctrl == NULL || controller == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    ctrl->controller = controller;
    ctrl->has_active_op = false;
    ctrl->initialized = true;

    return GSR_OPCTRL_OK;
}

gsr_opctrl_status_t gsr_opctrl_plan(const gsr_op_request_t *request,
                                    gsr_operating_point_t *operating_point) {
    if (!gsr_opctrl_is_valid_request(request) || operating_point == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    operating_point->request = *request;
    operating_point->config.channel = VCO_CHANNEL_P;
    operating_point->config.idac_code = k_range_profiles[(uint32_t)request->range].idac_code;
    operating_point->config.current_refresh_rate_Hz = k_sensitivity_profiles[(uint32_t)request->sensitivity].refresh_rate_Hz;
    // don't matter
    operating_point->config.baseline_refresh_rate_Hz = 0U;
    operating_point->config.phasic_refresh_rate_Hz = 0U;
    operating_point->config.recovery_refresh_rate_Hz = 0U;

    return GSR_OPCTRL_OK;
}

gsr_opctrl_status_t gsr_opctrl_apply(gsr_op_controller_t *ctrl,
                                     const gsr_operating_point_t *operating_point) {
    gsr_status_t status;
    gsr_config_t merged_config;
    gsr_ctrl_mode_t mode;

    if (ctrl == NULL || operating_point == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->controller == NULL) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    merged_config = ctrl->controller->config;
    merged_config.channel = operating_point->config.channel;
    merged_config.idac_code = operating_point->config.idac_code;

    mode = ctrl->controller->mode;
    if (mode == GSR_CTRL_MODE_INIT) {
        mode = GSR_CTRL_MODE_BASELINE;
        ctrl->controller->mode = mode;
    }

    if (mode == GSR_CTRL_MODE_BASELINE) {
        merged_config.baseline_refresh_rate_Hz = operating_point->config.current_refresh_rate_Hz;
    } else if (mode == GSR_CTRL_MODE_PHASIC) {
        merged_config.phasic_refresh_rate_Hz = operating_point->config.current_refresh_rate_Hz;
    } else if (mode == GSR_CTRL_MODE_RECOVERY) {
        merged_config.recovery_refresh_rate_Hz = operating_point->config.current_refresh_rate_Hz;
    } else {
        return GSR_OPCTRL_INVALID_REQUEST;
    }

    ctrl->controller->config = merged_config;
    status = gsr_controller_set_config(ctrl->controller);
    if (status != GSR_STATUS_OK) {
        return gsr_opctrl_status_from_gsr(status);
    }

    ctrl->active_op = *operating_point;
    ctrl->active_op.config = ctrl->controller->config;
    ctrl->has_active_op = true;
    return GSR_OPCTRL_OK;
}

gsr_opctrl_status_t gsr_opctrl_request(gsr_op_controller_t *ctrl,
                                       const gsr_op_request_t *request,
                                       gsr_operating_point_t *operating_point) {
    gsr_operating_point_t planned_operating_point;
    gsr_opctrl_status_t status;

    if (ctrl == NULL || request == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->controller == NULL) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    status = gsr_opctrl_plan(request, &planned_operating_point);
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
    if (!ctrl->initialized || ctrl->controller == NULL) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    status = gsr_read_sample(ctrl->controller);
    if (status != GSR_STATUS_OK) {
        return gsr_opctrl_status_from_gsr(status);
    }

    *sample = ctrl->controller->sample;
    return GSR_OPCTRL_OK;
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

    if (ctrl == NULL) {
        return GSR_OPCTRL_INVALID_REQUEST;
    }
    if (!ctrl->initialized || ctrl->controller == NULL || !ctrl->has_active_op) {
        return GSR_OPCTRL_NOT_INITIALIZED;
    }

    if (event == GSR_OPCTRL_RANGE_EVENT_NONE) {
        if (operating_point != NULL) {
            *operating_point = ctrl->active_op;
        }
        return GSR_OPCTRL_OK;
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
