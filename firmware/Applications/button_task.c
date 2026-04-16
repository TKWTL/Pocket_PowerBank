#include "applications.h"

void button_task_func(void *pvParameters)
{
    (void)pvParameters;
    uint8_t i = 0;
    uint16_t btn = 256;

    Key_Init();
    for (;;) {
        Key_DebounceService_10ms();
        Key_Scand();
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
