#include "temperature_controller.h"

#include <float.h>

const TemperatureControllerConfig TEMPERATURE_CONTROLLER_DEFAULT_CONFIG = {
    .kp = 10.0f,
    .ki = 0.02f,
    .kd = 0.0f,
    .derivative_tau_s = 1.0f
};

static bool finite_float(float value) {
    return value >= -FLT_MAX && value <= FLT_MAX;
}

static float clamp_output(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

static void reset_pid(TemperatureController *controller) {
    controller->integral_percent = 0.0f;
    controller->previous_temperature_c = 0.0f;
    controller->derivative_c_per_s = 0.0f;
    controller->derivative_initialized = false;
}

void TemperatureController_Reset(TemperatureController *controller) {
    reset_pid(controller);
    controller->reference_c = 0.0f;
    controller->reference_pending = false;
    controller->state = controller->configured ? TEMPERATURE_CONTROL_WAITING :
                                               TEMPERATURE_CONTROL_FAULT_CONFIG;
}

bool TemperatureController_Init(TemperatureController *controller,
                                const TemperatureControllerConfig *config) {
    *controller = (TemperatureController){0};
    controller->config = *config;
    controller->configured = finite_float(config->kp) && config->kp >= 0.0f &&
        finite_float(config->ki) && config->ki >= 0.0f &&
        finite_float(config->kd) && config->kd >= 0.0f &&
        finite_float(config->derivative_tau_s) && config->derivative_tau_s > 0.0f;
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
    reset_pid(controller);
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
    if (filtered_c >= 40.0f || median_c >= 40.0f) {
        TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_OVERTEMP);
        return 0;
    }
    if (!(elapsed_s > 0.0f && elapsed_s < 1.0f)) {
        TemperatureController_Trip(controller, TEMPERATURE_CONTROL_FAULT_GAP);
        return 0;
    }

    if (controller->state != TEMPERATURE_CONTROL_RUNNING) {
        const bool requested = controller->reference_pending;
        controller->reference_pending = false;
        const bool fault = controller->state >= TEMPERATURE_CONTROL_FAULT_ADC;
        if (!requested || (fault && (filtered_c >= 38.0f || median_c >= 38.0f))) {
            return 0;
        }
        controller->state = TEMPERATURE_CONTROL_RUNNING;
    }
    controller->reference_pending = false;

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
    const float candidate_output = proportional + candidate_integral + derivative;
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
    const float output = clamp_output(proportional + controller->integral_percent + derivative);
    return (uint32_t)(output + 0.5f);
}
