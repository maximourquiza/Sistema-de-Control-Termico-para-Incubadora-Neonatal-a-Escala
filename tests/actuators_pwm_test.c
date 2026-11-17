#include "actuators_pwm.h"
#include "LPC17xx.h"
#include "lpc17xx_pwm.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_clkpwr.h"
#include <stdbool.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint32_t cooler_period, cooler_match, heater_match;
static bool heater_enabled;
void TIMER1_IRQHandler(void);

uint32_t CLKPWR_GetPCLK(uint32_t clock) { (void)clock; return 25000000; }
void PWM_InitTimer(const PWM_TIMERCFG_T *config) { (void)config; }
void PWM_ConfigMatch(const PWM_MATCHCFG_T *config) {
    if (config->channel == PWM_MATCH_0) cooler_period = config->matchValue;
    else cooler_match = config->matchValue;
}
void PWM_PinConfig(PWM_PIN_OPTION pin) { (void)pin; }
void PWM_ChannelConfig(PWM_CHANNEL channel, PWM_CHANNEL_EDGE edge) { (void)channel; (void)edge; }
void PWM_ChannelEnable(PWM_CHANNEL channel) { (void)channel; }
void PWM_ResetCounter(void) {}
void PWM_Enable(void) {}
void PWM_CounterEnable(void) {}
void PWM_MatchUpdateSingle(PWM_MATCH_OPT channel, uint32_t ticks) {
    (void)channel;
    cooler_match = ticks;
}
void TIM_InitTimer(LPC_TIM_TypeDef *timer, const TIM_TIMERCFG_T *config) {
    (void)timer; (void)config;
}
void TIM_ConfigMatch(LPC_TIM_TypeDef *timer, const TIM_MATCHCFG_T *config) {
    (void)timer; heater_match = config->matchValue;
}
void TIM_Disable(LPC_TIM_TypeDef *timer) { (void)timer; heater_enabled = false; }
void TIM_Enable(LPC_TIM_TypeDef *timer) { (void)timer; heater_enabled = true; }
void TIM_PinConfig(TIM_PIN_OPTION pin) { (void)pin; }
void TIM_ClearIntPending(LPC_TIM_TypeDef *timer, TIM_INT source) { (void)timer; (void)source; }
void TIM_UpdateMatchValue(LPC_TIM_TypeDef *timer, TIM_MATCH_CH channel, uint32_t ticks) {
    (void)timer; (void)channel; heater_match = ticks;
}
void TIM_ResetCounter(LPC_TIM_TypeDef *timer) { (void)timer; }

int run_actuators_pwm_tests(void) {
    ActuatorsPWM_Init();
    CHECK((LPC_TIM1->EMR & 1U) != 0 && !heater_enabled);
    CHECK(cooler_match == cooler_period + 1U);
    ActuatorsPWM_SetCoolerDuty(25);
    const uint32_t fixed_cooler = cooler_match;
    CHECK(fixed_cooler == cooler_period * 75U / 100U);
    ActuatorsPWM_SetHeaterDuty(100);
    CHECK((LPC_TIM1->EMR & 1U) == 0 && !heater_enabled);
    ActuatorsPWM_SetHeaterDuty(0);
    CHECK((LPC_TIM1->EMR & 1U) != 0 && !heater_enabled);
    ActuatorsPWM_SetHeaterDuty(25);
    CHECK(heater_enabled && heater_match == 250000);
    LPC_TIM1->EMR |= 1U; /* Emular el toggle hardware al terminar la fase baja. */
    TIMER1_IRQHandler();
    CHECK(heater_match == 750000);
    ActuatorsPWM_SetHeaterDuty(75);
    LPC_TIM1->EMR &= ~1U;
    TIMER1_IRQHandler();
    CHECK(heater_match == 750000);
    LPC_TIM1->EMR |= 1U;
    TIMER1_IRQHandler();
    CHECK(heater_match == 250000);
    ActuatorsPWM_SetHeaterDuty(0);
    CHECK((LPC_TIM1->EMR & 1U) != 0 && !heater_enabled);
    CHECK(cooler_match == fixed_cooler);
    return 0;
}
