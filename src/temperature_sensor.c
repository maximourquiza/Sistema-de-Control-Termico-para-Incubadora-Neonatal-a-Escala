#include "temperature_sensor.h"
#include "lpc17xx_adc.h"

#define TEMPERATURE_ADC_CHANNEL   ADC_CHANNEL_0  /* P0.23 / AD0.0 */
#define ADC_CONVERSION_RATE_HZ    100000U
#define ADC_VREF_MV               3300U
#define ADC_MAX_CODE              4095U
#define TMP36_ZERO_MV             500
#define TMP36_MILLIC_PER_MV       100  /* 10 mV por grado Celsius. */

#if ADC_VREF_MV > (UINT32_MAX - ADC_MAX_CODE / 2U) / ADC_MAX_CODE / TMP36_MILLIC_PER_MV
#error La conversion de temperatura excede el rango de uint32_t
#endif

void TemperatureSensor_Init(void) {
    ADC_Init(ADC_CONVERSION_RATE_HZ);
    ADC_PinConfig(TEMPERATURE_ADC_CHANNEL);
    ADC_ChannelEnable(TEMPERATURE_ADC_CHANNEL);
    ADC_BurstDisable();

    /* Este módulo consulta el ADC por sondeo. */
    for (uint32_t source = ADC_INT_CH0; source <= ADC_INT_GLOBAL; ++source) {
        ADC_IntDisable((ADC_INT_SOURCE)source);
    }
}

void TemperatureSensor_Start(void) {
    ADC_StartCmd(ADC_START_NOW);
}

bool TemperatureSensor_TryRead(TemperatureMeasurement *measurement) {
    if (ADC_ChannelGetStatus(TEMPERATURE_ADC_CHANNEL, ADC_DATA_DONE) == RESET) {
        return false;
    }

    const uint16_t raw = ADC_ChannelGetData(TEMPERATURE_ADC_CHANNEL);
    ADC_StartCmd(ADC_START_CONTINUOUS); /* START=0 con BURST desactivado. */

    const uint32_t scaled_voltage = (uint32_t)raw * ADC_VREF_MV;
    measurement->raw_adc = raw;
    measurement->millivolts = (scaled_voltage + ADC_MAX_CODE / 2U) / ADC_MAX_CODE;
    measurement->millicelsius = (int32_t)(
        (scaled_voltage * TMP36_MILLIC_PER_MV + ADC_MAX_CODE / 2U) / ADC_MAX_CODE
    ) - TMP36_ZERO_MV * TMP36_MILLIC_PER_MV;
    return true;
}

void TemperatureSensor_Cancel(void) {
    ADC_StartCmd(ADC_START_CONTINUOUS);
    (void)ADC_ChannelGetData(TEMPERATURE_ADC_CHANNEL);
}
