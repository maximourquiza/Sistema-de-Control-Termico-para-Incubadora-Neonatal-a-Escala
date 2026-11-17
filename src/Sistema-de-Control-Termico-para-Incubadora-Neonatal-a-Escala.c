#include "LPC17xx.h"
#include "lpc17xx_systick.h"
#include "lpc17xx_uart.h"
#include "temperature_sensor.h"
#include "temperature_filter.h"
#include "temperature_controller.h"
#include "actuators_pwm.h"
#include "speaker_sine.h"

#include <stdio.h>

#define SAMPLE_PERIOD_MS 100U
#define COOLER_INITIAL_DUTY_PERCENT 15U
#define ACQUISITION_TIMEOUT_TICKS (1000U / SAMPLE_PERIOD_MS)
#define TELEMETRY_BAUD_RATE 9600U
#define REFERENCE_MIN_MILLIC 34000U
#define REFERENCE_MAX_MILLIC 37000U
#define REFERENCE_LINE_LENGTH 6U  /* Hasta dos enteros y tres decimales: 36.500. */
#define UART_RX_BYTES_PER_POLL 16U
#define TEMPERATURE_ALARM_ON_MILLIC 40000
#define TEMPERATURE_ALARM_OFF_MILLIC 39500

/* Mediciones y errores disponibles en el depurador. */
static volatile uint16_t temperature_adc;
static volatile uint32_t temperature_mv;
static volatile int32_t temperature_millicelsius;
static volatile int32_t temperature_median_millicelsius;
static volatile int32_t temperature_filtered_millicelsius;
static volatile uint32_t temperature_sample_count;
static volatile uint32_t temperature_adc_timeout_count;
static volatile uint32_t temperature_sensor_invalid_count;
static volatile uint32_t temperature_uart_error_count;
static volatile uint32_t temperature_reference_millicelsius;
static volatile bool speaker_sine_ready;
static volatile bool temperature_alarm_active;
static TemperatureController temperature_controller;
static volatile TemperatureControlState temperature_control_state;
static volatile uint32_t heater_duty_percent;
static volatile uint32_t last_valid_sample_tick;
static volatile bool control_watchdog_armed;
static volatile bool control_watchdog_expired;

/* SysTick es el único escritor. Cada tick corresponde a un periodo de muestreo. */
static volatile uint32_t sample_tick;

void SysTick_Handler(void) {
    ++sample_tick;
    /* Cortar incluso si main esta detenido dentro de la salida bloqueante. */
    if (control_watchdog_armed &&
        sample_tick - last_valid_sample_tick >= ACQUISITION_TIMEOUT_TICKS) {
        ActuatorsPWM_SetHeaterDuty(0);
        heater_duty_percent = 0;
#if !TEMPERATURE_CONTROL_PROTOTYPE_MODE
        temperature_control_state = TEMPERATURE_CONTROL_FAULT_GAP;
#endif
        control_watchdog_expired = true;
        control_watchdog_armed = false;
    }
    __SEV();
}

/* Estas funciones se ejecutan con interrupciones enmascaradas para impedir
 * que main vuelva a energizar el calefactor despues del corte de SysTick. */
static void apply_heater_duty(uint32_t duty) {
    if (duty != heater_duty_percent) {
        ActuatorsPWM_SetHeaterDuty(duty);
        heater_duty_percent = duty;
    }
}

static void trip_control(TemperatureControlState fault) {
    TemperatureController_Trip(&temperature_controller, fault);
    apply_heater_duty(0);
    control_watchdog_armed = false;
    temperature_control_state = temperature_controller.state;
}

static void synchronize_watchdog(void) {
    if (control_watchdog_expired) {
        trip_control(TEMPERATURE_CONTROL_FAULT_GAP);
        control_watchdog_expired = false;
    }
}

