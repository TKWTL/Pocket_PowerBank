/* load_task.c - 慢速外设/系统服务任务（10ms 状态机）
 *
 * 单线程负责 SD3078、SC7A20、NVM 的实际 I2C 提交：
 *  - 10ms：处理 RTC/后备电池 pending（无请求时不访问I2C）；
 *  - 200ms：SC7A20 5Hz 采样 + 上下方向算法；
 *  - 500ms：SD3078 Time Load、NVM dirty 合并写、WLED 慢速守护；
 *  - 60s：SD3078 Temp/VBAT Load + MS621FE Auto充电策略复核。
 * 各外设具体Load顺序/分频放在对应algo；task只提供调度节拍。
 */
#include "applications.h"

/* 延迟复位（实现在 functions/reset.c，声明在 functions/functions.h）。
 * 这里只做前置声明，不包含 functions.h：后者为 UI 头（lvgl.h + menu.h），
 * 而本文件属于 Tasks 组、不应耦合 UI 层。
 * 调用语义：负载任务每 500ms 调用一次；函数内部先判 pending 与 NVM 干净，
 * 仅当 UI 侧 action_reset_now() 置过 pending 时才真正 nvic_system_reset()，
 * 因此周期调用是幂等且安全的。 */
void action_reset_process(void);

/* SD3078 不再维护 initialized flag；应用层只根据每次 API 的 i2c_status_type
 * 决定是否继续使用/重试初始化。 */
static i2c_status_type s_sd3078_status = I2C_ERR_INTERRUPT;
static uint8_t s_nvm_ready;

static void sd3078_try_init(void)
{
    i2c_status_type st;

    if (s_sd3078_status == I2C_OK) return;

    s_sd3078_status = SD3078_Init();
    if (s_sd3078_status != I2C_OK) return;

    /* NVM 依赖 SD3078 Backup RAM，因此只在 RTC 驱动已可靠在线后初始化。 */
    if (!s_nvm_ready) {
        st = nvm_init();
        if (st != I2C_OK) {
            s_sd3078_status = st;
            return;
        }
        s_nvm_ready = 1U;

        /* 持久化校准值在业务算法开始前装入各自驱动。 */
        SW6306_SetBattRShunt(nvm_get_shunt_mohm());
        SC7A20_SetXCalibration(nvm_get_sc7a20_x_zero_raw(),
                               nvm_get_sc7a20_x_slope_mg_per_lsb());
        SC7A20_SetYCalibration(nvm_get_sc7a20_y_zero_raw(),
                               nvm_get_sc7a20_y_slope_mg_per_lsb());
        SC7A20_SetZCalibration(nvm_get_sc7a20_z_zero_raw(),
                               nvm_get_sc7a20_z_slope_mg_per_lsb());
    }

    /* SD3078_Init会把充电寄存器恢复为关闭；algo重新装载持久化模式。
     * 初始化后立即走一次全量Load，保证Auto充电第一次判定就使用fresh Temp/VBAT。 */
    SD3078_AlgoInit();
    s_sd3078_status = SD3078_AlgoLoadAll();
}

/* 一轮完整读取（唤醒预取 / 事件即时刷新）：SD3078 + SC7A20 + SW6306 镜像。
 * 读取顺序：先读 SD3078/SC7A20（唤醒后立即就绪），SW6306 放最后——
 * 其 I2C 操作天然为 SW6306 从 LPSet 唤醒留出就绪时间；唤醒预取时 SW6306 前再
 * 加稳定延迟，避免读到 ADC 未更新的无效值（VBUS/IBAT 等相邻通道 raw 相同）。 */
