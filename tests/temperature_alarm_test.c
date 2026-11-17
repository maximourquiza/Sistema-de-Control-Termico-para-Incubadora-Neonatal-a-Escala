/* Probar la coordinacion real de main, sin ejecutar perifericos. */
#define main firmware_main
#include "../src/Sistema-de-Control-Termico-para-Incubadora-Neonatal-a-Escala.c"
#undef main

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static unsigned int start_calls, stop_calls;
static bool start_success = true;

bool SpeakerSine_Start(void) { ++start_calls; return start_success; }
void SpeakerSine_Stop(void) { ++stop_calls; }
void ActuatorsPWM_SetHeaterDuty(uint32_t duty) { (void)duty; }

int run_temperature_alarm_tests(void) {
    TemperatureMeasurement measurement = {1055, 850, 35000};
    TemperatureFilterResult filtered = {35000, 35000};
    speaker_sine_ready = true;
    CHECK(!temperature_alarm_active);
    update_temperature_alarm(&measurement, &filtered);
    CHECK(!temperature_alarm_active && start_calls == 0 && stop_calls == 0);

    /* Activar por mediana aunque el IIR aun no haya llegado. */
    filtered = (TemperatureFilterResult){40000, 39999};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(temperature_alarm_active && start_calls == 1);
    for (unsigned int i = 0; i < 10; ++i) {
        filtered = (TemperatureFilterResult){39990, 40010};
        update_temperature_alarm(&measurement, &filtered);
    }
    CHECK(start_calls == 1 && stop_calls == 0);

    /* Ni una falla de sensor ni un timeout deben silenciar el aviso activo. */
    measurement.millicelsius = 126000;
    filtered = (TemperatureFilterResult){35000, 35000};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(temperature_alarm_active && stop_calls == 0);
    TemperatureController_Init(&temperature_controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG);
    trip_control(TEMPERATURE_CONTROL_FAULT_ADC);
    CHECK(temperature_alarm_active && stop_calls == 0);
    control_watchdog_armed = true;
    last_valid_sample_tick = sample_tick;
    for (unsigned int i = 0; i < ACQUISITION_TIMEOUT_TICKS; ++i) SysTick_Handler();
    CHECK(control_watchdog_expired && temperature_alarm_active && stop_calls == 0);

    measurement.millicelsius = 39500;
    filtered = (TemperatureFilterResult){39501, 39500};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(temperature_alarm_active && stop_calls == 0);
    filtered = (TemperatureFilterResult){39500, 39501};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(temperature_alarm_active && stop_calls == 0);
    filtered = (TemperatureFilterResult){39500, 39500};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(!temperature_alarm_active && stop_calls == 1);
    CHECK(temperature_controller.state == TEMPERATURE_CONTROL_FAULT_ADC);

    /* Fallas no termicas con temperatura normal no activan la bocina. */
    const TemperatureControlState faults[] = {
        TEMPERATURE_CONTROL_FAULT_ADC, TEMPERATURE_CONTROL_FAULT_SENSOR,
        TEMPERATURE_CONTROL_FAULT_GAP, TEMPERATURE_CONTROL_FAULT_CONFIG
    };
    for (unsigned int i = 0; i < sizeof faults / sizeof faults[0]; ++i) {
        temperature_control_state = faults[i];
        update_temperature_alarm(&measurement, &filtered);
        CHECK(!temperature_alarm_active && start_calls == 1 && stop_calls == 1);
    }
    measurement.millicelsius = 126000;
    filtered = (TemperatureFilterResult){126000, 126000};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(!temperature_alarm_active && start_calls == 1);

    /* Activar por filtrada, aun con el controlador bloqueado y sin rearme. */
    measurement.millicelsius = 40000;
    filtered = (TemperatureFilterResult){39999, 40000};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(temperature_alarm_active && start_calls == 2);
    filtered = (TemperatureFilterResult){39500, 39500};
    update_temperature_alarm(&measurement, &filtered);
    update_temperature_alarm(&measurement, &filtered);
    CHECK(!temperature_alarm_active && stop_calls == 2);

    /* Un fallo de inicio queda visible, sin reintentos en cada muestra. */
    start_success = false;
    filtered = (TemperatureFilterResult){40000, 40000};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(temperature_alarm_active && !speaker_sine_ready && start_calls == 3);
    update_temperature_alarm(&measurement, &filtered);
    CHECK(start_calls == 3);
    filtered = (TemperatureFilterResult){39500, 39500};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(!temperature_alarm_active && stop_calls == 3);
    filtered = (TemperatureFilterResult){40000, 40000};
    update_temperature_alarm(&measurement, &filtered);
    CHECK(temperature_alarm_active && start_calls == 3);
    return 0;
}
