#include "at32f423_wk_config.h"

#include "applications.h"

#include "framework/pm_controller.h"
#include "framework/pm_device.h"

static pm_controller_t s_pm_ctrl;
static uint8_t s_pm_ready;

static void pm_task_init_once(void)
{
    if (s_pm_ready != 0) {
        return;
    }

    pm_controller_init(&s_pm_ctrl, 30000);
    pm_controller_mark_ui_active(&s_pm_ctrl, 1);
    pm_device_register_all();

    s_pm_ready = 1;
}

void powerdown_task_func(void *pvParameters)
{
    pm_power_state_t state;

    (void)pvParameters;
    pm_task_init_once();

    while (1) {
        state = pm_controller_step(&s_pm_ctrl);
        switch (state) {
        case PM_STATE_RUN:
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        case PM_STATE_UI_OFF:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        case PM_STATE_SLEEP_PREPARE:
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        case PM_STATE_DEEPSLEEP:
            __WFI();
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        case PM_STATE_SLEEP_PENDING:
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        case PM_STATE_STANDBY:
            __WFI();
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }
    }
}
