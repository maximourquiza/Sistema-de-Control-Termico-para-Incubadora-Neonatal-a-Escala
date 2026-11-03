# Adquisicion periodica de temperatura

Implementacion minima en `src/Sistema-de-Control-Termico-para-Incubadora-Neonatal-a-Escala.c`,
utilizando los drivers ADC y SysTick de `../CMSISv2p00_LPC17xx`.
Importar y compilar ambos proyectos en MCUXpresso.

- Entrada analogica: P0.23 / AD0.0, resolucion de 12 bits.
- SysTick marca una lectura cada 100 ms (10 Hz).
- Main inicia una conversion, espera DONE y lee el resultado usando el driver.
- No se usan Timer0, DMA, buffer ni filtro. No hay acceso directo a registros.
- `SAMPLE_PERIOD_MS` define el periodo; `ADC_Init(100000)` define la velocidad
  interna de conversion, no la frecuencia de las lecturas.

Conectar la salida del sensor a P0.23 y compartir GND con la placa. La entrada
no debe superar VREFP. Ajustar `ADC_VREF_MV` a la referencia real (3300 mV inicial).
La conversion a temperatura usa TMP36, con 10 mV/C y offset de 500 mV; adaptar
la formula si se utiliza otro sensor.

Observar en el depurador:

- `temperature_adc`: lectura cruda (0 a 4095).
- `temperature_mv`: tension en milivoltios.
- `temperature_millicelsius`: temperatura en milesimas de grado Celsius.
- `temperature_sample_count`: cantidad de lecturas terminadas.

Para TMP36, una entrada de 750 mV debe producir aproximadamente 25000 miligrados C.
Verificar en placa que el contador avance unas 10 veces por segundo.
La espera de DONE es bloqueante; esta version simple no incluye timeout.

## Salida por semihosting

Debug y Release ya seleccionan Redlib Semihost en `.cproject`.
Ejecutar con una sesion de depuracion y habilitar semihosting en la configuracion
Debug de la sonda. Abrir la consola de semihosting en MCUXpresso.
Cada lectura imprime una linea, por ejemplo:

```text
N=1 ADC=931 V=750 mV T=25.026 C
```

La temperatura se calcula desde el ADC sin redondear primero a milivoltios.
Se imprimen enteros, sin habilitar soporte de printf para punto flotante.
El signo se conserva incluso para temperaturas entre -1 C y 0 C.

Semihosting necesita un depurador conectado y es bloqueante: puede alargar el
intervalo efectivo entre lecturas. La bandera de SysTick no acumula ticks;
si imprimir tarda mas de 100 ms, se omiten periodos en lugar de encolar muestras.
Para ejecutar sin depurador, quitar la salida y seleccionar una biblioteca sin
semihosting.