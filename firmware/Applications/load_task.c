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

/* ==================== WLED 守护 + 电量统计（假 A1 插入） ====================
 * 守护：正常工况下由 WLED_Tick（10ms）按 NTC 温度限档先压低功率；
 *       WLED_Update（500ms）是兜底——过温（NTC 60°C / 芯片 100°C）、零电量时
 *       锁存强制关灯（需降温/恢复后手动再开）。
 * 电量统计：WLED 开启且无任何真实口打开时，假插入 A1 口启动 SW6306 DCDC，
 *       使 WLED 耗电经 SW6306 库仑计计入电量（否则 WLED 电流不计入，虚增 SOC）。
 * 时序（必须严格）：开灯时【先插 A1、再放行 PWM 渐变】；关灯时【先停 PWM、再拔 A1】。
 *       否则灯先亮而库仑计尚未通路（这段电流漏计），或灯未灭就断电（灯闪一下）。
 *       本函数只有 500ms 节拍，所以两个"顺序点"都用 WLED_SetPowerPath() 的闸门做互锁，
 *       而不是靠在本函数里调顺序——调用顺序其实已经是对的（先插后补位/先让位后拔）。
 * 让位/补位：真实口（C1）插入或充电时拔出假 A1（让位）；拔出后补插。
 * 休眠：WLED 开启期间阻止系统休眠（PM_BLOCK_WLED）。 */
#define WLED_SOC_IBUS_NOLOAD   50   /* 假 A1 口空载判定电流（mA） */
#define WLED_SOC_DEATTACH_CNT  2    /* 假 A1 让位延迟（2×500ms=1s，wled_soc_manage 每 500ms 调用一次） */
#define WLED_TICK_MS           10   /* WLED 渐变节拍（与 load_task 的 vTaskDelay 一致） */
#define WLED_SOC_RAMP_MS       200  /* 等 PWM 渐变走完的余量：渐变 160ms + 裕量 */
#define WLED_SOC_RAMP_TICKS    ((WLED_SOC_RAMP_MS) / (WLED_TICK_MS))

/* 500ms 周期调用：管理假 A1 口的插入/让位/补位（I2C 操作，SW6306 互斥保护）。
 * 用 A1 口而非 C2：C 口端口事件仅在 source 状态生效（sw6306.h:456），
 * C2 非 source → 假插入 C2 事件被 SW6306 忽略（实测 c2_on 恒 0 且 VBUS 异常跌低）；
 * A 口无此限制（3S1P 用 A1 假插入验证过），故假插入改走 A1 口启动 DCDC 计入电量。 */
static void wled_soc_manage(void)
{
    static uint16_t deattach_delay;
    static uint8_t  pwm_was_on;
    static uint8_t  post_off_cnt;
    uint8_t wled_on  = (WLED_GetBrightness() != 0);
    uint8_t a1_fake  = SW6306_IsPortA1ON();                        /* 假 A1（插入事件启动的 DCDC 通路） */
    uint8_t real_any = SW6306_IsPortC1ON();                        /* 真实口（A1 为假插入口，不计入） */
    uint8_t charging = SW6306_IsCharging();
    uint8_t keep_a1  = 0;                                          /* 1=本周期必须保留 A1（灯准备亮/正在亮/正在灭） */

    /* WLED 开启期间阻止系统休眠（灯亮需持续供电，避免深睡掉电） */
    pm_api_set_sleep_block(PM_BLOCK_WLED, wled_on);
    if (wled_on) {
        pm_api_refresh_idle();
    }

    /* ---- 开灯侧：A1 未就绪前冻结 PWM 渐变（先插 A1，再让灯亮） ---- */
    if (wled_on && !a1_fake) {
        WLED_SetPowerPath(0);      /* 关闸：WLED 目标已非 0，但 PWM 停在当前值 */
        if (!real_any && !charging) {
            SW6306_PortA1Insert();  /* 先插 A1（启动 DCDC）→ 库仑计通路就绪 */
            a1_fake = 1;           /* 本周期内视为已就绪 */
        }
        keep_a1 = 1;               /* A1 尚未插上（真实口占用/充电中）时不要被让位逻辑拔掉 */
    }
    if (a1_fake && wled_on) {
        /* A1 已在位：等灯真正亮起（PWM>0）并留够渐变时间，再放行 */
        if (WLED_GetPwm() != 0) {
            pwm_was_on = 1;
        }
        WLED_SetPowerPath(1);
    }

    /* ---- 关灯侧：先让 PWM 归 0（灯完全灭），再拔 A1 ---- */
    if (!wled_on) {
        WLED_SetPowerPath(1);      /* 闸门不拦关灯，但显式放行以免影响下次点亮 */
        if (pwm_was_on && WLED_GetPwm() != 0) {
            keep_a1 = 1;                            /* 渐变尚未走完，别拔 A1 */
            if (post_off_cnt < WLED_SOC_RAMP_TICKS) post_off_cnt++;
        } else if (pwm_was_on) {
            keep_a1 = 1;                            /* PWM 已归 0，再留 2 个 500ms 让电流彻底落下去 */
            if (post_off_cnt < WLED_SOC_RAMP_TICKS) {
                post_off_cnt++;
            } else {
                pwm_was_on   = 0;
                post_off_cnt = 0;
                /* 此刻才允许拔 A1（下面让位逻辑里执行） */
            }
        }
    } else {
        post_off_cnt = 0;
    }

    /* 让位：真实口（C1）打开 / 充电中 / WLED 关闭且假 A1 空载 → 拔出假 A1 */
    if (a1_fake && !keep_a1 && (real_any || charging ||
                    (!wled_on && SW6306_ReadIBUS() < WLED_SOC_IBUS_NOLOAD))) {
        if (deattach_delay < WLED_SOC_DEATTACH_CNT) deattach_delay++;
    } else {
        deattach_delay = 0;
    }
    if (a1_fake && deattach_delay >= WLED_SOC_DEATTACH_CNT) {
        SW6306_PortA1Remove();
        deattach_delay = 0;
    }

    /* 补位：WLED 开、无真实口、未充电 → 插入假 A1 启动 DCDC 计入电量 */
    if (wled_on && !a1_fake && !real_any && !charging) {
        SW6306_PortA1Insert();
    }
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

            wled_soc_manage();
            WLED_Update();
        }

        WLED_Tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
