// Copyright 2026 EPFL contributors
// SPDX-License-Identifier: Apache-2.0
//
// File: test_GSR_op_controller/main.c
// Author: Ismail Essaidi
// Date: 13/04/2026
// Description: End-to-end test for the GSR operating-point abstraction layer.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "GSR_op_controller.h"
#include "GSR_sdk.h"
#include "REFs_ctrl.h"
#include "iDAC_ctrl.h"
#include "soc_ctrl.h"
#include "timer_sdk.h"
#include "x-heep.h"

#define PRINTF_IN_SIM   1
#define PRINTF_IN_FPGA  0

#if TARGET_SIM && PRINTF_IN_SIM
#define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#elif PRINTF_IN_FPGA && !TARGET_SIM
#define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define SYS_FCLK_HZ               10000000U
#define IREF_DEFAULT_CAL          255U
#define VREF_DEFAULT_CAL          0b1111111111
#define IDAC_DEFAULT_CAL          0U

#define SAMPLE_TARGET_PER_OP      4U
#define SAMPLE_ATTEMPT_LIMIT      20U

static void hw_init(void) {
    soc_ctrl_t soc_ctrl;
    soc_ctrl.base_addr = mmio_region_from_addr((uintptr_t)SOC_CTRL_START_ADDRESS);
    soc_ctrl_set_frequency(&soc_ctrl, SYS_FCLK_HZ);

    timer_cycles_init();

    REFs_calibrate(IREF_DEFAULT_CAL, IREF1);
    REFs_calibrate(VREF_DEFAULT_CAL, VREF);

    iDACs_enable(true, false);
    iDAC1_calibrate(IDAC_DEFAULT_CAL);

    enable_timer_interrupt();
    timer_irq_enable();
    timer_start();
}

static const char *range_name(gsr_range_t range) {
    switch (range) {
    case GSR_RANGE_LOW:
        return "LOW";
    case GSR_RANGE_MEDIUM:
        return "MEDIUM";
    case GSR_RANGE_HIGH:
        return "HIGH";
    default:
        return "INVALID";
    }
}

static const char *sensitivity_name(gsr_sensitivity_t sensitivity) {
    switch (sensitivity) {
    case GSR_SENSITIVITY_LOW:
        return "LOW";
    case GSR_SENSITIVITY_MEDIUM:
        return "MEDIUM";
    case GSR_SENSITIVITY_HIGH:
        return "HIGH";
    default:
        return "INVALID";
    }
}

static void print_operating_point(const gsr_operating_point_t *op) {
    PRINTF("  request: range=%s sensitivity=%s\n",
           range_name(op->request.range),
           sensitivity_name(op->request.sensitivity));
    PRINTF("  VCO: channel=%d refresh=%lu Hz\n",
           (int)op->config.channel,
           (unsigned long)op->config.refresh_rate_Hz);
    PRINTF("  IDAC: code=%u current=%lu nA\n",
           (unsigned int)op->config.idac_code,
           (unsigned long)gsr_current_from_idac_code_nA(op->config.idac_code));
}

static int expect_opctrl_ok(const char *step, gsr_opctrl_status_t status) {
    if (status != GSR_OPCTRL_OK) {
        PRINTF("  FAIL: %s returned %d\n", step, (int)status);
        return -1;
    }

    return 0;
}

static int test_plan_profiles(void) {
    static const gsr_op_request_t requests[] = {
        { .range = GSR_RANGE_LOW,    .sensitivity = GSR_SENSITIVITY_LOW    },
        { .range = GSR_RANGE_MEDIUM, .sensitivity = GSR_SENSITIVITY_MEDIUM },
        { .range = GSR_RANGE_HIGH,   .sensitivity = GSR_SENSITIVITY_HIGH   },
    };

    PRINTF("=== test_plan_profiles ===\n");

    for (uint32_t i = 0U; i < (sizeof(requests) / sizeof(requests[0])); i++) {
        gsr_operating_point_t op;
        gsr_opctrl_status_t status = gsr_opctrl_plan(&requests[i], &op);
        if (expect_opctrl_ok("gsr_opctrl_plan", status) != 0) {
            return -1;
        }

        if (op.config.refresh_rate_Hz == 0U) {
            PRINTF("  FAIL: planned refresh rate is zero\n");
            return -1;
        }
        PRINTF("plan[%lu]\n", (unsigned long)i);
        print_operating_point(&op);
    }

    PRINTF("test_plan_profiles PASS\n");
    return 0;
}

static int collect_samples(gsr_op_controller_t *opctrl, uint32_t target_samples) {
    uint32_t valid_samples = 0U;
    uint32_t attempts = 0U;

    while (valid_samples < target_samples && attempts < SAMPLE_ATTEMPT_LIMIT) {
        gsr_sample_t sample;
        gsr_opctrl_status_t status = gsr_opctrl_read_sample(opctrl, &sample);

        if (status == GSR_OPCTRL_OK) {
            PRINTF("  sample[%lu]: Vin=%lu uV G=%lu nS I=%lu nA\n",
                   (unsigned long)valid_samples,
                   (unsigned long)sample.vin_uV,
                   (unsigned long)sample.conductance_nS,
                   (unsigned long)sample.current_nA);
            valid_samples++;
        } else if (status == GSR_OPCTRL_MEASUREMENT_ERROR) {
            /* Expected while waiting for the next VCO refresh or after a missed update. */
        } else {
            PRINTF("  FAIL: gsr_opctrl_read_sample returned %d\n", (int)status);
            return -1;
        }

        attempts++;
    }

    if (valid_samples != target_samples) {
        PRINTF("  FAIL: timed out waiting for samples (%lu/%lu)\n",
               (unsigned long)valid_samples,
               (unsigned long)target_samples);
        return -1;
    }

    return 0;
}

