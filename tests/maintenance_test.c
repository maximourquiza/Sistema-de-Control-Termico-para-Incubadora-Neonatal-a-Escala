#include "temperature_controller.h"
#include <math.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int run_maintenance_tests(void) {
    TemperatureController controller;
    const float temperatures[] = {35.7f, 36.0f, 36.2f};
    const uint32_t expected[] = {58, 40, 28};
    for (unsigned int i = 0; i < 3; ++i) {
        CHECK(TemperatureController_Init(&controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG));
        CHECK(controller.config.kp == 60 && controller.config.ki == 0.20f);
        CHECK(controller.config.kd == 0 && controller.config.maintenance_percent == 40);
        CHECK(TemperatureController_Update(&controller, temperatures[i], temperatures[i], 0.1f) == 0);
        TemperatureController_SetReference(&controller, 36);
        CHECK(TemperatureController_Update(&controller, temperatures[i], temperatures[i], 0.1f) == expected[i]);
    }

    const float invalid[] = {-0.1f, 100.1f, NAN, INFINITY, -INFINITY};
    for (unsigned int i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        TemperatureControllerConfig config = TEMPERATURE_CONTROLLER_DEFAULT_CONFIG;
        config.maintenance_percent = invalid[i];
        CHECK(!TemperatureController_Init(&controller, &config));
        CHECK(!TemperatureController_SetReference(&controller, 36));
        CHECK(TemperatureController_Update(&controller, 36, 36, 0.1f) == 0);
    }
    const float limits[] = {0, 100};
    for (unsigned int i = 0; i < 2; ++i) {
        TemperatureControllerConfig config = TEMPERATURE_CONTROLLER_DEFAULT_CONFIG;
        config.maintenance_percent = limits[i];
        CHECK(TemperatureController_Init(&controller, &config));
        TemperatureController_SetReference(&controller, 36);
        CHECK(TemperatureController_Update(&controller, 36, 36, 0.1f) == (uint32_t)limits[i]);
    }

    /* Con P=0 se puede aislar el anti-windup de la suma BASE + I. */
    const TemperatureControllerConfig integral_only = {0, 10, 0, 1, 40};
    CHECK(TemperatureController_Init(&controller, &integral_only));
    TemperatureController_SetReference(&controller, 36);
    for (unsigned int i = 0; i < 200; ++i) {
        TemperatureController_Update(&controller, 35.5f, 35.5f, 0.1f);
    }
    CHECK(controller.integral_percent == 60);
    CHECK(TemperatureController_Update(&controller, 35.5f, 35.5f, 0.1f) == 100);
    CHECK(controller.integral_percent == 60); /* No crecer hasta I=100. */
    TemperatureController_Update(&controller, 36.5f, 36.5f, 0.1f);
    CHECK(controller.integral_percent == 59.5f); /* Desaturar desde arriba. */
    for (unsigned int i = 0; i < 250; ++i) {
        TemperatureController_Update(&controller, 36.5f, 36.5f, 0.1f);
    }
    CHECK(controller.integral_percent == -40);
    CHECK(TemperatureController_Update(&controller, 36.5f, 36.5f, 0.1f) == 0);
    CHECK(controller.integral_percent == -40);
    CHECK(TemperatureController_Update(&controller, 36, 36, 0.1f) == 0);
    TemperatureController_Update(&controller, 35.5f, 35.5f, 0.1f);
    CHECK(controller.integral_percent == -39.5f); /* Desaturar desde abajo. */

    /* Al salir de boost la base se aplica aun con P e I en cero. */
    TemperatureController_Reset(&controller);
    TemperatureController_SetReference(&controller, 36);
    CHECK(TemperatureController_Update(&controller, 33, 33, 0.1f) == 100);
    CHECK(controller.preheating && controller.integral_percent == 0);
    CHECK(TemperatureController_Update(&controller, 36, 36, 0.1f) == 40);
    CHECK(!controller.preheating && controller.integral_percent == 0);
    TemperatureController_Trip(&controller, TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    CHECK(TemperatureController_Update(&controller, 36, 36, 0.1f) == 0);
    return 0;
}
