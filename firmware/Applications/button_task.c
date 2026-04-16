#include "applications.h"

#include "framework/pm_api.h"

static uint8_t key_has_activity(void)
{
    uint8_t i;

    for (i = 0; i < KeyIndex_Max; i++) {
        if (KEY_GetState((KeyIndex_t)i) != KeyState_None) {
            return 1;
        }
    }
    return 0;
}

void button_task_func(void *pvParameters)
{
    (void)pvParameters;
    uint8_t i = 0;
    uint16_t btn = 256;

    Key_Init();
    for (;;) {
        Key_DebounceService_10ms();
        Key_Scand();

        if (key_has_activity() != 0) {
            pm_api_refresh_idle();
        }

        if (KEY_GetDASClick(KeyIndex_NEXT) && btn < 1024) {
            btn += 8;
        }
        if (KEY_GetDASClick(KeyIndex_MENU) && btn > 0) {
            btn -= 8;
        }
        if (KEY_GetDASClick(KeyIndex_CONF)) {
            i = i ? 0 : 1;
        }
        if (i) {
            tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, btn);
        } else {
            tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
