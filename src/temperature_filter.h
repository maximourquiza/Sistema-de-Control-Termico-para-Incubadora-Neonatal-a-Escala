#ifndef TEMPERATURE_FILTER_H
#define TEMPERATURE_FILTER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t samples[3];
    uint32_t next_sample;
    float filtered_millicelsius;
    bool initialized;
} TemperatureFilter;

typedef struct {
    int32_t median_millicelsius;
    int32_t filtered_millicelsius;
} TemperatureFilterResult;

/* Reiniciar antes del primer uso y despues de una interrupcion de adquisicion. */
void TemperatureFilter_Reset(TemperatureFilter *filter);

/* Procesar solo conversiones completadas. Entrada y salida en miligrados Celsius.
 * La primera muestra inicializa ambas etapas, sin transitorio desde cero. */
TemperatureFilterResult TemperatureFilter_Process(TemperatureFilter *filter,
                                                 int32_t millicelsius);

#endif
