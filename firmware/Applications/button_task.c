#include "applications.h"

/* ========== 按键活动检测（修复 Release 卡住休眠计时器的问题） ==========
 *
 * 原问题：按键按下→释放后 Key_Scand() 将状态设为 KeyState_Release，
 * 但由于 IO 不再变化，Release 状态永远不会回到 None。
 * key_has_activity() 认为 State != None 即为活跃 → 不断调用
 * pm_api_refresh_idle() → 休眠定时器被持续刷新 → 倒计时卡在 99xx ms。
 *
 * 修复：KeyState_Release 是「已释放」的瞬态，不代表当前有按键活动，
 * 将其排除在活跃判定之外。
 */
static uint8_t key_has_activity(void)
{
    uint8_t i;

    for (i = 0; i < KeyIndex_Max; i++) {
        /* KeyState_Release 是按键弹起后的残留状态，不是持续按键活动，
           不应阻止系统休眠。排除它。 */
        if (KEY_GetState((KeyIndex_t)i) != KeyState_None &&
            KEY_GetState((KeyIndex_t)i) != KeyState_Release) {
            return 1;
        }
        /* 有边沿事件（按下、弹起、长按到达）也算活动 */
        if (Key_EdgeDetect((KeyIndex_t)i) != KeyEdge_Null) {
            return 1;
        }
    }
    return 0;
}

void button_task_func(void *pvParameters)
{
    (void)pvParameters;
    Key_Init();
    for (;;) {
        Key_DebounceService_10ms();
        Key_Scand();

        /* 唤醒静默期：刚唤醒时不响应按键，避免误操作 */
        if (pm_api_is_unstable_wake()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (key_has_activity() != 0) {
            pm_api_refresh_idle();
        }

        /* WLED 守护已移至 load_task（500ms：过温/零电量强制关灯 + 假 A1 电量统计 + 阻止休眠）。
         * WLED 开关按键在 main_screen_run（ui_task 的 ui_loop 调度分发）。 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
