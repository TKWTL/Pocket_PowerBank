#include "pm_sleep_timer.h"

#include "FreeRTOS.h"
#include "task.h"

static TickType_t s_deadline;
static uint32_t s_timeout_ms;

static TickType_t pm_to_ticks(uint32_t ms)
{
    return pdMS_TO_TICKS(ms);
}

void pm_sleep_timer_init(uint32_t timeout_ms)
{
    s_timeout_ms = timeout_ms;
    s_deadline = xTaskGetTickCount() + pm_to_ticks(timeout_ms);
}

void pm_sleep_timer_set(uint32_t timeout_ms)
{
    taskENTER_CRITICAL();
    s_timeout_ms = timeout_ms;
    s_deadline = xTaskGetTickCount() + pm_to_ticks(timeout_ms);
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_refresh(void)
{
    taskENTER_CRITICAL();
    s_deadline = xTaskGetTickCount() + pm_to_ticks(s_timeout_ms);
    taskEXIT_CRITICAL();
}

void pm_sleep_timer_force_expire(void)
{
    taskENTER_CRITICAL();
    s_deadline = xTaskGetTickCount();
    taskEXIT_CRITICAL();
}

uint32_t pm_sleep_timer_left_ms(void)
{
    TickType_t now;
    TickType_t left;

    now = xTaskGetTickCount();
    if (now >= s_deadline) {
        return 0;
    }

    left = s_deadline - now;
    return (uint32_t)(left * portTICK_PERIOD_MS);
}

uint8_t pm_sleep_timer_expired(void)
{
    TickType_t now = xTaskGetTickCount();
    return (now >= s_deadline) ? 1 : 0;
}
