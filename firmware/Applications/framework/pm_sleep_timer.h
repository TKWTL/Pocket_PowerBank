#ifndef PM_SLEEP_TIMER_H
#define PM_SLEEP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* left_ms() 在“停止倒计时”时返回的指示值（视为无限剩余） */
#define PM_SLEEP_INFINITE   0xFFFFFFFFUL

void pm_sleep_timer_init(uint32_t timeout_ms);
void pm_sleep_timer_set(uint32_t timeout_ms);    /* 设置/更新超时并恢复倒计时 */
void pm_sleep_timer_disable(void);               /* 停止倒计时（不休眠）：expired 恒 0 */
void pm_sleep_timer_refresh(void);               /* 活动/唤醒：重载倒计时；暂停时重载冻结值 */
void pm_sleep_timer_pause(void);                 /* 常亮要求：冻结当前剩余倒计时 */
void pm_sleep_timer_resume(void);                /* 常亮解除：从冻结值继续倒计时 */
void pm_sleep_timer_force_expire(void);          /* 立刻休眠：置 force 标志（expired 恒 1） */
void pm_sleep_timer_clear_force(void);           /* 进入休眠分支后无条件清除 force 标志 */
uint32_t pm_sleep_timer_left_ms(void);
uint8_t pm_sleep_timer_expired(void);
uint8_t pm_sleep_timer_is_disabled(void);

#ifdef __cplusplus
}
#endif

#endif
