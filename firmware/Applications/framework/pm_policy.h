#ifndef PM_POLICY_H
#define PM_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_STATE_RUN = 0,
    PM_STATE_UI_OFF,
    PM_STATE_SLEEP_PREPARE,
    PM_STATE_DEEPSLEEP,
    PM_STATE_SLEEP_PENDING,
    PM_STATE_STANDBY
} pm_power_state_t;

typedef enum {
    PM_SLEEP_DEPTH_UI_OFF = 0,
    PM_SLEEP_DEPTH_DEEPSLEEP,
    PM_SLEEP_DEPTH_STANDBY
} pm_sleep_depth_t;

#ifndef PM_MAX_SLEEP_DEPTH
#define PM_MAX_SLEEP_DEPTH PM_SLEEP_DEPTH_DEEPSLEEP
#endif

typedef struct {
    uint8_t ui_active;
    uint8_t idle_timeout;
    uint8_t usb_or_dc_attached;
    uint8_t low_battery;
    uint8_t force_standby;
    uint8_t wake_event;
    uint8_t post_deepsleep_hold;
} pm_policy_input_t;

pm_power_state_t pm_policy_next_state(pm_power_state_t current, const pm_policy_input_t *in);

#ifdef __cplusplus
}
#endif

#endif