static void update_control(const TemperatureMeasurement *measurement,
                           const TemperatureFilterResult *filtered,
                           uint32_t elapsed_ticks) {
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    synchronize_watchdog();
    if (measurement->millicelsius < -40000 || measurement->millicelsius > 125000) {
        trip_control(TEMPERATURE_CONTROL_FAULT_SENSOR);
    } else {
        const uint32_t duty = TemperatureController_Update(&temperature_controller,
            (float)filtered->filtered_millicelsius / 1000.0f,
            (float)filtered->median_millicelsius / 1000.0f,
            (float)elapsed_ticks * ((float)SAMPLE_PERIOD_MS / 1000.0f));
        apply_heater_duty(duty);
        last_valid_sample_tick = sample_tick;
        control_watchdog_armed = temperature_controller.state == TEMPERATURE_CONTROL_RUNNING;
        temperature_control_state = temperature_controller.state;
    }
    __set_PRIMASK(primask);
}

/* Aviso termico independiente del estado/enclavamiento del controlador.
 * Solo main lo actualiza, con una adquisicion completada y plausible. */
static void update_temperature_alarm(const TemperatureMeasurement *measurement,
                                     const TemperatureFilterResult *filtered) {
    const int32_t temperature = filtered->filtered_millicelsius;
    const int32_t median = filtered->median_millicelsius;
    if (measurement->millicelsius < -40000 || measurement->millicelsius > 125000 ||
        temperature < -40000 || temperature > 125000 ||
        median < -40000 || median > 125000) {
        return; /* Una falla de sensor no enciende ni silencia el aviso. */
    }

    if (!temperature_alarm_active &&
        (temperature >= TEMPERATURE_ALARM_ON_MILLIC || median >= TEMPERATURE_ALARM_ON_MILLIC)) {
        temperature_alarm_active = true;
        if (speaker_sine_ready) speaker_sine_ready = SpeakerSine_Start();
    } else if (temperature_alarm_active &&
               temperature <= TEMPERATURE_ALARM_OFF_MILLIC && median <= TEMPERATURE_ALARM_OFF_MILLIC) {
        SpeakerSine_Stop();
        temperature_alarm_active = false;
    }
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
                    /* REF confirma recepcion; el rearme depende de la medicion. */
                    const uint32_t primask = __get_PRIMASK();
                    __disable_irq();
                    synchronize_watchdog();
                    TemperatureController_SetReference(&temperature_controller,
                                                       (float)reference / 1000.0f);
                    __set_PRIMASK(primask);
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

static void report_sample(const TemperatureMeasurement *measurement,
                          const TemperatureFilterResult *filtered) {
    const int32_t temperature = filtered->filtered_millicelsius;
    /* Ampliar antes de negar para admitir tambien el minimo de int32_t. */
    const int64_t magnitude = temperature < 0 ? -(int64_t)temperature : (int64_t)temperature;
    const char *sign = temperature < 0 ? "-" : "";
    const long degrees = (long)(magnitude / 1000);
    const long fraction = (long)(magnitude % 1000);
    const uint32_t count = temperature_sample_count + 1U;

    temperature_adc = measurement->raw_adc;
    temperature_mv = measurement->millivolts;
    temperature_millicelsius = measurement->millicelsius;
    temperature_median_millicelsius = filtered->median_millicelsius;
    temperature_filtered_millicelsius = temperature;
    temperature_sample_count = count;

    /* Temperatura filtrada en °C, conservando el protocolo de la webapp. */
    char line[16];
    const int length = snprintf(line, sizeof line, "%s%ld.%03ld\r\n",
                                sign, degrees, fraction);
    if (length > 0 && (size_t)length < sizeof line) {
        send_uart(line, (uint32_t)length);
    } else {
        ++temperature_uart_error_count;
    }

    printf("N=%lu ADC=%u V=%lu mV T=%s%ld.%03ld C tick=%lu REF_mC=%lu DUTY=%lu STATE=%u BOOST=%u BASE=%lu I_mPct=%ld DROP=%lu\n",
           (unsigned long)count,
           (unsigned int)measurement->raw_adc, (unsigned long)measurement->millivolts,
           sign, degrees, fraction, (unsigned long)sample_tick,
           (unsigned long)temperature_reference_millicelsius,
           (unsigned long)heater_duty_percent, (unsigned int)temperature_control_state,
           (unsigned int)temperature_controller.preheating,
           (unsigned long)(temperature_controller.configured ?
               temperature_controller.config.maintenance_percent : 0.0f),
           (long)(temperature_controller.integral_percent * 1000.0f),
           (unsigned long)temperature_sensor_invalid_count);
}

int main(void) {
    /* Redlib Semihost dirige stdout a la consola del depurador. */
    setvbuf(stdout, NULL, _IONBF, 0);
    SystemCoreClockUpdate();
    ActuatorsPWM_Init();
    ActuatorsPWM_SetCoolerDuty(COOLER_INITIAL_DUTY_PERCENT);
    TemperatureController_Init(&temperature_controller, &TEMPERATURE_CONTROLLER_DEFAULT_CONFIG);
    temperature_control_state = temperature_controller.state;
    speaker_sine_ready = SpeakerSine_Init();
    TemperatureSensor_Init();
    init_telemetry();
    SYSTICK_InternalInit(SAMPLE_PERIOD_MS);
    SYSTICK_IntCmd(ENABLE);
    SYSTICK_Cmd(ENABLE);

    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    uint32_t last_tick = 0;
    uint32_t previous_sample_tick = 0;
    while (1) {
        TemperatureMeasurement measurement;
        last_tick = wait_for_sample(last_tick);
        poll_reference();
        if (read_temperature(&measurement)) {
            const uint32_t current_tick = sample_tick;
#if TEMPERATURE_CONTROL_PROTOTYPE_MODE
            if (measurement.millicelsius < -40000 || measurement.millicelsius > 125000) {
                /* Descartar antes de los filtros: un pico no debe contaminar
                 * la mediana ni el IIR de las adquisiciones siguientes. */
                const uint32_t primask = __get_PRIMASK();
                __disable_irq();
                synchronize_watchdog();
                trip_control(TEMPERATURE_CONTROL_FAULT_SENSOR);
                __set_PRIMASK(primask);
                ++temperature_sensor_invalid_count;
                last_tick = current_tick;
                previous_sample_tick = current_tick;
                printf("CONTROL SAMPLE_DISCARDED ADC=%u tick=%lu\n",
                       (unsigned int)measurement.raw_adc, (unsigned long)current_tick);
                continue;
            }
#endif
            if (current_tick - previous_sample_tick >= ACQUISITION_TIMEOUT_TICKS) {
                TemperatureFilter_Reset(&filter);
            }
            const TemperatureFilterResult filtered =
                TemperatureFilter_Process(&filter, measurement.millicelsius);
            update_control(&measurement, &filtered, current_tick - previous_sample_tick);
            update_temperature_alarm(&measurement, &filtered);
            /* No reutilizar un tick que haya transcurrido durante RX o el ADC. */
            last_tick = current_tick;
            if (measurement.millicelsius < -40000 || measurement.millicelsius > 125000) {
                TemperatureFilter_Reset(&filter);
            }
            previous_sample_tick = current_tick;
            report_sample(&measurement, &filtered);
        } else {
            const uint32_t primask = __get_PRIMASK();
            __disable_irq();
            synchronize_watchdog();
            trip_control(TEMPERATURE_CONTROL_FAULT_ADC);
            __set_PRIMASK(primask);
            ++temperature_adc_timeout_count;
            TemperatureFilter_Reset(&filter);
            last_tick = sample_tick;
            printf("CONTROL ADC_TIMEOUT STATE=%u DUTY=0 tick=%lu\n",
                   (unsigned int)temperature_control_state, (unsigned long)sample_tick);
        }
    }
}