static void data_refresh_all(void)
{
    sd3078_try_init();
    if (s_sd3078_status == I2C_OK) {
        s_sd3078_status = SD3078_AlgoLoadAll();
        if (s_sd3078_status == I2C_OK) {
            s_sd3078_status = SD3078_AlgoProcessFast();
        }
    }

    SC7A20_AlgoLoadSample();

    if (SW6306_IsInitialized()) {
        /* SW6306刚从LPSet唤醒时ADC需要稳定时间；任务只负责时序，
         * 真正的寄存器Load顺序由SW6306 algo持有。 */
        if (pm_api_wake_data_ready() == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        SW6306_AlgoLoadAll();
    }
}

/* ==================== WLED algorithm executor ====================
 * wled_algo owns brightness/thermal/ramp/fake-A1 state. This task only
 * advances it and performs the requested hardware operations. */
static void wled_service_10ms(void)
{
    uint16_t pwm;
    uint16_t pwm_out;
    wled_port_cmd_t cmd;

    WLED_AlgoTick10ms();

    cmd = WLED_AlgoTakePortCommand();
    if (cmd == WLED_PORT_CMD_A1_INSERT) {
        SW6306_PortA1Insert();
    } else if (cmd == WLED_PORT_CMD_A1_REMOVE) {
        SW6306_PortA1Remove();
    }

    if (WLED_AlgoTakePwm(&pwm) != 0U) {
        WLED_SetPwm(pwm);
    }

    /* 休眠门控绑定【实际 PWM 输出】，而不是 s_level（用户意图）。
     * 原因：s_level 在收到开关请求时立刻置位，但 PWM 还要过计量通路门控
     * （s_path_ok）和 160ms 渐变才会真正输出。若假插入口没确认（s_path_ok=0），
     * s_level 已是 12 而 s_pwm 恒为 0 —— 此时若按 s_level 阻止休眠，
     * 就会出现"灯没亮、却一直拒绝休眠、30s 倒计时被打断"的假激活状态。 */
    pwm_out = WLED_AlgoGetPwm();
    pm_api_set_sleep_block(PM_BLOCK_WLED, (pwm_out != 0U) ? 1U : 0U);
    if (pwm_out != 0U) pm_api_refresh_idle();
}

/* load_task 任务入口：10ms 状态机（周期 500ms 刷新 + 请求即立即一轮完整读取）
 * 注：曾尝试"INT 刷新提醒驱动"（SD3078 在共用 INT 线上输出 2Hz，由 EXTI 通知本任务），
 * 因长期占用共用 INT 线已回滚，恢复为周期轮询 + 事件驱动。 */
void load_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t period_cnt = 0U;
    uint8_t sc7_cnt = 0U;
    uint8_t sd3078_slow_cnt = 0U;

    WLED_Init();
    WLED_AlgoInit();
    SC7A20_AlgoInit();

    for (;;) {
        /* 睡眠准备/DeepSleep期间停止发起新的总线事务。 */
        if (pm_api_sleep_gate_get() != 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* SD3078 快速服务：没有 pending 时只检查 RAM flag，不访问 I2C。 */
        if (s_sd3078_status == I2C_OK) {
            i2c_status_type st = SD3078_AlgoProcessFast();
            if (st != I2C_OK) s_sd3078_status = st;
        }

        /* 5Hz SC7A20：只有本周期路径推进姿态计时，确保连续约2s才翻转。 */
        if (++sc7_cnt >= (SC7A20_ALGO_SAMPLE_MS / 10U)) {
            sc7_cnt = 0U;
            SC7A20_AlgoLoadSample();
        }

        if (pm_api_data_refresh_pending() != 0) {
            data_refresh_all();
            pm_api_data_refresh_done();
        } else if (++period_cnt >= 50U) {  /* 50×10ms = 500ms */
            period_cnt = 0U;

            sd3078_try_init();

            if (!SC7A20_IsInitialized()) {
                SC7A20_Init();       /* 传感器10Hz，应用层按5Hz读取 */
                SC7A20_AlgoInit();
                exint_flag_clear(EXINT_LINE_8);
                NVIC_ClearPendingIRQ(EXINT9_5_IRQn);
            }

            if (s_sd3078_status == I2C_OK) {
                /* Time保持500ms；Temp/VBAT由Slow入口降到60s。 */
                s_sd3078_status = SD3078_AlgoLoadFast();

                /* NVM setter只置dirty；500ms合并窗口避免菜单连续步进造成密集SRAM写。 */
                if (s_sd3078_status == I2C_OK && s_nvm_ready && nvm_is_dirty()) {
                    s_sd3078_status = nvm_process();
                }
                if (s_sd3078_status == I2C_OK && s_nvm_ready) {
                    action_reset_process();
                }

                if (s_sd3078_status == I2C_OK && ++sd3078_slow_cnt >= 120U) {
                    sd3078_slow_cnt = 0U;
                    s_sd3078_status = SD3078_AlgoLoadSlow();
                }
            }

            WLED_AlgoUpdate500ms();
        }

        wled_service_10ms();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
