#include "wled.h"
#include "at32f423.h"

static uint16_t s_pwm;

void WLED_Init(void)
{
    s_pwm = 0U;
    tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, 0U);
}

void WLED_SetPwm(uint16_t pwm)
{
    if (pwm > WLED_PWM_MAX) pwm = WLED_PWM_MAX;
    if (pwm == s_pwm) return;

    s_pwm = pwm;
    tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, pwm);
}

uint16_t WLED_GetPwm(void)
{
    return s_pwm;
}
