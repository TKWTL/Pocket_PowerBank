#include "pm_sleep_timer.h"

#include "FreeRTOS.h"
#include "task.h"

static TickType_t s_deadline;
static TickType_t s_pause_left;
static uint32_t s_timeout_ms;
static volatile uint8_t s_countdown_disabled;   /* 1=停止倒计时（不休眠）：expired 恒 0 */
static volatile uint8_t s_countdown_paused;     /* 1=常亮阻塞：冻结剩余倒计时 */
static volatile uint8_t s_force_sleep;          /* 1=立刻休眠：expired 恒 1；进入休眠分支后清除 */

static TickType_t pm_to_ticks(uint32_t ms)
{
    /* 用 64 位中间值计算，防止 (ms*HZ/1000) 在 32 位下溢出：
     * 如 0xFFFFFFFFms 经 pdMS_TO_TICKS 溢出后只剩约 71.6 分钟就“超时”。 */
    uint64_t t = (uint64_t)ms * (uint64_t)configTICK_RATE_HZ / 1000ULL;
    if (t > (uint64_t)0xFFFFFFFFUL) {
        return (TickType_t)0xFFFFFFFFUL;   /* 饱和到 TickType_t 最大值 */
    }
    return (TickType_t)t;
}

void pm_sleep_timer_init(uint32_t timeout_ms)
{
    s_timeout_ms = timeout_ms;
    s_countdown_disabled = 0;
    s_countdown_paused = 0;
    s_force_sleep = 0;
    s_pause_left = 0;
    s_deadline = xTaskGetTickCount() + pm_to_ticks(timeout_ms);
}

void pm_sleep_timer_set(uint32_t timeout_ms)
{
    TickType_t ticks = pm_to_ticks(timeout_ms);

    taskENTER_CRITICAL();
    s_timeout_ms = timeout_ms;
    s_countdown_disabled = 0;
    if (s_countdown_paused != 0) {
        s_pause_left = ticks;   /* 常亮期间改超时：继续暂停，并采用新的完整倒计时 */
    } else {
        s_deadline = xTaskGetTickCount() + ticks;
    }
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_disable(void)
{
    taskENTER_CRITICAL();
    /* 关闭休眠只需阻止倒计时递减（expired 恒 0）：
     * 不设极大超时值、不动 deadline——refresh 仍无条件重载，靠短路保证不超时。 */
    s_countdown_disabled = 1;
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_refresh(void)
{
    TickType_t ticks = pm_to_ticks(s_timeout_ms);

    taskENTER_CRITICAL();
    /* 常亮阻塞期间的活动只重载“冻结值”，不让倒计时偷偷流逝。 */
    if (s_countdown_paused != 0) {
        s_pause_left = ticks;
    } else {
        s_deadline = xTaskGetTickCount() + ticks;
    }
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_pause(void)
{
    TickType_t now;

    taskENTER_CRITICAL();
    if (s_countdown_paused == 0) {
        now = xTaskGetTickCount();
        s_pause_left = (now >= s_deadline) ? 0 : (s_deadline - now);
        s_countdown_paused = 1;
    }
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_resume(void)
{
    taskENTER_CRITICAL();
    if (s_countdown_paused != 0) {
        s_deadline = xTaskGetTickCount() + s_pause_left;
        s_countdown_paused = 0;
    }
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_force_expire(void)
{
    taskENTER_CRITICAL();
    s_force_sleep = 1;          /* 立刻休眠（即使停止倒计时也生效）；进入休眠分支后清除 */
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_clear_force(void)
{
    taskENTER_CRITICAL();
    s_force_sleep = 0;          /* 进入休眠分支后无条件清除 */
    taskEXIT_CRITICAL();
}

uint32_t pm_sleep_timer_left_ms(void)
{
    TickType_t now;
    TickType_t left;

    if (s_force_sleep != 0) {
        return 0;               /* 已被强制到期 */
    }
    if (s_countdown_disabled != 0) {
        return PM_SLEEP_INFINITE;   /* 停止倒计时：剩余视为无限 */
    }
    if (s_countdown_paused != 0) {
        return (uint32_t)(s_pause_left * portTICK_PERIOD_MS);
    }

    now = xTaskGetTickCount();
    if (now >= s_deadline) {
        return 0;
    }

    left = s_deadline - now;
    return (uint32_t)(left * portTICK_PERIOD_MS);
}

uint8_t pm_sleep_timer_expired(void)
{
    if (s_force_sleep != 0) {
        return 1;               /* OR 条件：立刻休眠 */
    }
    if (s_countdown_disabled != 0) {
        return 0;               /* 停止倒计时：永不超时 */
    }
    if (s_countdown_paused != 0) {
        return 0;               /* 常亮要求：冻结倒计时，不允许自动休眠 */
    }
    return (xTaskGetTickCount() >= s_deadline) ? 1 : 0;
}

uint8_t pm_sleep_timer_is_disabled(void)
{
    return (s_countdown_disabled != 0) ? 1 : 0;
}
