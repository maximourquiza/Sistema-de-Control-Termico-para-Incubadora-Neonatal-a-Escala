#include "temperature_controller.h"

#include <float.h>

const TemperatureControllerConfig TEMPERATURE_CONTROLLER_DEFAULT_CONFIG = {
    .kp = 60.0f,
    .ki = 0.20f,
    .kd = 0.0f,
    .derivative_tau_s = 1.0f,
    .maintenance_percent = 30.0f
};

static bool finite_float(float value) {
    return value >= -FLT_MAX && value <= FLT_MAX;
}

static float clamp_output(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

static void reset_derivative(TemperatureController *controller) {
    controller->previous_temperature_c = 0.0f;
    controller->derivative_c_per_s = 0.0f;
    controller->derivative_initialized = false;
}

static void reset_regulation(TemperatureController *controller) {
    controller->integral_percent = 0.0f;
    reset_derivative(controller);
}

static void reset_pid(TemperatureController *controller) {
    reset_regulation(controller);
    controller->overtemperature_ms = 0;
}

void TemperatureController_Reset(TemperatureController *controller) {
    reset_pid(controller);
    controller->reference_c = 0.0f;
    controller->reference_pending = false;
    controller->preheating = false;
#if TEMPERATURE_CONTROL_PROTOTYPE_MODE
    controller->state = TEMPERATURE_CONTROL_WAITING;
#else
    controller->state = controller->configured ? TEMPERATURE_CONTROL_WAITING :
                                               TEMPERATURE_CONTROL_FAULT_CONFIG;
#endif
}

bool TemperatureController_Init(TemperatureController *controller,
                                const TemperatureControllerConfig *config) {
    *controller = (TemperatureController){0};
    controller->config = *config;
    controller->configured = finite_float(config->kp) && config->kp >= 0.0f &&
        finite_float(config->ki) && config->ki >= 0.0f &&
        finite_float(config->kd) && config->kd >= 0.0f &&
        finite_float(config->derivative_tau_s) && config->derivative_tau_s > 0.0f &&
        finite_float(config->maintenance_percent) && config->maintenance_percent >= 0.0f &&
        config->maintenance_percent <= 100.0f;
    TemperatureController_Reset(controller);
    return controller->configured;
}

bool TemperatureController_SetReference(TemperatureController *controller, float reference_c) {
    if (!controller->configured || !(reference_c >= 34.0f && reference_c <= 37.0f)) {
        return false;
    }
    controller->reference_c = reference_c;
    controller->reference_pending = true;
    return true;
}

void TemperatureController_Trip(TemperatureController *controller, TemperatureControlState fault) {
#if TEMPERATURE_CONTROL_PROTOTYPE_MODE
    if (fault == TEMPERATURE_CONTROL_FAULT_SENSOR) {
        /* Descartar una lectura no debe borrar la potencia de equilibrio ya
         * aprendida. No usar el hueco para calcular derivada ni confirmar calor. */
        reset_derivative(controller);
        controller->overtemperature_ms = 0;
        return;
    }
#endif
    reset_pid(controller);
#if TEMPERATURE_CONTROL_PROTOTYPE_MODE
    /* Pausar la salida sin perder la consigna ni exigir rearme. */
    if (fault != TEMPERATURE_CONTROL_FAULT_OVERTEMP) return;
#endif
    controller->preheating = false;
    controller->reference_pending = false;
    /* Conservar el primer motivo hasta el rearme. */
    if (controller->state < TEMPERATURE_CONTROL_FAULT_ADC) {
        controller->state = fault;
    }
}

uint32_t TemperatureController_Update(TemperatureController *controller,
                                      float filtered_c, float median_c, float elapsed_s) {
    if (!controller->configured) return 0;
    if (!finite_float(filtered_c) || !finite_float(median_c) ||
        filtered_c < -40.0f || filtered_c > 125.0f ||
        median_c < -40.0f || median_c > 125.0f) {
        TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_SENSOR);
        return 0;
    }
#if TEMPERATURE_CONTROL_PROTOTYPE_MODE
    if (!(elapsed_s > 0.0f && elapsed_s < 1.0f)) {
        TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_GAP);
        return 0;
    }
    /* Ambas etapas deben confirmar calor real. La cola del IIR por si sola
     * no prolonga un pico cuya mediana ya ha vuelto al rango normal. */
    if (filtered_c >= TEMPERATURE_BLOCK_THRESHOLD_C && median_c >= TEMPERATURE_BLOCK_THRESHOLD_C) {
        const uint32_t elapsed_ms = (uint32_t)(elapsed_s * 1000.0f + 0.5f);
        if (controller->overtemperature_ms < TEMPERATURE_CONTROL_OVERTEMP_CONFIRM_MS) {
            controller->overtemperature_ms += elapsed_ms;
        }
        if (controller->overtemperature_ms >= TEMPERATURE_CONTROL_OVERTEMP_CONFIRM_MS) {
            TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_OVERTEMP);
            return 0;
        }
    } else {
        controller->overtemperature_ms = 0;
    }
