#include "actuators_pwm.h"
#include "LPC17xx.h"
#include "lpc17xx_clkpwr.h"
#include "lpc17xx_pwm.h"
#include "lpc17xx_timer.h"

#include <stdbool.h>

#define COOLER_PERIOD_US 45U
#define HEATER_PERIOD_US 1000000U
#define HEATER_MIN_PHASE_US 1000U
#define HEATER_OUTPUT_MASK 1UL

static uint32_t cooler_period_ticks;
static volatile uint32_t heater_high_us;
static volatile uint32_t heater_low_us;
static bool heater_constant;

void ActuatorsPWM_Init(void) {
    const PWM_TIMERCFG_T pwm_timer = {PWM_TICK, 1};
    PWM_InitTimer(&pwm_timer);
    cooler_period_ticks = (uint32_t)(
        (uint64_t)CLKPWR_GetPCLK(CLKPWR_PCLKSEL_PWM1) * COOLER_PERIOD_US / 1000000U);
    const PWM_MATCHCFG_T pwm_period = {
        .channel = PWM_MATCH_0,
        .intEn = DISABLE,
        .stopEn = DISABLE,
        .resetEn = ENABLE,
        .matchValue = cooler_period_ticks
    };
    const PWM_MATCHCFG_T pwm_duty = {
        .channel = PWM_MATCH_1,
        .intEn = DISABLE,
        .stopEn = DISABLE,
        .resetEn = DISABLE,
        .matchValue = 0
    };
    PWM_ConfigMatch(&pwm_period);
    PWM_ConfigMatch(&pwm_duty);
    PWM_PinConfig(PWM1_P2_0);
    PWM_ChannelConfig(PWM_CHANNEL_1, PWM_SINGLE_EDGE);
    PWM_ChannelEnable(PWM_CHANNEL_1);
    PWM_ResetCounter();
    PWM_Enable();
    PWM_CounterEnable();

    const TIM_TIMERCFG_T heater_timer = {TIM_US, 1};
    const TIM_MATCHCFG_T heater_match = {
        .channel = TIM_MATCH_0,
        .intEn = ENABLE,
        .stopEn = DISABLE,
        .resetEn = ENABLE,
        .extOpt = TIM_TOGGLE,
        .matchValue = HEATER_PERIOD_US
    };
    NVIC_DisableIRQ(TIMER1_IRQn);
    TIM_InitTimer(LPC_TIM1, &heater_timer);
    TIM_ConfigMatch(LPC_TIM1, &heater_match);
    LPC_TIM1->EMR &= ~HEATER_OUTPUT_MASK;
    TIM_PinConfig(TIM_MAT1_0_P1_22);
    heater_high_us = 0;
    heater_low_us = HEATER_PERIOD_US;
    heater_constant = true;
    TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);
}

void ActuatorsPWM_SetCoolerDuty(uint32_t percent) {
    if (percent > 100U) {
        percent = 100U;
    }
    /* MR1 por encima de MR0 mantiene la salida alta durante todo el periodo. */
    const uint32_t ticks = percent == 100U ? cooler_period_ticks + 1U :
        (uint32_t)((uint64_t)cooler_period_ticks * percent / 100U);
    PWM_MatchUpdateSingle(PWM_MATCH_1, ticks);
}

void ActuatorsPWM_SetHeaterDuty(uint32_t percent) {
    if (percent > 100U) {
        percent = 100U;
    }
    const uint32_t high_us = HEATER_PERIOD_US * percent / 100U;
    const uint32_t low_us = HEATER_PERIOD_US - high_us;
    if (high_us < HEATER_MIN_PHASE_US) {
        percent = 0;
    } else if (low_us < HEATER_MIN_PHASE_US) {
        percent = 100;
    }

    /* Actualizar el par de tiempos y el estado sin interferencia de Timer1.
     * Restaurar PRIMASK conserva el estado previo de las interrupciones. */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (percent == 0U || percent == 100U) {
        TIM_Disable(LPC_TIM1);
        TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
        NVIC_ClearPendingIRQ(TIMER1_IRQn);
        if (percent == 0U) {
            LPC_TIM1->EMR &= ~HEATER_OUTPUT_MASK;
        } else {
            LPC_TIM1->EMR |= HEATER_OUTPUT_MASK;
        }
        heater_constant = true;
    } else {
        heater_high_us = high_us;
        heater_low_us = low_us;
        if (heater_constant) {
            LPC_TIM1->EMR &= ~HEATER_OUTPUT_MASK;
            TIM_UpdateMatchValue(LPC_TIM1, TIM_MATCH_0, heater_low_us);
            TIM_ResetCounter(LPC_TIM1);
            TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
            NVIC_ClearPendingIRQ(TIMER1_IRQn);
            heater_constant = false;
            TIM_Enable(LPC_TIM1);
        }
    }
    __set_PRIMASK(primask);
}

void TIMER1_IRQHandler(void) {
    TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
    TIM_UpdateMatchValue(LPC_TIM1, TIM_MATCH_0,
        (LPC_TIM1->EMR & HEATER_OUTPUT_MASK) ? heater_high_us : heater_low_us);
}
