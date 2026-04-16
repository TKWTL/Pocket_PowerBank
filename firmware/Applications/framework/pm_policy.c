#include "pm_policy.h"

static uint8_t pm_allow_depth(pm_sleep_depth_t depth)
{
    return (depth <= (pm_sleep_depth_t)PM_MAX_SLEEP_DEPTH) ? 1 : 0;
}

pm_power_state_t pm_policy_next_state(pm_power_state_t current, const pm_policy_input_t *in)
{
    if (in == 0) {
        return PM_STATE_RUN;
    }

    if (in->wake_event != 0) {
        return PM_STATE_RUN;
    }

    if (in->force_standby != 0 && pm_allow_depth(PM_SLEEP_DEPTH_STANDBY) != 0) {
        return PM_STATE_STANDBY;
    }

    if (in->low_battery != 0 && in->usb_or_dc_attached == 0 && pm_allow_depth(PM_SLEEP_DEPTH_STANDBY) != 0) {
        return PM_STATE_STANDBY;
    }

    if (current == PM_STATE_DEEPSLEEP) {
        return PM_STATE_SLEEP_PENDING;
    }

    if (current == PM_STATE_SLEEP_PENDING) {
        if (in->post_deepsleep_hold != 0) {
            return PM_STATE_SLEEP_PENDING;
        }
        return PM_STATE_RUN;
    }

    if (in->ui_active != 0 || in->idle_timeout == 0) {
        return PM_STATE_RUN;
    }

    if (pm_allow_depth(PM_SLEEP_DEPTH_UI_OFF) == 0) {
        return PM_STATE_RUN;
    }

    if (current == PM_STATE_RUN) {
        return PM_STATE_UI_OFF;
    }

    if (current == PM_STATE_UI_OFF) {
        if (pm_allow_depth(PM_SLEEP_DEPTH_DEEPSLEEP) != 0) {
            return PM_STATE_SLEEP_PREPARE;
        }
        return PM_STATE_UI_OFF;
    }

    if (current == PM_STATE_SLEEP_PREPARE) {
        if (pm_allow_depth(PM_SLEEP_DEPTH_DEEPSLEEP) != 0) {
            return PM_STATE_DEEPSLEEP;
        }
        return PM_STATE_UI_OFF;
    }

    return current;
}
