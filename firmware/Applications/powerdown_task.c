#include "at32f423_wk_config.h"

#include "applications.h"

#include "framework/pm_api.h"
#include "framework/pm_controller.h"
#include "framework/pm_device.h"

static pm_controller_t s_pm_ctrl;
static uint8_t s_pm_ready;
static volatile uint8_t s_pm_refresh_req;
static volatile uint8_t s_pm_block_mask;

void pm_api_refresh_idle(void)
{
    s_pm_refresh_req = 1;
}

void pm_api_set_sleep_block(uint8_t mask, uint8_t enable)
{
    if (enable != 0) {
        s_pm_block_mask |= mask;
    } else {
        s_pm_block_mask &= (uint8_t)(~mask);
    }
}

static void pm_enter_deep_sleep(void)
{
    pwc_voltage_regulate_set(PWC_REGULATOR_LOW_POWER);
    pwc_deep_sleep_mode_enter(PWC_DEEP_SLEEP_ENTER_WFI);
}

static void pm_enter_standby(void)
{
    pwc_wakeup_pin_enable(PWC_WAKEUP_PIN_1, TRUE);
    pwc_flag_clear(PWC_WAKEUP_FLAG | PWC_STANDBY_FLAG);
    pwc_standby_mode_enter();
}

static void pm_task_init_once(void)
{
    if (s_pm_ready != 0) {
        return;
    }

    pm_controller_init(&s_pm_ctrl, 10000);
    pm_controller_mark_ui_active(&s_pm_ctrl, 1);
    pm_device_register_all();

    s_pm_ready = 1;
}

void powerdown_task_func(void *pvParameters)
{
    pm_power_state_t state;
    uint8_t sleep_blocked = 0;

    (void)pvParameters;
    pm_task_init_once();

    while (1) {
        if (s_pm_refresh_req != 0) {
            pm_controller_refresh_idle(&s_pm_ctrl);
            s_pm_refresh_req = 0;
        }

        if (s_pm_block_mask != 0) {
            if (sleep_blocked == 0) {
                pm_controller_pause_idle(&s_pm_ctrl);
                sleep_blocked = 1;
            }
            pm_controller_notify_wake(&s_pm_ctrl);
        } else if (sleep_blocked != 0) {
            pm_controller_resume_idle(&s_pm_ctrl);
            sleep_blocked = 0;
        }

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
            pm_enter_deep_sleep();
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        case PM_STATE_SLEEP_PENDING:
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        case PM_STATE_STANDBY:
            pm_enter_standby();
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }
    }
}
