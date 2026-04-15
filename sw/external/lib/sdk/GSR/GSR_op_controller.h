// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: GSR_op_controller.h
// Author: Ismail Essaidi
// Date: 08/04/2026
// Description: Operating-point request layer for the GSR front-end.

#ifndef GSR_OP_CONTROLLER_H
#define GSR_OP_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "GSR_sdk.h"

/* Status codes returned by the operating-point controller. */
typedef enum {
    GSR_OPCTRL_OK = 0,
    GSR_OPCTRL_INVALID_REQUEST,
    GSR_OPCTRL_UNSATISFIABLE,
    GSR_OPCTRL_NOT_INITIALIZED,
    GSR_OPCTRL_NO_VALID_SAMPLE,
    GSR_OPCTRL_MEASUREMENT_ERROR
} gsr_opctrl_status_t;

/*
 * Conductance range request. Higher range uses a higher injected current and
 * therefore consumes more power; power is considered a consequence of range,
 * not a separate request axis.
 */
typedef enum {
    GSR_RANGE_LOW = 0,
    GSR_RANGE_MEDIUM = 1,
    GSR_RANGE_HIGH = 2,
} gsr_range_t;

/* Readout sensitivity request. */
typedef enum {
    GSR_SENSITIVITY_LOW = 0,
    GSR_SENSITIVITY_MEDIUM = 1,
    GSR_SENSITIVITY_HIGH = 2,
} gsr_sensitivity_t;

/* Application-level intent. This is what the main application should request. */
typedef struct {
    gsr_range_t range;               /* Low/medium/high conductance range. */
    gsr_sensitivity_t sensitivity;   /* Low/medium/high VCO readout sensitivity. */
} gsr_op_request_t;

/* Hardware state resolved from a request. */
typedef struct {
    gsr_op_request_t request;        /* Original high-level request. */
    gsr_config_t config;             /* Resolved hardware configuration. */
} gsr_operating_point_t;

/* Deferred range event. ISRs should raise one of these, then non-ISR code can
 * call gsr_opctrl_handle_range_event().
 */
typedef enum {
    GSR_OPCTRL_RANGE_EVENT_NONE = 0,
    GSR_OPCTRL_RANGE_EVENT_VIN_TOO_LOW,
    GSR_OPCTRL_RANGE_EVENT_VIN_TOO_HIGH,
} gsr_opctrl_range_event_t;

/* Controller state. */
typedef struct {
    gsr_context_t *measurement_ctx;  /* GSR SDK context used for samples/config. */
    gsr_operating_point_t active_op; /* Last operating point successfully applied. */
    gsr_op_request_t last_request;   /* Last application-level request accepted. */

    bool has_active_op;              /* True after a successful apply/request. */
    bool initialized;                /* True after gsr_opctrl_init(). */
} gsr_op_controller_t;

/* Initialize the operating-point controller with the GSR SDK context it controls. */
gsr_opctrl_status_t gsr_opctrl_init(gsr_op_controller_t *ctrl,
                                    gsr_context_t *measurement_ctx);

/* Translate an application request into concrete SDK configuration fields. Will contain the control algorithm. */
gsr_opctrl_status_t gsr_opctrl_plan(const gsr_op_request_t *request,
                                    gsr_operating_point_t *operating_point);

/* Apply an already planned operating point through the GSR SDK. */
gsr_opctrl_status_t gsr_opctrl_apply(gsr_op_controller_t *ctrl,
                                     const gsr_operating_point_t *operating_point);

/* Plan and apply a request in one call. */
gsr_opctrl_status_t gsr_opctrl_request(gsr_op_controller_t *ctrl,
                                       const gsr_op_request_t *request,
                                       gsr_operating_point_t *operating_point);

/* Read one sample through the GSR SDK. */
gsr_opctrl_status_t gsr_opctrl_read_sample(gsr_op_controller_t *ctrl,
                                           gsr_sample_t *sample);

/* Return the active operating point, or NULL if none has been applied. */
const gsr_operating_point_t *gsr_opctrl_get_active(const gsr_op_controller_t *ctrl);

/* Adjust range after a deferred Vin range event. */
gsr_opctrl_status_t gsr_opctrl_handle_range_event(gsr_op_controller_t *ctrl,
                                                  gsr_opctrl_range_event_t event,
                                                  gsr_operating_point_t *operating_point);

/* Clear op-controller state. Hardware shutdown remains owned by lower layers. */
void gsr_opctrl_shutdown(gsr_op_controller_t *ctrl);

#endif /* GSR_OP_CONTROLLER_H */
