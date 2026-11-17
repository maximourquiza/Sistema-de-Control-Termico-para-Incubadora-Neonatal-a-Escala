#ifndef TEMPERATURE_CONTROLLER_H
#define TEMPERATURE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float kp;                 /* % / C */
    float ki;                 /* % / (C s) */
    float kd;                 /* % s / C */
    float derivative_tau_s;
} TemperatureControllerConfig;

typedef enum {
    TEMPERATURE_CONTROL_WAITING,
    TEMPERATURE_CONTROL_RUNNING,
    TEMPERATURE_CONTROL_FAULT_ADC,
    TEMPERATURE_CONTROL_FAULT_SENSOR,
    TEMPERATURE_CONTROL_FAULT_OVERTEMP,
    TEMPERATURE_CONTROL_FAULT_GAP,
    TEMPERATURE_CONTROL_FAULT_CONFIG
} TemperatureControlState;

typedef struct {
    TemperatureControllerConfig config;
    TemperatureControlState state;
    float reference_c;
    float integral_percent;
    float previous_temperature_c;
    float derivative_c_per_s;
    bool derivative_initialized;
    bool reference_pending;
    bool configured;
} TemperatureController;

/* Ganancias provisionales para puesta a punto en placa. */
extern const TemperatureControllerConfig TEMPERATURE_CONTROLLER_DEFAULT_CONFIG;

bool TemperatureController_Init(TemperatureController *controller,
                                const TemperatureControllerConfig *config);
/* Conserva ganancias y vuelve a esperar una nueva consigna. */
void TemperatureController_Reset(TemperatureController *controller);
bool TemperatureController_SetReference(TemperatureController *controller, float reference_c);
/* Enclava el fallo y elimina solicitudes anteriores de rearme. */
void TemperatureController_Trip(TemperatureController *controller, TemperatureControlState fault);
/* Una llamada por muestra valida. Unidades C y segundos. Devuelve duty 0..100.
 * El llamador valida la lectura cruda y notifica timeouts mediante Trip.
 * No compartir la instancia entre main e interrupciones. */
uint32_t TemperatureController_Update(TemperatureController *controller,
                                      float filtered_c, float median_c, float elapsed_s);

#endif
