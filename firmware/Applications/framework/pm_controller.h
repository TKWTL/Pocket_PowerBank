#ifndef PM_CONTROLLER_H
#define PM_CONTROLLER_H

#include <stdint.h>

#include "pm_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    pm_power_state_t state;
    uint8_t ui_active;
    uint8_t usb_or_dc_attached;
    uint8_t low_battery;
    uint8_t force_standby;
    uint8_t wake_latched;
    uint8_t post_hold_ticks;
    uint8_t idle_timer_paused;
} pm_controller_t;

void pm_controller_init(pm_controller_t *ctx, uint32_t idle_timeout_ms);
void pm_controller_mark_ui_active(pm_controller_t *ctx, uint8_t active);
void pm_controller_set_usb_attached(pm_controller_t *ctx, uint8_t attached);
void pm_controller_set_low_battery(pm_controller_t *ctx, uint8_t low_battery);
void pm_controller_force_standby(pm_controller_t *ctx, uint8_t force_standby);
void pm_controller_refresh_idle(pm_controller_t *ctx);
void pm_controller_pause_idle(pm_controller_t *ctx);
void pm_controller_resume_idle(pm_controller_t *ctx);
void pm_controller_notify_wake(pm_controller_t *ctx);
pm_power_state_t pm_controller_step(pm_controller_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
