#ifndef SPEAKER_SINE_H
#define SPEAKER_SINE_H

#include <stdbool.h>

/* Llamar una vez despues de SystemCoreClockUpdate(). Reserva DMA 7 y P0.26.
 * Inicia un tono continuo de 1 kHz; devuelve false si no puede iniciarlo. */
bool SpeakerSine_Init(void);

#endif
