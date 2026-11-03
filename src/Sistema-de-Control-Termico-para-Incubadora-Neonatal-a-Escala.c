#include "LPC17xx.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_systick.h"
#include <stdio.h>

#define SAMPLE_PERIOD_MS 100U
#define ADC_VREF_MV      3300U

/* TMP36: 500 mV a 0 C y pendiente de 10 mV/C. */
volatile uint16_t temperature_adc;
volatile uint32_t temperature_mv;
volatile int32_t temperature_millicelsius;
volatile uint32_t temperature_sample_count;
static volatile uint32_t sample_due;

void SysTick_Handler(void) {
	sample_due = 1;
}

int main(void) {
	/* Redlib Semihost envia stdout a la consola del depurador. */
	setvbuf(stdout, NULL, _IONBF, 0);
	SystemCoreClockUpdate();
	ADC_Init(100000U); /* Velocidad de conversion, independiente del periodo. */
	ADC_PinConfig(ADC_CHANNEL_0); /* P0.23 / AD0.0, sin pull-up ni pull-down. */
	ADC_ChannelEnable(ADC_CHANNEL_0);
	ADC_BurstDisable();
	/* Se consulta DONE desde main: no se usa la interrupcion del ADC. */
	for (uint32_t source = ADC_INT_CH0; source <= ADC_INT_GLOBAL; source++) {
		ADC_IntDisable((ADC_INT_SOURCE)source);
	}
	SYSTICK_InternalInit(SAMPLE_PERIOD_MS);
	SYSTICK_IntCmd(ENABLE);
	SYSTICK_Cmd(ENABLE);
	while (1) {
		if (sample_due) {
			sample_due = 0;
			ADC_StartCmd(ADC_START_NOW);
			while (ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE) == RESET) {
				/* Espera breve hasta completar esta conversion. */
			}
			temperature_adc = ADC_ChannelGetData(ADC_CHANNEL_0);
			ADC_StartCmd(ADC_START_CONTINUOUS); /* START=0; BURST desactivado. */
			temperature_mv = ((uint32_t)temperature_adc * ADC_VREF_MV + 2047U) / 4095U;
			temperature_millicelsius = (int32_t)(
				((uint64_t)temperature_adc * ADC_VREF_MV * 100U + 2047U) / 4095U) - 50000;
			temperature_sample_count++;
			/* Formato decimal con enteros: no requiere printf con punto flotante. */
			const int32_t temperature = temperature_millicelsius;
			const int32_t magnitude = temperature < 0 ? -temperature : temperature;
			printf("N=%lu ADC=%u V=%lu mV T=%s%ld.%03ld C\n",
				(unsigned long)temperature_sample_count,
				(unsigned int)temperature_adc,
				(unsigned long)temperature_mv,
				temperature < 0 ? "-" : "",
				(long)(magnitude / 1000), (long)(magnitude % 1000));
		} else {
			__WFI();
		}
	}
    return 0 ;
}
