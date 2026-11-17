#define main firmware_main
#define printf test_printf
#include "../src/Sistema-de-Control-Termico-para-Incubadora-Neonatal-a-Escala.c"
#undef printf
#undef main

#include <math.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static uint32_t applied_duty;
void ActuatorsPWM_SetHeaterDuty(uint32_t duty) { applied_duty = duty; }

static int test_preserve_integral_on_discard(void) {
    TemperatureController controller;
    TemperatureController_Init(&controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG);
    TemperatureController_SetReference(&controller, 36);
    for (unsigned int i = 0; i < 100; ++i) {
        TemperatureController_Update(&controller, 35.5f, 35.5f, 0.1f);
    }
    CHECK(controller.integral_percent > 0.999f && controller.integral_percent < 1.001f);
    const float accumulated = controller.integral_percent;
    /* La pausa debe retener el aporte aprendido y cancelar la confirmacion. */
    for (unsigned int i = 0; i < 19; ++i) {
        TemperatureController_Update(&controller, 40, 40, 0.1f);
    }
    CHECK(controller.overtemperature_ms == 1900);
    const float invalid[] = {NAN, INFINITY, -INFINITY, -41, 126};
    for (unsigned int i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        CHECK(TemperatureController_Update(&controller, invalid[i], 35, 0.1f) == 0);
        CHECK(controller.integral_percent == accumulated);
        CHECK(controller.overtemperature_ms == 0 && !controller.derivative_initialized);
        CHECK(controller.derivative_c_per_s == 0);
        CHECK(controller.state == TEMPERATURE_CONTROL_RUNNING && controller.reference_c == 36);
    }
    CHECK(TemperatureController_Update(&controller, 35.5f, 35.5f, 0.1f) == 71);
    CHECK(controller.integral_percent > accumulated + 0.009f);
    CHECK(controller.integral_percent < accumulated + 0.011f);
    CHECK(controller.derivative_c_per_s == 0);

    const TemperatureControlState reset_faults[] = {
        TEMPERATURE_CONTROL_FAULT_ADC, TEMPERATURE_CONTROL_FAULT_GAP,
        TEMPERATURE_CONTROL_FAULT_CONFIG, TEMPERATURE_CONTROL_FAULT_OVERTEMP
    };
    for (unsigned int i = 0; i < sizeof reset_faults / sizeof reset_faults[0]; ++i) {
        TemperatureController_Init(&controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG);
        TemperatureController_SetReference(&controller, 36);
        TemperatureController_Update(&controller, 35.5f, 35.5f, 0.1f);
        CHECK(controller.integral_percent > 0);
        TemperatureController_Trip(&controller, reset_faults[i]);
        CHECK(controller.integral_percent == 0 && !controller.derivative_initialized);
        CHECK(controller.overtemperature_ms == 0);
    }
    return 0;
}

