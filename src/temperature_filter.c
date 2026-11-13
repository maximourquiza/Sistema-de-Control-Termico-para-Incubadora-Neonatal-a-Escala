#include "temperature_filter.h"

/* Ajuste inicial para muestras separadas por 100 ms. */
#define TEMPERATURE_FILTER_ALPHA 0.2f

static int32_t median_of_three(int32_t a, int32_t b, int32_t c) {
    if (a > b) {
        const int32_t swap = a;
        a = b;
        b = swap;
    }
    if (b > c) {
        b = c;
    }
    return a > b ? a : b;
}

static int32_t round_millicelsius(float value) {
    /* Saturar antes de convertir evita desbordar incluso con entradas int32_t.
     * INT32_MAX se redondea a 2^31 al representarlo como float. */
    if (value >= (float)INT32_MAX) {
        return INT32_MAX;
    }
    /* Redlib define INT32_MIN como (-0x80000000), de tipo unsigned int.
     * Construir el minimo con signo antes de convertirlo a float. */
    const int32_t minimum = -INT32_MAX - 1;
    if (value <= (float)minimum) {
        return minimum;
    }
    return (int32_t)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

void TemperatureFilter_Reset(TemperatureFilter *filter) {
    *filter = (TemperatureFilter){0};
}

TemperatureFilterResult TemperatureFilter_Process(TemperatureFilter *filter,
                                                 int32_t millicelsius) {
    if (!filter->initialized) {
        for (uint32_t i = 0; i < 3U; ++i) {
            filter->samples[i] = millicelsius;
        }
        filter->next_sample = 0;
        filter->filtered_millicelsius = (float)millicelsius;
        filter->initialized = true;
        return (TemperatureFilterResult){millicelsius, millicelsius};
    }

    filter->samples[filter->next_sample] = millicelsius;
    filter->next_sample = (filter->next_sample + 1U) % 3U;
    const int32_t median = median_of_three(filter->samples[0], filter->samples[1],
                                          filter->samples[2]);
    filter->filtered_millicelsius += TEMPERATURE_FILTER_ALPHA *
        ((float)median - filter->filtered_millicelsius);

    return (TemperatureFilterResult){median, round_millicelsius(filter->filtered_millicelsius)};
}
