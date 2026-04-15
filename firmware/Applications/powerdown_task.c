#include "at32f423_wk_config.h"

#include "applications.h"


void powerdown_task_func(void *pvParameters)
{    
    while(1){
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}