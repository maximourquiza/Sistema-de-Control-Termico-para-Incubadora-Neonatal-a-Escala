#ifndef SPEAKER_SINE_H
#define SPEAKER_SINE_H

#include <stdbool.h>

/* Llamar una vez despues de SystemCoreClockUpdate(). Reserva DMA 7 y P0.26.
 * Prepara el tono de 1 kHz y deja el DAC fijo en 512 cuentas, sin sonido. */
bool SpeakerSine_Init(void);

/* Solo desde main. Start devuelve false si no se puede preparar el canal.
 * Las llamadas repetidas no reinician una reproduccion ya activa. */
bool SpeakerSine_Start(void);
void SpeakerSine_Stop(void);

#endif
