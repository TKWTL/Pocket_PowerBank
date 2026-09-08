#include "pm_controller.h"

#include "pm_device.h"
#include "pm_sleep_timer.h"

#ifndef PM_PENDING_HOLD_TICKS
#define PM_PENDING_HOLD_TICKS 3
#endif

static void pm_handle_transition(pm_power_state_t from, pm_power_state_t to)
{
    /* RUN → UI_OFF：立即关闭背光等视觉指示 */
    if (from == PM_STATE_RUN && to == PM_STATE_UI_OFF) {
        pm_device_prepare_all();
    }

    /* UI_OFF → SLEEP_PREPARE：进一步准备进入深睡（已无背光） */
    if (from == PM_STATE_UI_OFF && to == PM_STATE_SLEEP_PREPARE) {
        pm_device_prepare_all();
    }

    /* SLEEP_PREPARE → DEEPSLEEP：挂起外设 */
    if (from == PM_STATE_SLEEP_PREPARE && to == PM_STATE_DEEPSLEEP) {
        pm_device_suspend_all();
    }

    /* 任何路径进入 RUN → 恢复外设 */
    if (to == PM_STATE_RUN && from != PM_STATE_RUN) {
        pm_device_resume_all();
    }
}

void pm_controller_init(pm_controller_t *ctx, uint32_t idle_timeout_ms)
{
    if (ctx == 0) {
        return;
    }

    ctx->state = PM_STATE_RUN;
    ctx->ui_active = 1;
    ctx->usb_or_dc_attached = 0;
    ctx->low_battery = 0;
    ctx->force_standby = 0;
    ctx->wake_latched = 0;
    ctx->post_hold_ticks = 0;
    ctx->idle_timer_paused = 0;

    pm_sleep_timer_init(idle_timeout_ms);
}

void pm_controller_mark_ui_active(pm_controller_t *ctx, uint8_t active)
{
    if (ctx == 0) {
        return;
    }

    ctx->ui_active = (active != 0) ? 1 : 0;
    if (ctx->ui_active != 0) {
        pm_controller_refresh_idle(ctx);
    }
}

void pm_controller_set_usb_attached(pm_controller_t *ctx, uint8_t attached)
{
    if (ctx == 0) {
        return;
    }

    ctx->usb_or_dc_attached = (attached != 0) ? 1 : 0;
    if (ctx->usb_or_dc_attached != 0) {
        pm_controller_refresh_idle(ctx);
    }
}

void pm_controller_set_low_battery(pm_controller_t *ctx, uint8_t low_battery)
{
    if (ctx == 0) {
        return;
    }

    ctx->low_battery = (low_battery != 0) ? 1 : 0;
}

void pm_controller_force_standby(pm_controller_t *ctx, uint8_t force_standby)
{
    if (ctx == 0) {
        return;
    }

    ctx->force_standby = (force_standby != 0) ? 1 : 0;
}

void pm_controller_refresh_idle(pm_controller_t *ctx)
{
    if (ctx == 0) {
        return;
    }

    /* 有外部活动 → UI 应重新激活 */
    ctx->ui_active = 1;
    ctx->wake_latched = 1;
    if (ctx->idle_timer_paused == 0) {
        pm_sleep_timer_refresh();
    }
}

void pm_controller_pause_idle(pm_controller_t *ctx)
{
    if (ctx == 0) {
        return;
    }

    ctx->idle_timer_paused = 1;
}

void pm_controller_resume_idle(pm_controller_t *ctx)
{
    if (ctx == 0) {
        return;
    }

    ctx->idle_timer_paused = 0;
    /* 活动（充电/放电阻塞）结束后刷新空闲计时：
     * 否则阻塞期间 deadline 已过，解除阻塞会立即识别超时入睡，
     * 导致充电/放电一停就睡、LPSet 切断输出、唤醒后功率跌落。
     * 注意：本函数仅在 block 解除的转换瞬间被调用一次，不会阻止正常休眠。 */
    pm_sleep_timer_refresh();
}

void pm_controller_notify_wake(pm_controller_t *ctx)
{
    if (ctx == 0) {
        return;
    }

    ctx->wake_latched = 1;
}

uint8_t pm_controller_is_ui_blocked(pm_controller_t *ctx)
{
    if (ctx == 0) {
        return 0;
    }

    /* 非 RUN 状态时 UI 任务应主动让步 */
    return (ctx->state != PM_STATE_RUN) ? 1 : 0;
}

pm_power_state_t pm_controller_step(pm_controller_t *ctx)
{
    pm_policy_input_t in;
    pm_power_state_t next;

    if (ctx == 0) {
        return PM_STATE_RUN;
    }

    in.ui_active = ctx->ui_active;
    in.idle_timeout = (ctx->idle_timer_paused != 0) ? 0 : pm_sleep_timer_expired();
    in.usb_or_dc_attached = ctx->usb_or_dc_attached;
    in.low_battery = ctx->low_battery;
    in.force_standby = ctx->force_standby;
    in.wake_event = ctx->wake_latched;
    in.post_deepsleep_hold = (ctx->post_hold_ticks != 0) ? 1 : 0;

    next = pm_policy_next_state(ctx->state, &in);
    pm_handle_transition(ctx->state, next);

    if (ctx->state == PM_STATE_DEEPSLEEP && next == PM_STATE_SLEEP_PENDING) {
        ctx->post_hold_ticks = PM_PENDING_HOLD_TICKS;
    } else if (ctx->post_hold_ticks > 0) {
        ctx->post_hold_ticks--;
    }

    ctx->state = next;
    ctx->wake_latched = 0;
    return ctx->state;
}
