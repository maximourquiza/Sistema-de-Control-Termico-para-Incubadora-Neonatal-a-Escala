#include "temperature_controller.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int run_preheating_tests(void) {
    TemperatureController controller;
    CHECK(TemperatureController_Init(&controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG));
    CHECK(TemperatureController_Update(&controller, 25, 25, 0.1f) == 0);
    CHECK(!controller.preheating && controller.state == TEMPERATURE_CONTROL_WAITING);
    CHECK(TemperatureController_SetReference(&controller, 36));

    /* Caso de placa: 33,5 C y consigna 36 C ahora deben producir 100 %. */
    for (unsigned int i = 0; i < 100; ++i) {
        CHECK(TemperatureController_Update(&controller, 33.5f, 33.5f, 0.1f) == 100);
        CHECK(controller.preheating && controller.integral_percent == 0);
        CHECK(!controller.derivative_initialized);
    }
    CHECK(TemperatureController_Update(&controller, 34.999f, 34.999f, 0.1f) == 100);
    CHECK(TemperatureController_Update(&controller, 35, 35, 0.1f) == 100);
    CHECK(!controller.preheating && controller.derivative_c_per_s == 0);
    CHECK(controller.integral_percent < 0.03f);
    CHECK(TemperatureController_Update(&controller, 34.001f, 34.001f, 0.1f) == 100);
    CHECK(!controller.preheating); /* No reentrar antes del margen de 2 C. */
    CHECK(TemperatureController_Update(&controller, 34, 34, 0.1f) == 100);
    CHECK(controller.preheating && controller.integral_percent == 0);

    /* Salir por la lectura mayor, incluso si la filtrada llega con retardo. */
    CHECK(TemperatureController_Update(&controller, 34, 35, 0.1f) == 100);
    CHECK(!controller.preheating);
    CHECK(TemperatureController_Update(&controller, 33.5f, 35, 0.1f) == 100);
    CHECK(!controller.preheating);
    CHECK(TemperatureController_Update(&controller, 34, 34, 0.1f) == 100);
    CHECK(TemperatureController_Update(&controller, 35, 34, 0.1f) == 100);
    CHECK(!controller.preheating);

    /* La consigna nueva se evalua antes de ordenar potencia. */
    TemperatureController_SetReference(&controller, 37);
    CHECK(TemperatureController_Update(&controller, 34.5f, 34.5f, 0.1f) == 100);
    TemperatureController_SetReference(&controller, 34);
    CHECK(TemperatureController_Update(&controller, 34.5f, 34.5f, 0.1f) == 10);
    CHECK(!controller.preheating);
    TemperatureController_SetReference(&controller, 36);
    CHECK(TemperatureController_Update(&controller, 34.5f, 34.5f, 0.1f) == 100);
    CHECK(!controller.preheating);

    /* Un bloqueo termico cancela boost y no permite reenergizar sin rearme. */
    CHECK(TemperatureController_Update(&controller, 33, 33, 0.1f) == 100);
    TemperatureController_Trip(&controller, TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    CHECK(!controller.preheating);
    CHECK(TemperatureController_Update(&controller, 33, 33, 0.1f) == 0);
    TemperatureController_SetReference(&controller, 36);
    CHECK(TemperatureController_Update(&controller, 33, 33, 0.1f) == 100);
    TemperatureController_Reset(&controller);
    CHECK(!controller.preheating && controller.integral_percent == 0);
    CHECK(TemperatureController_Update(&controller, 33, 33, 0.1f) == 0);

    /* Los chequeos de temperatura preceden a cualquier salida de boost. */
    TemperatureController_SetReference(&controller, 36);
    TemperatureController_Update(&controller, 33, 33, 0.1f);
    for (unsigned int i = 0; i < 20; ++i) {
        CHECK(TemperatureController_Update(&controller, 40, 40, 0.1f) == 0);
    }
    CHECK(!controller.preheating && controller.state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    CHECK(TemperatureController_Update(&controller, 33, 33, 0.1f) == 0);
    return 0;
}
