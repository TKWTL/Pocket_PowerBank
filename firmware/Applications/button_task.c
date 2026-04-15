#include "applications.h"

void button_task_func(void *pvParameters)
{
    uint8_t i = 0;
    uint16_t btn = 256;
    Key_Init();
    for(;;)
    {
        Key_DebounceService_10ms();
        Key_Scand();/*
        if(Unstable_of_Wake == 0){//判断是否刚刚唤醒，是则不响应按键，避免误操作
            for(i = 0; i < KeyIndex_Max; i++){
                if(KEY_GetDASClick(i)){
                    btn = (i << 8)| 0x01;
                    osMessageQueuePut(Button_QueueHandle, &btn, 0, osWaitForever);
                    UserOperationDetected();
                }
            }
        }*/
        if(KEY_GetDASClick(KeyIndex_NEXT) && btn < 1024) btn += 8;
        if(KEY_GetDASClick(KeyIndex_MENU) && btn > 0) btn -= 8;
        if(KEY_GetDASClick(KeyIndex_CONF)) i = i ? 0 : 1;
        if(i) tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, btn);
        else tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
