/* WLED PWM hardware driver
 *
 * Hardware only: MCU TMR1_CH2(PB0) -> external boost -> LED.
 * Brightness policy, thermal protection, ramping and SW6306 metering coordination
 * live in Applications/algorithm/wled_algo.*.
 */
#ifndef __WLED_H__
#define __WLED_H__

#include <stdint.h>

#define WLED_PWM_MAX 1023U

void WLED_Init(void);
void WLED_SetPwm(uint16_t pwm);
uint16_t WLED_GetPwm(void);

#endif
