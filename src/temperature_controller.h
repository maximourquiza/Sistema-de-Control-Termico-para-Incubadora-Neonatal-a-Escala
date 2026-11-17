#ifndef TEMPERATURE_CONTROLLER_H
#define TEMPERATURE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/* Modo de ensayo: solo la sobretemperatura confirmada enclava el control.
 * Definir a 0 para recuperar los bloqueos inmediatos por otras fallas. */
#ifndef TEMPERATURE_CONTROL_PROTOTYPE_MODE
#define TEMPERATURE_CONTROL_PROTOTYPE_MODE 1
#endif
#define TEMPERATURE_CONTROL_OVERTEMP_CONFIRM_MS 2000U
#define TEMPERATURE_CONTROL_PREHEAT_ENTER_ERROR_C 2.0f
#define TEMPERATURE_CONTROL_PREHEAT_EXIT_ERROR_C 1.0f

typedef struct {
    float kp;                 /* % / C */
    float ki;                 /* % / (C s) */
    float kd;                 /* % s / C */
    float derivative_tau_s;
    float maintenance_percent; /* Potencia base 0..100 %, corregible por el PI. */
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
    bool preheating;
    uint32_t overtemperature_ms;
} TemperatureController;

/* Ganancias provisionales para puesta a punto en placa. */
extern const TemperatureControllerConfig TEMPERATURE_CONTROLLER_DEFAULT_CONFIG;

bool TemperatureController_Init(TemperatureController *controller,
                                const TemperatureControllerConfig *config);
/* Conserva ganancias y vuelve a esperar una nueva consigna. */
void TemperatureController_Reset(TemperatureController *controller);
bool TemperatureController_SetReference(TemperatureController *controller, float reference_c);
/* En modo prototipo solo enclava OVERTEMP. SENSOR conserva la integral y
 * reinicia derivada/confirmacion termica; las otras fallas reinician el PID.
 * El llamador pausa la salida, conservando consigna y estado de ejecucion. */
void TemperatureController_Trip(TemperatureController *controller, TemperatureControlState fault);
/* Una llamada por muestra valida. Unidades C y segundos. Devuelve duty 0..100.
 * El llamador descarta lecturas crudas invalidas y notifica timeouts con Trip.
 * No compartir la instancia entre main e interrupciones. */
uint32_t TemperatureController_Update(TemperatureController *controller,
                                      float filtered_c, float median_c, float elapsed_s);

#endif