int run_prototype_control_tests(void) {
    const int preservation_result = test_preserve_integral_on_discard();
    if (preservation_result) return preservation_result;
    TemperatureMeasurement measurement = {1055, 850, 35000};
    TemperatureFilterResult filtered = {35000, 35000};
    TemperatureController_Init(&temperature_controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG);
    CHECK(temperature_controller.state == TEMPERATURE_CONTROL_WAITING);
    TemperatureController_SetReference(&temperature_controller, 36);
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100 && temperature_control_state == TEMPERATURE_CONTROL_RUNNING);

    /* El caso observado: pico crudo aislado, sin bloqueo ni nueva consigna. */
    measurement.millicelsius = 250000;
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0 && temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    CHECK(temperature_controller.reference_c == 36);
    measurement.millicelsius = 35000;
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100);

    /* Timeout y corte del watchdog: pausa temporal, recuperacion automatica. */
    trip_control(TEMPERATURE_CONTROL_FAULT_ADC);
    CHECK(applied_duty == 0 && temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100);
    for (unsigned int i = 0; i < ACQUISITION_TIMEOUT_TICKS; ++i) SysTick_Handler();
    CHECK(applied_duty == 0 && control_watchdog_expired);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    update_control(&measurement, &filtered, ACQUISITION_TIMEOUT_TICKS);
    CHECK(applied_duty == 0 && !control_watchdog_expired);
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100);

    /* Temperatura alta corta: no enclavar aunque momentaneamente sea 40 C. */
    filtered = (TemperatureFilterResult){41000, 41000};
    for (unsigned int i = 0; i < 19; ++i) {
        update_control(&measurement, &filtered, 1);
        CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    }
    CHECK(temperature_controller.overtemperature_ms == 1900);
    filtered = (TemperatureFilterResult){35000, 35000};
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100 && temperature_controller.overtemperature_ms == 0);

    /* Cola del IIR sin confirmacion de mediana: no acumular sobretemperatura. */
    filtered = (TemperatureFilterResult){35000, 41000};
    for (unsigned int i = 0; i < 30; ++i) update_control(&measurement, &filtered, 1);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    CHECK(temperature_controller.overtemperature_ms == 0);

    /* Un hueco o muestra invalida interrumpe la confirmacion. */
    filtered = (TemperatureFilterResult){40000, 40000};
    for (unsigned int i = 0; i < 19; ++i) update_control(&measurement, &filtered, 1);
    trip_control(TEMPERATURE_CONTROL_FAULT_ADC);
    CHECK(temperature_controller.overtemperature_ms == 0);
    for (unsigned int i = 0; i < 19; ++i) update_control(&measurement, &filtered, 1);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    update_control(&measurement, &filtered, 10);
    CHECK(temperature_controller.overtemperature_ms == 0);
    for (unsigned int i = 0; i < 19; ++i) update_control(&measurement, &filtered, 1);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    update_control(&measurement, &filtered, 1);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_FAULT_OVERTEMP && applied_duty == 0);

    /* El bloqueo confirmado conserva el rearme existente bajo 38 C. */
    filtered = (TemperatureFilterResult){35000, 35000};
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0 && temperature_control_state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    TemperatureController_SetReference(&temperature_controller, 36);
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100 && temperature_control_state == TEMPERATURE_CONTROL_RUNNING);

    /* Valores no finitos y tiempos invalidos pausan, sin enclavar otro motivo. */
    const float bad_values[] = {NAN, INFINITY, -INFINITY, -41, 126};
    for (unsigned int i = 0; i < sizeof bad_values / sizeof bad_values[0]; ++i) {
        CHECK(TemperatureController_Update(&temperature_controller, bad_values[i], 35, 0.1f) == 0);
        CHECK(temperature_controller.state == TEMPERATURE_CONTROL_RUNNING);
        CHECK(TemperatureController_Update(&temperature_controller, 35, 35, 0.1f) == 100);
    }
    const float bad_times[] = {NAN, INFINITY, -1, 0, 1};
    for (unsigned int i = 0; i < sizeof bad_times / sizeof bad_times[0]; ++i) {
        CHECK(TemperatureController_Update(&temperature_controller, 35, 35, bad_times[i]) == 0);
        CHECK(temperature_controller.state == TEMPERATURE_CONTROL_RUNNING);
        CHECK(TemperatureController_Update(&temperature_controller, 35, 35, 0.1f) == 100);
    }

    /* Una pausa transitoria conserva boost dentro de la banda de histeresis. */
    filtered = (TemperatureFilterResult){33500, 33500};
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100 && temperature_controller.preheating);
    trip_control(TEMPERATURE_CONTROL_FAULT_ADC);
    CHECK(applied_duty == 0 && temperature_controller.preheating);
    filtered = (TemperatureFilterResult){34500, 34500};
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100 && temperature_controller.preheating);
    for (unsigned int i = 0; i < ACQUISITION_TIMEOUT_TICKS; ++i) SysTick_Handler();
    CHECK(applied_duty == 0 && temperature_controller.preheating);
    update_control(&measurement, &filtered, 10);
    CHECK(applied_duty == 0 && temperature_controller.preheating);
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100);
    measurement.millicelsius = 250000;
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0 && temperature_controller.preheating);
    measurement.millicelsius = 35000;
    filtered = (TemperatureFilterResult){35000, 34000};
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100 && !temperature_controller.preheating);
    CHECK(temperature_controller.integral_percent < 0.03f);

    /* Camino real de descarte crudo en main: pausar sin borrar la integral. */
    filtered = (TemperatureFilterResult){35500, 35500};
    for (unsigned int i = 0; i < 100; ++i) update_control(&measurement, &filtered, 1);
    const float learned = temperature_controller.integral_percent;
    CHECK(learned > 0.99f);
    measurement.millicelsius = 250000;
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0 && temperature_controller.integral_percent == learned);
    measurement.millicelsius = 35000;
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty >= 71 && temperature_controller.integral_percent > learned);
    return 0;
}
