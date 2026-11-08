#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t raw_adc;
    uint32_t millivolts;
    int32_t millicelsius;
} TemperatureMeasurement;

/* Inicializar una vez, con el reloj del sistema ya actualizado. */
void TemperatureSensor_Init(void);

/* Una conversión a la vez. Leer hasta completar o cancelar antes de reiniciar. */
void TemperatureSensor_Start(void);
/* Si la conversión sigue pendiente, devuelve false y no modifica la medición. */
bool TemperatureSensor_TryRead(TemperatureMeasurement *measurement);
void TemperatureSensor_Cancel(void);

#endif
