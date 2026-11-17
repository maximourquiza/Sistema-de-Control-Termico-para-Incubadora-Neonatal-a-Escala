/* Incluir main permite probar su coordinacion real sin ejecutar perifericos. */
#define main firmware_main
#define printf test_printf
#include "../src/Sistema-de-Control-Termico-para-Incubadora-Neonatal-a-Escala.c"
#undef printf
#undef main

#include <string.h>
#include <stdarg.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint32_t applied_duty, heater_calls;
static const char *rx_text;
static char tx_text[128];
static unsigned int tx_length;
static bool adc_completes;
static char debug_text[256];

void ActuatorsPWM_SetHeaterDuty(uint32_t duty) {
    applied_duty = duty;
    ++heater_calls;
}

uint8_t UART_GetLineStatus(LPC_UART_TypeDef *uart) {
    (void)uart;
    return *rx_text ? UART_LINESTAT_RDR : 0;
}

uint8_t UART_ReceiveByte(LPC_UART_TypeDef *uart) {
    (void)uart;
    return (uint8_t)*rx_text++;
}

uint32_t UART_Send(LPC_UART_TypeDef *uart, const uint8_t *text, uint32_t length,
                   TRANSFER_BLOCK_Type mode) {
    (void)uart;
    (void)mode;
    for (uint32_t i = 0; i < length && tx_length < sizeof tx_text - 1; ++i) {
        tx_text[tx_length++] = (char)text[i];
    }
    tx_text[tx_length] = '\0';
    return length;
}

void TemperatureSensor_Start(void) {}
void TemperatureSensor_Cancel(void) {}
bool TemperatureSensor_TryRead(TemperatureMeasurement *measurement) {
    if (!adc_completes) {
        SysTick_Handler();
        return false;
    }
    *measurement = (TemperatureMeasurement){1004, 809, 31000};
    return true;
}

/* Verificar el camino de telemetria sin ejecutar semihosting en el emulador. */
int test_printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(debug_text, sizeof debug_text, format, arguments);
    va_end(arguments);
    return length;
}

static void receive_reference(const char *text) {
    rx_text = text;
    poll_reference();
}

int run_control_integration_tests(void) {
    TemperatureController_Init(&temperature_controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG);
    TemperatureMeasurement measurement = {1055, 850, 35000};
    TemperatureFilterResult filtered = {35000, 35000};
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0 && temperature_control_state == TEMPERATURE_CONTROL_WAITING);
    receive_reference("36.5\r\n");
    CHECK(strcmp(tx_text, "REF=36500\r\n") == 0);
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100 && temperature_control_state == TEMPERATURE_CONTROL_RUNNING);
    const uint32_t calls = heater_calls;
    update_control(&measurement, &filtered, 1);
    CHECK(heater_calls == calls); /* No reiniciar PWM si no cambia el duty. */
    for (unsigned int i = 0; i < 9; ++i) SysTick_Handler();
    CHECK(applied_duty == 100);
    SysTick_Handler();
    CHECK(applied_duty == 0 && control_watchdog_expired);
    update_control(&measurement, &filtered, 10);
    CHECK(temperature_control_state == TEMPERATURE_CONTROL_FAULT_GAP);
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0);
    receive_reference("36\n");
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100);
    filtered.median_millicelsius = 40000;
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0 && temperature_control_state == TEMPERATURE_CONTROL_FAULT_OVERTEMP);
    receive_reference("36\n");
    filtered = (TemperatureFilterResult){38000, 38000};
    update_control(&measurement, &filtered, 1);
    filtered = (TemperatureFilterResult){35000, 35000};
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0);
    receive_reference("36\n");
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100);
    measurement.millicelsius = 126000;
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0 && temperature_control_state == TEMPERATURE_CONTROL_FAULT_SENSOR);
    measurement.millicelsius = 35000;
    receive_reference("36\n");
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 100);
    adc_completes = false;
    CHECK(!read_temperature(&measurement));
    /* main usa esta misma secuencia al recibir false. */
    trip_control(TEMPERATURE_CONTROL_FAULT_ADC);
    CHECK(applied_duty == 0 && temperature_control_state == TEMPERATURE_CONTROL_FAULT_ADC);
    adc_completes = true;
    CHECK(read_temperature(&measurement));
    update_control(&measurement, &filtered, 1);
    CHECK(applied_duty == 0);
    receive_reference("99\n");
    CHECK(temperature_reference_millicelsius == 36000);
    CHECK(temperature_controller.reference_pending == false);
    tx_length = 0;
    temperature_controller.integral_percent = 1.25f;
    temperature_sensor_invalid_count = 7;
    report_sample(&measurement, &filtered);
    CHECK(strcmp(tx_text, "35.000\r\n") == 0);
    CHECK(temperature_sample_count == 1);
    CHECK(strstr(debug_text, "I_mPct=1250 DROP=7") != 0);
    CHECK(strstr(debug_text, "BASE=40") != 0);
    return 0;
}
