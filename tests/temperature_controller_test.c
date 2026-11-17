#include "temperature_controller.h"

#include <math.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static TemperatureController start_controller(float reference) {
    TemperatureController controller;
    TemperatureController_Init(&controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG);
    TemperatureController_SetReference(&controller, reference);
    return controller;
}

static int test_startup_and_reference(void) {
    TemperatureController controller;
    CHECK(TemperatureController_Init(&controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG));
    CHECK(TemperatureController_Update(&controller, 25, 25, 0.1f) == 0);
    CHECK(controller.state == TEMPERATURE_CONTROL_WAITING);
    CHECK(!TemperatureController_SetReference(&controller, 33.999f));
    CHECK(!TemperatureController_SetReference(&controller, 37.001f));
    CHECK(!TemperatureController_SetReference(&controller, NAN));
    CHECK(TemperatureController_SetReference(&controller, 36));
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 10);
    CHECK(controller.state == TEMPERATURE_CONTROL_RUNNING);
    CHECK(TemperatureController_Update(&controller, 37, 37, 0.1f) == 0);
    return 0;
}

static int test_anti_windup(void) {
    TemperatureController controller = start_controller(36);
    for (unsigned int i = 0; i < 500; ++i) {
        CHECK(TemperatureController_Update(&controller, 20, 20, 0.1f) == 100);
    }
    CHECK(controller.integral_percent == 0);
    CHECK(TemperatureController_Update(&controller, 36, 36, 0.1f) == 0);
    for (unsigned int i = 0; i < 500; ++i) {
        CHECK(TemperatureController_Update(&controller, 39, 39, 0.1f) == 0);
    }
    CHECK(controller.integral_percent == 0);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 10);
    return 0;
}

static int test_integral_and_setpoint(void) {
    TemperatureController controller = start_controller(36);
    for (unsigned int i = 0; i < 100; ++i) {
        TemperatureController_Update(&controller, 35, 35, 0.1f);
    }
    CHECK(controller.integral_percent > 0.199f && controller.integral_percent < 0.201f);
    const float integral = controller.integral_percent;
    CHECK(TemperatureController_SetReference(&controller, 35));
    TemperatureController_Update(&controller, 35, 35, 0.1f);
    CHECK(controller.integral_percent == integral);
    TemperatureController_Update(&controller, 35.01f, 35.01f, 0.1f);
    CHECK(controller.integral_percent < integral);
    return 0;
}

static int test_elapsed_and_derivative(void) {
    TemperatureController short_step = start_controller(36);
    TemperatureController long_step = start_controller(36);
    TemperatureController_Update(&short_step, 35, 35, 0.1f);
    TemperatureController_Update(&long_step, 35, 35, 0.3f);
    CHECK(long_step.integral_percent > short_step.integral_percent * 2.99f);
    TemperatureControllerConfig config = {0, 0, 10, 1};
    TemperatureController_Init(&short_step, &config);
    TemperatureController_SetReference(&short_step, 36);
    CHECK(TemperatureController_Update(&short_step, 35, 35, 0.1f) == 0);
    CHECK(TemperatureController_Update(&short_step, 34, 34, 0.1f) == 9);
    const float rate = short_step.derivative_c_per_s;
    TemperatureController_SetReference(&short_step, 37);
    TemperatureController_Update(&short_step, 34, 34, 0.1f);
    CHECK(short_step.derivative_c_per_s > rate); /* Sin impulso por la consigna. */
    CHECK(short_step.derivative_c_per_s < 0);
    return 0;
}

static int test_faults_and_rearm(void) {
    TemperatureController controller = start_controller(36);
    TemperatureController_Update(&controller, 35, 35, 0.1f);
    TemperatureController_Trip(&controller, TEMPERATURE_CONTROL_FAULT_ADC);
    CHECK(controller.integral_percent == 0);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 0);
    TemperatureController_SetReference(&controller, 36);
    CHECK(TemperatureController_Update(&controller, 37, 38, 0.1f) == 0);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 0);
    TemperatureController_SetReference(&controller, 36);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 10);
    CHECK(controller.state == TEMPERATURE_CONTROL_RUNNING);
    CHECK(TemperatureController_Update(&controller, 39, 40, 0.1f) == 0);
    CHECK(controller.state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    TemperatureController_SetReference(&controller, 36);
    CHECK(TemperatureController_Update(&controller, 40, 39, 0.1f) == 0);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 0);
    TemperatureController_SetReference(&controller, 36);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 10);
    return 0;
}

static int test_invalid_input_and_reset(void) {
    const float bad_times[] = {0, -0.1f, 1, 2, NAN, INFINITY};
    for (unsigned int i = 0; i < sizeof bad_times / sizeof bad_times[0]; ++i) {
        TemperatureController controller = start_controller(36);
        CHECK(TemperatureController_Update(&controller, 35, 35, bad_times[i]) == 0);
        CHECK(controller.state == TEMPERATURE_CONTROL_FAULT_GAP);
    }
    const float bad_temperatures[] = {-41, 126, NAN, INFINITY};
    for (unsigned int i = 0; i < sizeof bad_temperatures / sizeof bad_temperatures[0]; ++i) {
        TemperatureController controller = start_controller(36);
        CHECK(TemperatureController_Update(&controller, bad_temperatures[i], 35, 0.1f) == 0);
        CHECK(controller.state == TEMPERATURE_CONTROL_FAULT_SENSOR);
    }
    TemperatureController controller = start_controller(36);
    TemperatureController_Update(&controller, 35, 35, 0.1f);
    TemperatureController_Reset(&controller);
    CHECK(controller.integral_percent == 0 && !controller.derivative_initialized);
    CHECK(controller.config.kp == 10);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 0);
    TemperatureControllerConfig invalid = {NAN, 0, 0, 1};
    CHECK(!TemperatureController_Init(&controller, &invalid));
    CHECK(!TemperatureController_SetReference(&controller, 36));
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 0);
    return 0;
}

int run_controller_tests(void) {
    int result;
#define RUN(name) do { result = name(); if (result) return result; } while (0)
    RUN(test_startup_and_reference);
    RUN(test_anti_windup);
    RUN(test_integral_and_setpoint);
    RUN(test_elapsed_and_derivative);
    RUN(test_faults_and_rearm);
    RUN(test_invalid_input_and_reset);
    return 0;
}
