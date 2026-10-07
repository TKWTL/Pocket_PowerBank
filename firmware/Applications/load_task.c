/* load_task.c - 慢速外设/系统服务任务（10ms 状态机）
 *
 * 单线程负责 SD3078、SC7A20、NVM 的实际 I2C 提交：
 *  - 10ms：处理 RTC/后备电池 pending（无请求时不访问I2C）；
 *  - 200ms：SC7A20 5Hz 采样 + 上下方向算法；
 *  - 500ms：SD3078 镜像、NVM dirty 合并写、WLED 慢速守护；
 *  - 60s：MS621FE Auto 充电策略复核（SD3078 本身约60s更新VBAT测量）。
 * SW6306 周期采样和业务算法由 SW6306_task 单独负责。
 */
#include "applications.h"

/* SD3078 不再维护 initialized flag；应用层只根据每次 API 的 i2c_status_type
 * 决定是否继续使用/重试初始化。 */
static i2c_status_type s_sd3078_status = I2C_ERR_INTERRUPT;
static uint8_t s_nvm_ready;

static i2c_status_type sd3078_refresh_mirrors(void)
{
    i2c_status_type st;

    st = SD3078_TimeLoad();
    if (st != I2C_OK) return st;
    st = SD3078_TempLoad();
    if (st != I2C_OK) return st;
    return SD3078_BattLoad();
}

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
    }

    /* SD3078_Init 会把充电寄存器恢复为关闭；每次驱动重新初始化后都让
     * algo 重新装载持久化模式并按策略恢复。 */
    SD3078_AlgoInit();
}

/* 一轮完整读取（唤醒预取 / 事件即时刷新）：SD3078 + SC7A20 + SW6306 镜像。
 * 读取顺序：先读 SD3078/SC7A20（唤醒后立即就绪），SW6306 放最后——
 * 其 I2C 操作天然为 SW6306 从 LPSet 唤醒留出就绪时间；唤醒预取时 SW6306 前再
 * 加稳定延迟，避免读到 ADC 未更新的无效值（VBUS/IBAT 等相邻通道 raw 相同）。 */
static void data_refresh_all(void)
{
    sd3078_try_init();
    if (s_sd3078_status == I2C_OK) {
        s_sd3078_status = sd3078_refresh_mirrors();
        if (s_sd3078_status == I2C_OK) {
            s_sd3078_status = SD3078_AlgoProcessFast();
        }
    }
    if (SC7A20_IsInitialized()) {
        SC7A20_AccelLoad();
    }
    if (SW6306_IsInitialized()) {
        /* 唤醒预取：SW6306 刚从 LPSet 唤醒，ADC 需时间就绪；
         * 等待稳定后再读，避免读到未更新的无效 ADC 值。 */
        if (pm_api_wake_data_ready() == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        SW6306_ADCLoad();
        SW6306_StatusLoad();
        SW6306_NTCTempLoad();
        SW6306_PortStatusLoad();
        SW6306_PowerLoad();
        SW6306_CapacityLoad();
    }
}

/* ==================== WLED algorithm executor ====================
 * wled_algo owns brightness/thermal/ramp/fake-A1 state. This task only
 * advances it and performs the requested hardware operations. */
static void wled_service_10ms(void)
{
    uint16_t pwm;
    wled_port_cmd_t cmd;
    uint8_t on;

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

    on = WLED_AlgoIsOn();
    pm_api_set_sleep_block(PM_BLOCK_WLED, on);
    if (on != 0U) pm_api_refresh_idle();
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
            if (SC7A20_IsInitialized()) {
                SC7A20_AccelLoad();
                if (SC7A20_IsInitialized()) {
                    SC7A20_AlgoUpdate(SC7A20_ReadX_mg());
                }
            }
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
                s_sd3078_status = sd3078_refresh_mirrors();

                /* NVM setter 只置 dirty；500ms 合并窗口避免菜单连续步进造成密集 SRAM 写。 */
                if (s_sd3078_status == I2C_OK && s_nvm_ready && nvm_is_dirty()) {
                    s_sd3078_status = nvm_process();
                }

                /* Auto 后备电池策略一分钟复核一次已经足够；SD3078 的 VBAT/TEMP
                 * 硬件自动测量本身也是分钟级。 */
                if (s_sd3078_status == I2C_OK) {
                    if (++sd3078_slow_cnt >= 120U) { /* 120×500ms = 60s */
                        sd3078_slow_cnt = 0U;
                        s_sd3078_status = SD3078_AlgoProcessSlow();
                    }
                }
            }

            WLED_AlgoUpdate500ms();
        }

        wled_service_10ms();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
