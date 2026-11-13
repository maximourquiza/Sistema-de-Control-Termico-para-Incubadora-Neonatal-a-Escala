#include "temperature_filter.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static int test_initialization_and_constant(void) {
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    CHECK(!filter.initialized);
    for (unsigned int i = 0; i < 30; ++i) {
        const TemperatureFilterResult result = TemperatureFilter_Process(&filter, 36500);
        CHECK(result.median_millicelsius == 36500);
        CHECK(result.filtered_millicelsius == 36500);
    }
    return 0;
}

static int test_redlib_repeated_31000(void) {
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    for (unsigned int i = 0; i < 300; ++i) {
        const TemperatureFilterResult result = TemperatureFilter_Process(&filter, 31000);
        CHECK(result.median_millicelsius == 31000);
        CHECK(result.filtered_millicelsius == 31000);
    }
    return 0;
}

static int test_spikes(void) {
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    TemperatureFilter_Process(&filter, 35000);
    const int32_t samples[] = {35000, 80000, 35000, 35000, -20000, 35000, 35000};
    for (unsigned int i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        const TemperatureFilterResult result = TemperatureFilter_Process(&filter, samples[i]);
        CHECK(result.median_millicelsius == 35000);
        CHECK(result.filtered_millicelsius == 35000);
    }
    return 0;
}

static int test_median_orderings(void) {
    const int32_t inputs[6][3] = {{1,2,3},{1,3,2},{2,1,3},{2,3,1},{3,1,2},{3,2,1}};
    for (unsigned int i = 0; i < 6; ++i) {
        TemperatureFilter filter;
        TemperatureFilter_Reset(&filter);
        TemperatureFilter_Process(&filter, inputs[i][0]);
        TemperatureFilter_Process(&filter, inputs[i][1]);
        CHECK(TemperatureFilter_Process(&filter, inputs[i][2]).median_millicelsius == 2);
    }
    return 0;
}

static int test_step(void) {
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    TemperatureFilter_Process(&filter, 35000);
    CHECK(TemperatureFilter_Process(&filter, 36000).filtered_millicelsius == 35000);
    CHECK(TemperatureFilter_Process(&filter, 36000).filtered_millicelsius == 35200);
    CHECK(TemperatureFilter_Process(&filter, 36000).filtered_millicelsius == 35360);
    int32_t previous = 35360;
    for (unsigned int i = 0; i < 60; ++i) {
        const TemperatureFilterResult result = TemperatureFilter_Process(&filter, 36000);
        CHECK(result.filtered_millicelsius >= previous);
        CHECK(result.filtered_millicelsius <= 36000);
        previous = result.filtered_millicelsius;
    }
    CHECK(previous == 36000);
    return 0;
}

static int test_negative_and_rounding(void) {
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    CHECK(TemperatureFilter_Process(&filter, -1000).filtered_millicelsius == -1000);
    TemperatureFilter_Process(&filter, -1003);
    CHECK(TemperatureFilter_Process(&filter, -1003).filtered_millicelsius == -1001);
    TemperatureFilter_Reset(&filter);
    TemperatureFilter_Process(&filter, 1000);
    TemperatureFilter_Process(&filter, 1003);
    CHECK(TemperatureFilter_Process(&filter, 1003).filtered_millicelsius == 1001);
    TemperatureFilter_Reset(&filter);
    TemperatureFilter_Process(&filter, -1000);
    int32_t last = -1000;
    for (unsigned int i = 0; i < 60; ++i) {
        last = TemperatureFilter_Process(&filter, 1000).filtered_millicelsius;
        CHECK(last >= -1000 && last <= 1000);
    }
    CHECK(last == 1000);
    return 0;
}

static int test_fractional_state(void) {
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    TemperatureFilter_Process(&filter, 0);
    int32_t last = 0;
    for (unsigned int i = 0; i < 30; ++i) {
        last = TemperatureFilter_Process(&filter, 1).filtered_millicelsius;
    }
    CHECK(last == 1); /* No perder la fraccion en cada iteracion. */
    return 0;
}

static int test_reset_after_gap(void) {
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    TemperatureFilter_Process(&filter, 36000);
    TemperatureFilter_Process(&filter, 80000);
    TemperatureFilter_Reset(&filter); /* El main lo ejecuta al cancelar el ADC. */
    CHECK(!filter.initialized);
    const TemperatureFilterResult result = TemperatureFilter_Process(&filter, -2500);
    CHECK(result.median_millicelsius == -2500);
    CHECK(result.filtered_millicelsius == -2500);
    CHECK(TemperatureFilter_Process(&filter, -2500).filtered_millicelsius == -2500);
    return 0;
}

static int test_integer_limits(void) {
    const int32_t minimum = -INT32_MAX - 1;
    TemperatureFilter filter;
    TemperatureFilter_Reset(&filter);
    CHECK(TemperatureFilter_Process(&filter, INT32_MAX).filtered_millicelsius == INT32_MAX);
    CHECK(TemperatureFilter_Process(&filter, INT32_MAX).filtered_millicelsius == INT32_MAX);
    TemperatureFilter_Reset(&filter);
    CHECK(TemperatureFilter_Process(&filter, minimum).filtered_millicelsius == minimum);
    CHECK(TemperatureFilter_Process(&filter, minimum).filtered_millicelsius == minimum);
    return 0;
}

/* Devuelve cero o la linea de la primera comprobacion fallida. */
int run_filter_tests(void) {
    int result;
#define RUN(name) do { result = name(); if (result) return result; } while (0)
    RUN(test_initialization_and_constant);
    RUN(test_redlib_repeated_31000);
    RUN(test_spikes);
    RUN(test_median_orderings);
    RUN(test_step);
    RUN(test_negative_and_rounding);
    RUN(test_fractional_state);
    RUN(test_reset_after_gap);
    RUN(test_integer_limits);
    return 0;
}

/* Evitar depender de un runtime de inicio al ejecutar las pruebas ARM. */
void *memset(void *destination, int value, unsigned int size) {
    unsigned char *bytes = destination;
    for (unsigned int i = 0; i < size; ++i) bytes[i] = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, unsigned int size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (unsigned int i = 0; i < size; ++i) out[i] = in[i];
    return destination;
}
