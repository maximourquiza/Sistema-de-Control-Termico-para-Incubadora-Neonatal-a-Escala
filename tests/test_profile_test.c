/* Los tres perfiles ejecutan la coordinacion real de main, con bocina simulada. */
#define main firmware_main
#include "../src/Sistema-de-Control-Termico-para-Incubadora-Neonatal-a-Escala.c"
#undef main

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static uint32_t applied_duty, starts, stops;
void ActuatorsPWM_SetHeaterDuty(uint32_t duty) { applied_duty = duty; }
bool SpeakerSine_Start(void) { ++starts; return true; }
void SpeakerSine_Stop(void) { ++stops; }

static void sample(int32_t temperature) {
    const TemperatureMeasurement measurement = {1000, 800, temperature};
    const TemperatureFilterResult filtered = {temperature, temperature};
    update_control(&measurement, &filtered, 1);
    update_temperature_alarm(&measurement, &filtered);
}

int run_test_profile_tests(void) {
#if TEMPERATURE_TEST_PROFILE == TEMPERATURE_TEST_NORMAL
    CHECK(TEMPERATURE_ALARM_ON_MILLIC == 40000 && TEMPERATURE_ALARM_OFF_MILLIC == 39500);
    CHECK(TEMPERATURE_BLOCK_THRESHOLD_C == 40 && TEMPERATURE_BLOCK_REARM_C == 38);
#elif TEMPERATURE_TEST_PROFILE == TEMPERATURE_TEST_SPEAKER
    CHECK(TEMPERATURE_ALARM_ON_MILLIC == 35000 && TEMPERATURE_ALARM_OFF_MILLIC == 34500);
    CHECK(TEMPERATURE_BLOCK_THRESHOLD_C == 40 && TEMPERATURE_BLOCK_REARM_C == 38);
#else
    CHECK(TEMPERATURE_ALARM_ON_MILLIC == 35000 && TEMPERATURE_ALARM_OFF_MILLIC == 34500);
    CHECK(TEMPERATURE_BLOCK_THRESHOLD_C == 35 && TEMPERATURE_BLOCK_REARM_C == 34);
#endif
    CHECK(TEMPERATURE_CONTROL_OVERTEMP_CONFIRM_MS == 2000);
    CHECK(TemperatureController_Init(&temperature_controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG));
    speaker_sine_ready = true;
    CHECK(TemperatureController_SetReference(&temperature_controller, 36));
    sample(TEMPERATURE_ALARM_OFF_MILLIC - 100);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    CHECK(!temperature_alarm_active && starts == 0);
    for (unsigned int i = 0; i < 19; ++i) {
        sample(TEMPERATURE_ALARM_ON_MILLIC);
        CHECK(temperature_alarm_active && starts == 1);
        CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    }
    sample(TEMPERATURE_ALARM_ON_MILLIC);
#if TEMPERATURE_TEST_PROFILE == TEMPERATURE_TEST_SPEAKER
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    /* La prueba sonora mantiene el corte real de 40 C. */
    for (unsigned int i = 0; i < 20; ++i) sample(40000);
#endif
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    CHECK(applied_duty == 0 && temperature_alarm_active && starts == 1);
    sample(TEMPERATURE_ALARM_OFF_MILLIC + 1);
    CHECK(temperature_alarm_active && stops == 0);
    sample(TEMPERATURE_ALARM_OFF_MILLIC);
    CHECK(!temperature_alarm_active && stops == 1);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    const int32_t rearm = (int32_t)(TEMPERATURE_BLOCK_REARM_C * 1000.0f);
    TemperatureController_SetReference(&temperature_controller, 36);
    sample(rearm);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    sample(rearm - 100);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    TemperatureController_SetReference(&temperature_controller, 36);
    sample(rearm - 100);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
#if TEMPERATURE_TEST_PROFILE == TEMPERATURE_TEST_SPEAKER
    /* 37,9 C permite el rearme del bloqueo real, pero supera el umbral sonoro
     * de prueba de 35 C: la bocina debe volver a sonar. */
    CHECK(starts == 2 && stops == 1 && temperature_alarm_active);
#else
    CHECK(starts == 1 && stops == 1 && !temperature_alarm_active);
#endif
    return 0;
}
