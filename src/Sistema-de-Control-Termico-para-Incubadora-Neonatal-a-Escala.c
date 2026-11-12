#include "LPC17xx.h"
#include "lpc17xx_systick.h"
#include "lpc17xx_uart.h"
#include "temperature_sensor.h"
#include "actuators_pwm.h"
#include "speaker_sine.h"

#include <stdio.h>

#define SAMPLE_PERIOD_MS 100U
#define COOLER_INITIAL_DUTY_PERCENT 25U
#define HEATER_INITIAL_DUTY_PERCENT 100U
#define TELEMETRY_BAUD_RATE 9600U
#define REFERENCE_MIN_MILLIC 34000U
#define REFERENCE_MAX_MILLIC 37000U
#define REFERENCE_LINE_LENGTH 6U  /* Hasta dos enteros y tres decimales: 36.500. */
#define UART_RX_BYTES_PER_POLL 16U

/* Mediciones y errores disponibles en el depurador. */
static volatile uint16_t temperature_adc;
static volatile uint32_t temperature_mv;
static volatile int32_t temperature_millicelsius;
static volatile uint32_t temperature_sample_count;
static volatile uint32_t temperature_adc_timeout_count;
static volatile uint32_t temperature_uart_error_count;
static volatile uint32_t temperature_reference_millicelsius;
static volatile bool speaker_sine_ready;

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
    UART_PinConfig(UART_RX0_P0_3);
    UART_Init(UART0, &uart_config);
    UART_FIFOConfig(UART0, &fifo_config);
    UART_TxEnable(UART0);
}

static void send_uart(const char *text, uint32_t length) {
    const uint32_t sent = UART_Send(UART0, (const uint8_t *)text, length, BLOCKING);
    if (sent != length) {
        ++temperature_uart_error_count;
    }
}

static bool parse_reference(const char *line, size_t length, uint32_t *reference) {
    /* Dos dígitos enteros, con uno a tres decimales opcionales. */
    if (length < 2U || line[0] < '0' || line[0] > '9' ||
        line[1] < '0' || line[1] > '9') {
        return false;
    }

    uint32_t value = (10U * (uint32_t)(line[0] - '0') +
                      (uint32_t)(line[1] - '0')) * 1000U;
    if (length > 2U) {
        if (line[2] != '.' || length < 4U || length > REFERENCE_LINE_LENGTH) {
            return false;
        }
        uint32_t place = 100U;
        for (size_t i = 3; i < length; ++i) {
            if (line[i] < '0' || line[i] > '9') {
                return false;
            }
            value += (uint32_t)(line[i] - '0') * place;
            place /= 10U;
        }
    }

    if (value < REFERENCE_MIN_MILLIC || value > REFERENCE_MAX_MILLIC) {
        return false;
    }
    *reference = value;
    return true;
}

static void poll_reference(void) {
    static char line[REFERENCE_LINE_LENGTH];
    static size_t length;
    static bool discard;
    const uint8_t rx_errors = UART_LINESTAT_OE | UART_LINESTAT_PE |
                              UART_LINESTAT_FE | UART_LINESTAT_BI;

    /* Leer solo los bytes disponibles y acotar el trabajo por período. */
    for (unsigned int i = 0; i < UART_RX_BYTES_PER_POLL; ++i) {
        const uint8_t status = UART_GetLineStatus(UART0);
        if (status & rx_errors) {
            discard = true;
        }
        if (!(status & UART_LINESTAT_RDR)) {
            break;
        }

        const char byte = (char)UART_ReceiveByte(UART0);
        if (byte == '\r' || byte == '\n') {
            if (discard || length != 0U) {
                uint32_t reference;
                if (!discard && parse_reference(line, length, &reference)) {
                    temperature_reference_millicelsius = reference;
                    char reply[16];
                    const int count = snprintf(reply, sizeof reply, "REF=%lu\r\n",
                                               (unsigned long)reference);
                    if (count > 0 && (size_t)count < sizeof reply) {
                        send_uart(reply, (uint32_t)count);
                    } else {
                        ++temperature_uart_error_count;
                    }
                } else {
                    send_uart("ERR REF\r\n", sizeof "ERR REF\r\n" - 1U);
                }
            }
            length = 0;
            discard = false;
        } else if (!discard) {
            if (length < sizeof line) {
                line[length++] = byte;
            } else {
                discard = true;
            }
        }
    }
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
    if (length > 0 && (size_t)length < sizeof line) {
        send_uart(line, (uint32_t)length);
    } else {
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
    speaker_sine_ready = SpeakerSine_Init();
    ActuatorsPWM_Init();
    ActuatorsPWM_SetCoolerDuty(COOLER_INITIAL_DUTY_PERCENT);
    ActuatorsPWM_SetHeaterDuty(HEATER_INITIAL_DUTY_PERCENT);
    TemperatureSensor_Init();
    init_telemetry();
    SYSTICK_InternalInit(SAMPLE_PERIOD_MS);
    SYSTICK_IntCmd(ENABLE);
    SYSTICK_Cmd(ENABLE);

    uint32_t last_tick = 0;
    while (1) {
        TemperatureMeasurement measurement;
        last_tick = wait_for_sample(last_tick);
        poll_reference();
        if (read_temperature(&measurement)) {
            report_sample(&measurement);
        } else {
            ++temperature_adc_timeout_count;
        }
    }
}
