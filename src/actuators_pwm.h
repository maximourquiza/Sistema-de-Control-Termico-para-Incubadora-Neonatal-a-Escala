#ifndef ACTUATORS_PWM_H
#define ACTUATORS_PWM_H

#include <stdint.h>

/* Inicializar una vez, despues de SystemCoreClockUpdate(). Ambas salidas en 0 %. */
void ActuatorsPWM_Init(void);

/* Porcentaje de potencia, con etapa activa en bajo. 0 apaga, 100 enciende.
 * Valores mayores que 100 se limitan a 100. */
void ActuatorsPWM_SetCoolerDuty(uint32_t percent);
void ActuatorsPWM_SetHeaterDuty(uint32_t percent);

#endif
