#include "LPC17xx.h"
#include "lpc17xx_systick.h"
#include "lpc17xx_uart.h"
#include "temperature_sensor.h"

#include <stdio.h>

#define SAMPLE_PERIOD_MS 100U
#define TELEMETRY_BAUD_RATE 115200U

/* Mediciones y errores disponibles en el depurador. */
static volatile uint16_t temperature_adc;
static volatile uint32_t temperature_mv;
static volatile int32_t temperature_millicelsius;
static volatile uint32_t temperature_sample_count;
static volatile uint32_t temperature_adc_timeout_count;
static volatile uint32_t temperature_uart_error_count;

/* SysTick es el único escritor. Cada tick corresponde a un periodo de muestreo. */
static volatile uint32_t sample_tick;

void SysTick_Handler(void) {
    ++sample_tick;
    __SEV();
}

static uint32_t wait_for_sample(uint32_t last_tick) {
    uint32_t tick;
    while ((tick = sample_tick) == last_tick) {
        /* El evento queda pendiente si SysTick se ejecuta antes de WFE. */
        __WFE();
    }
    return tick;
}

static bool read_temperature(TemperatureMeasurement *measurement) {
    const uint32_t start_tick = sample_tick;
    TemperatureSensor_Start();

    while (!TemperatureSensor_TryRead(measurement)) {
        if (sample_tick != start_tick) {
            TemperatureSensor_Cancel();
            return false;
        }
    }
    return true;
}

static void init_telemetry(void) {
    const UART_CFG_T uart_config = {
        .baudRate = TELEMETRY_BAUD_RATE,
        .parity = UART_PARITY_NONE,
        .dataBits = UART_DBITS_8,
        .stopBits = UART_STOPBIT_1
    };
    const UART_FIFO_CFG_T fifo_config = {
        .resetRxBuf = ENABLE,
        .resetTxBuf = ENABLE,
        .dmaMode = DISABLE,
        .level = UART_FIFO_TRGLEV0
    };

    UART_PinConfig(UART_TX0_P0_2);
    UART_Init(UART0, &uart_config);
    UART_FIFOConfig(UART0, &fifo_config);
    UART_TxEnable(UART0);
}

static void report_sample(const TemperatureMeasurement *measurement) {
    const int32_t temperature = measurement->millicelsius;
    const int32_t magnitude = temperature < 0 ? -temperature : temperature;
    const char *sign = temperature < 0 ? "-" : "";
    const long degrees = (long)(magnitude / 1000);
    const long fraction = (long)(magnitude % 1000);
    const uint32_t count = temperature_sample_count + 1U;

    temperature_adc = measurement->raw_adc;
    temperature_mv = measurement->millivolts;
    temperature_millicelsius = temperature;
    temperature_sample_count = count;

    /* Una temperatura en °C por línea, con punto decimal. */
    char line[16];
    const int length = snprintf(line, sizeof line, "%s%ld.%03ld\r\n",
                                sign, degrees, fraction);
    if (length <= 0 || (size_t)length >= sizeof line ||
        UART_Send(UART0, (const uint8_t *)line, (uint32_t)length, BLOCKING)
            != (uint32_t)length) {
        ++temperature_uart_error_count;
    }

    printf("N=%lu ADC=%u V=%lu mV T=%s%ld.%03ld C\n",
           (unsigned long)count,
           (unsigned int)measurement->raw_adc, (unsigned long)measurement->millivolts,
           sign, degrees, fraction);
}

int main(void) {
    /* Redlib Semihost dirige stdout a la consola del depurador. */
    setvbuf(stdout, NULL, _IONBF, 0);
    SystemCoreClockUpdate();
    TemperatureSensor_Init();
    init_telemetry();
    SYSTICK_InternalInit(SAMPLE_PERIOD_MS);
    SYSTICK_IntCmd(ENABLE);
    SYSTICK_Cmd(ENABLE);

    uint32_t last_tick = 0;
    while (1) {
        TemperatureMeasurement measurement;
        last_tick = wait_for_sample(last_tick);
        if (read_temperature(&measurement)) {
            report_sample(&measurement);
        } else {
            ++temperature_adc_timeout_count;
        }
    }
}
