#ifndef TEMPERATURE_TEST_CONFIG_H
#define TEMPERATURE_TEST_CONFIG_H

/* Seleccionar un solo perfil y recompilar/cargar el firmware.
 * NORMAL restaura los umbrales del montaje que funciona actualmente.
 * Estas pruebas usan el sensor real, no inyectan temperaturas simuladas. */
#define TEMPERATURE_TEST_NORMAL 0
#define TEMPERATURE_TEST_SPEAKER 1
#define TEMPERATURE_TEST_SPEAKER_AND_BLOCK 2

#ifndef TEMPERATURE_TEST_PROFILE
#define TEMPERATURE_TEST_PROFILE TEMPERATURE_TEST_NORMAL
#endif

#if TEMPERATURE_TEST_PROFILE == TEMPERATURE_TEST_NORMAL
#define TEMPERATURE_ALARM_ON_MILLIC 40000
#define TEMPERATURE_ALARM_OFF_MILLIC 39500
#define TEMPERATURE_BLOCK_THRESHOLD_C 40.0f
#define TEMPERATURE_BLOCK_REARM_C 38.0f
#elif TEMPERATURE_TEST_PROFILE == TEMPERATURE_TEST_SPEAKER
/* Con consigna 36 C, la bocina suena al alcanzar 35 C sin bloquear el PI. */
#define TEMPERATURE_ALARM_ON_MILLIC 35000
#define TEMPERATURE_ALARM_OFF_MILLIC 34500
#define TEMPERATURE_BLOCK_THRESHOLD_C 40.0f
#define TEMPERATURE_BLOCK_REARM_C 38.0f
#elif TEMPERATURE_TEST_PROFILE == TEMPERATURE_TEST_SPEAKER_AND_BLOCK
/* Comprobar corte a 35 C; enfriar por debajo de 34 C y reenviar consigna
 * para rearmar. El tiempo de confirmacion de 2 s permanece sin cambios. */
#define TEMPERATURE_ALARM_ON_MILLIC 35000
#define TEMPERATURE_ALARM_OFF_MILLIC 34500
#define TEMPERATURE_BLOCK_THRESHOLD_C 35.0f
#define TEMPERATURE_BLOCK_REARM_C 34.0f
#else
#error Perfil de prueba de temperatura no reconocido
#endif

#endif