static int test_request_apply_and_sample(void) {
    static const gsr_op_request_t requests[] = {
        { .range = GSR_RANGE_LOW,    .sensitivity = GSR_SENSITIVITY_LOW    },
        { .range = GSR_RANGE_MEDIUM, .sensitivity = GSR_SENSITIVITY_MEDIUM },
        { .range = GSR_RANGE_HIGH,   .sensitivity = GSR_SENSITIVITY_HIGH   },
    };

    gsr_context_t gsr_ctx;
    gsr_op_controller_t opctrl;

    PRINTF("=== test_request_apply_and_sample ===\n");

    if (expect_opctrl_ok("gsr_opctrl_init", gsr_opctrl_init(&opctrl, &gsr_ctx)) != 0) {
        return -1;
    }

    for (uint32_t i = 0U; i < (sizeof(requests) / sizeof(requests[0])); i++) {
        gsr_operating_point_t op;
        const gsr_operating_point_t *active_op;
        gsr_opctrl_status_t status = gsr_opctrl_request(&opctrl, &requests[i], &op);

        if (expect_opctrl_ok("gsr_opctrl_request", status) != 0) {
            return -1;
        }

        active_op = gsr_opctrl_get_active(&opctrl);
        if (active_op == NULL) {
            PRINTF("  FAIL: no active operating point after request\n");
            return -1;
        }

        if (!gsr_ctx.initialized || gsr_ctx.config.idac_code != op.config.idac_code ||
            gsr_ctx.config.refresh_rate_Hz != op.config.refresh_rate_Hz ||
            gsr_ctx.config.channel != op.config.channel ||
            gsr_ctx.current_nA != gsr_current_from_idac_code_nA(op.config.idac_code)) {
            PRINTF("  FAIL: GSR SDK context not synchronized with operating point\n");
            return -1;
        }
        PRINTF("  SDK limits: max_I=%lu nA\n",
               (unsigned long)gsr_ctx.limits.max_current_nA);

        PRINTF("request[%lu] applied\n", (unsigned long)i);
        print_operating_point(active_op);

        if (collect_samples(&opctrl, SAMPLE_TARGET_PER_OP) != 0) {
            return -1;
        }
    }

    gsr_opctrl_shutdown(&opctrl);
    PRINTF("test_request_apply_and_sample PASS\n");
    return 0;
}

static int test_range_event_adjustment(void) {
    gsr_context_t gsr_ctx;
    gsr_op_controller_t opctrl;
    gsr_operating_point_t op;
    gsr_op_request_t request = {
        .range = GSR_RANGE_MEDIUM,
        .sensitivity = GSR_SENSITIVITY_MEDIUM,
    };

    PRINTF("=== test_range_event_adjustment ===\n");

    if (expect_opctrl_ok("gsr_opctrl_init", gsr_opctrl_init(&opctrl, &gsr_ctx)) != 0) {
        return -1;
    }
    if (expect_opctrl_ok("gsr_opctrl_request", gsr_opctrl_request(&opctrl, &request, &op)) != 0) {
        return -1;
    }

    if (expect_opctrl_ok("VIN_TOO_HIGH adjustment",
                         gsr_opctrl_handle_range_event(&opctrl,
                                                       GSR_OPCTRL_RANGE_EVENT_VIN_TOO_HIGH,
                                                       &op)) != 0) {
        return -1;
    }
    if (op.request.range != GSR_RANGE_HIGH) {
        PRINTF("  FAIL: VIN_TOO_HIGH did not increase range\n");
        return -1;
    }
    PRINTF("  VIN_TOO_HIGH -> range=%s\n", range_name(op.request.range));

    if (expect_opctrl_ok("VIN_TOO_LOW adjustment",
                         gsr_opctrl_handle_range_event(&opctrl,
                                                       GSR_OPCTRL_RANGE_EVENT_VIN_TOO_LOW,
                                                       &op)) != 0) {
        return -1;
    }
    if (op.request.range != GSR_RANGE_MEDIUM) {
        PRINTF("  FAIL: VIN_TOO_LOW did not decrease range\n");
        return -1;
    }
    PRINTF("  VIN_TOO_LOW -> range=%s\n", range_name(op.request.range));

    gsr_opctrl_shutdown(&opctrl);
    PRINTF("test_range_event_adjustment PASS\n");
    return 0;
}

int main(void) {
    int failures = 0;

    hw_init();

    PRINTF("=== test_GSR_op_controller ===\n");

    failures += (test_plan_profiles() != 0) ? 1 : 0;
    failures += (test_request_apply_and_sample() != 0) ? 1 : 0;
    // failures += (test_range_event_adjustment() != 0) ? 1 : 0;

    if (failures == 0) {
        PRINTF("=== ALL TESTS PASSED ===\n");
    } else {
        PRINTF("=== %d TEST(S) FAILED ===\n", failures);
    }

    return failures;
}
