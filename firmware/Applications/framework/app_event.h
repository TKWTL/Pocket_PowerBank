#ifndef APP_EVENT_H
#define APP_EVENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_EVT_NONE = 0,
    APP_EVT_INPUT_ACTIVITY,
    APP_EVT_WAKE,
    APP_EVT_PORT_STATE_CHANGED,
    APP_EVT_BATTERY_ALERT,
    APP_EVT_RTC_TICK
} app_event_id_t;

typedef struct {
    app_event_id_t id;
    uint32_t arg0;
    uint32_t arg1;
} app_event_t;

#ifdef __cplusplus
}
#endif

#endif