#else
    if (filtered_c >= TEMPERATURE_BLOCK_THRESHOLD_C || median_c >= TEMPERATURE_BLOCK_THRESHOLD_C) {
        TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_OVERTEMP);
        return 0;
    }
    if (!(elapsed_s > 0.0f && elapsed_s < 1.0f)) {
        TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_GAP);
        return 0;
    }
#endif

    if (controller->state != TEMPERATURE_CONTROL_RUNNING) {
        const bool requested = controller->reference_pending;
        controller->reference_pending = false;
        const bool fault = controller->state >= TEMPERATURE_CONTROL_FAULT_ADC;
        if (!requested || (fault && (filtered_c >= TEMPERATURE_BLOCK_REARM_C ||
                                     median_c >= TEMPERATURE_BLOCK_REARM_C))) {
            return 0;
        }
        controller->state = TEMPERATURE_CONTROL_RUNNING;
    }
    controller->reference_pending = false;

    /* La lectura mas alta evita prolongar el 100 % por el retardo del IIR. */
    const float warmest_c = filtered_c > median_c ? filtered_c : median_c;
    const float preheat_error = controller->reference_c - warmest_c;
    const bool was_preheating = controller->preheating;
    if (controller->preheating) {
        if (preheat_error <= TEMPERATURE_CONTROL_PREHEAT_EXIT_ERROR_C) {
            controller->preheating = false;
        }
    } else if (preheat_error >= TEMPERATURE_CONTROL_PREHEAT_ENTER_ERROR_C) {
        controller->preheating = true;
    }
    if (was_preheating != controller->preheating) {
        /* No precargar integral ni producir un impulso derivativo al cambiar. */
        reset_regulation(controller);
    }
    if (controller->preheating) return 100U;

    const float error = controller->reference_c - filtered_c;
    if (controller->derivative_initialized) {
        const float rate = (filtered_c - controller->previous_temperature_c) / elapsed_s;
        const float alpha = elapsed_s / (controller->config.derivative_tau_s + elapsed_s);
        controller->derivative_c_per_s += alpha * (rate - controller->derivative_c_per_s);
    }
    controller->previous_temperature_c = filtered_c;
    controller->derivative_initialized = true;

    const float proportional = controller->config.kp * error;
    const float derivative = -controller->config.kd * controller->derivative_c_per_s;
    const float increment = controller->config.ki * error * elapsed_s;
    const float candidate_integral = controller->integral_percent + increment;
    const float candidate_output = controller->config.maintenance_percent +
        proportional + candidate_integral + derivative;
    if (!finite_float(candidate_output)) {
        TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_CONFIG);
        return 0;
    }
    /* Integrar dentro del rango o cuando la correccion ayuda a salir de saturacion. */
    if ((candidate_output >= 0.0f && candidate_output <= 100.0f) ||
        (candidate_output > 100.0f && increment < 0.0f) ||
        (candidate_output < 0.0f && increment > 0.0f)) {
        controller->integral_percent = candidate_integral;
    }
    const float output = clamp_output(controller->config.maintenance_percent +
        proportional + controller->integral_percent + derivative);
    return (uint32_t)(output + 0.5f);
}
