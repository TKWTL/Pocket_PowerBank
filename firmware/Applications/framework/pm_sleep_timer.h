#ifndef PM_SLEEP_TIMER_H
#define PM_SLEEP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pm_sleep_timer_init(uint32_t timeout_ms);
void pm_sleep_timer_set(uint32_t timeout_ms);
void pm_sleep_timer_refresh(void);
void pm_sleep_timer_force_expire(void);
uint32_t pm_sleep_timer_left_ms(void);
uint8_t pm_sleep_timer_expired(void);

#ifdef __cplusplus
}
#endif

#endif
