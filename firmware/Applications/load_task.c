/* load_task.c - 传感器/时间/电源数据读取任务（10ms 状态机）
 *
 * 本任务集中所有 I2C 访问（SD3078/SC7A20），并承担唤醒/事件数据预取：
 *  - 10ms tick 轮询：累计 50 tick（500ms）执行一轮周期刷新（SD3078/SC7A20 镜像）；
 *  - 检测数据刷新请求（EXINT 中断置单一 flag）：立即执行一轮完整读取
 *    （SW6306 全套 + SD3078 + SC7A20），完成后 pm_api_data_refresh_done()
 *    置位唤醒使能（数据就绪）→ ui_task 解除阻塞，先绘后亮。
 *  - SW6306 周期读取仍在 SW6306_task；两任务并发读 SW6306 由 I2C 互斥锁保护。
 * UI 只读驱动句柄镜像（Read* 系列，不访问 I2C），避免并发访问外设。
 */
#include "applications.h"

/* 一轮完整读取（唤醒预取 / 事件即时刷新）：SD3078 + SC7A20 + SW6306 镜像。
 * 读取顺序：先读 SD3078/SC7A20（唤醒后立即就绪），SW6306 放最后——
 * 其 I2C 操作天然为 SW6306 从 LPSet 唤醒留出就绪时间；唤醒预取时 SW6306 前再
 * 加稳定延迟，避免读到 ADC 未更新的无效值（VBUS/IBAT 等相邻通道 raw 相同）。 */
static void data_refresh_all(void)
{
    if (SD3078_IsInitialized()) {
        SD3078_TimeLoad();       /* 时间镜像（硬件锁存防错读，十进制同步） */
        SD3078_TempLoad();       /* 温度镜像 */
        SD3078_BattLoad();       /* 备用电池电压镜像 */
        SD3078_TimeSetProcess(); /* 检测并提交 UI 的时间设置请求 */
    }
    if (SC7A20_IsInitialized()) {
        SC7A20_AccelLoad();      /* 三轴加速度原始数据镜像（Read*_mg 读它换算） */
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
 * 守护：过温（NTC 60°C / 芯片 100°C）、零电量时由 WLED_Update 强制关灯
 *       （保持目标亮度，解除后允许手动再开）。
 * 电量统计：WLED 开启且无任何真实口打开时，假插入 A1 口启动 SW6306 DCDC，
 *       使 WLED 耗电经 SW6306 库仑计计入电量（否则 WLED 电流不计入，虚增 SOC）。
 * 让位/补位：真实口（C1）插入或充电时拔出假 A1（让位）；拔出后补插。
 * 休眠：WLED 开启期间阻止系统休眠（PM_BLOCK_WLED）。 */
#define WLED_SOC_IBUS_NOLOAD   50   /* 假 A1 口空载判定电流（mA） */
#define WLED_SOC_DEATTACH_CNT  2    /* 假 A1 让位延迟（2×500ms=1s，wled_soc_manage 每 500ms 调用一次） */

/* 500ms 周期调用：管理假 A1 口的插入/让位/补位（I2C 操作，SW6306 互斥保护）。
 * 用 A1 口而非 C2：C 口端口事件仅在 source 状态生效（sw6306.h:456），
 * C2 非 source → 假插入 C2 事件被 SW6306 忽略（实测 c2_on 恒 0 且 VBUS 异常跌低）；
 * A 口无此限制（3S1P 用 A1 假插入验证过），故假插入改走 A1 口启动 DCDC 计入电量。 */
static void wled_soc_manage(void)
{
    static uint16_t deattach_delay;
    uint8_t wled_on  = (WLED_GetBrightness() != 0);
    uint8_t a1_fake  = SW6306_IsPortA1ON();                        /* 假 A1（插入事件启动的 DCDC 通路） */
    uint8_t real_any = SW6306_IsPortC1ON();                        /* 真实口（A1 为假插入口，不计入） */
    uint8_t charging = SW6306_IsCharging();

    /* 诊断：确认本函数执行 + 各条件值（WLED=1 且其余为 0 时应触发 A1 插入） */
    USART_Printf("[SOC] wled=%u a1=%u real=%u chg=%u\n", wled_on, a1_fake, real_any, charging);

    /* WLED 开启期间阻止系统休眠（灯亮需持续供电，避免深睡掉电） */
    pm_api_set_sleep_block(PM_BLOCK_WLED, wled_on);
    if (wled_on) {
        pm_api_refresh_idle();
    }

    /* 让位：真实口（C1）打开 / 充电中 / WLED 关闭且假 A1 空载 → 拔出假 A1 */
    if (a1_fake && (real_any || charging ||
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
        USART_Printf("[SOC] A1 insert\n");
    }
}

/* load_task 任务入口：10ms 状态机（周期 500ms 刷新 + 请求即立即一轮完整读取） */
void load_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t period_cnt = 0;

    for (;;) {
        /* 睡眠门控：睡眠准备/深睡期间不发起总线读写（已发起的由 powerdown 等待完成） */
        if (pm_api_sleep_gate_get() != 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (pm_api_data_refresh_pending() != 0) {
            /* 数据刷新请求（唤醒预取 / RUN 态事件）：立即一轮完整读取 */
            data_refresh_all();
            pm_api_data_refresh_done();   /* 清请求 + 置位唤醒使能（数据就绪） */
        } else if (++period_cnt >= 50) {  /* 50×10ms = 500ms 周期轮询 */
            period_cnt = 0;
            if (!SD3078_IsInitialized()) {
                SD3078_Init();       /* 正式初始化：默认不充电 + 低功耗（内部只执行一次） */
            }
            if (!SC7A20_IsInitialized()) {
                SC7A20_Init();       /* 正式初始化：H_LACTIVE=1 INT极性修正 + ODR 1Hz */
                /* SC7A20 上电/改极性可能产生沿，清 EXINT8 pending 防伪中断 */
                exint_flag_clear(EXINT_LINE_8);
                NVIC_ClearPendingIRQ(EXINT9_5_IRQn);
            }
            if (SD3078_IsInitialized()) {
                SD3078_TimeLoad();       /* 时间镜像（硬件锁存防错读，十进制同步） */
                SD3078_TempLoad();       /* 温度镜像 */
                SD3078_BattLoad();       /* 备用电池电压镜像 */
                SD3078_TimeSetProcess(); /* 检测并提交 UI 的时间设置请求 */
            }
            if (SC7A20_IsInitialized()) {
                SC7A20_AccelLoad();      /* 三轴加速度原始数据镜像（Read*_mg 读它换算） */
            }
            wled_soc_manage();   /* 500ms：管理假 C2 口（插入/让位/补位） */
            WLED_Update();       /* 500ms：WLED 保护（仅开启时判断，过温/零电量强制关灯） */
        }
        /* 每 10ms：仅轮询计时（WLED 保护已移至 500ms 周期，灯关时不判断） */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ==================== 胚胎区：自动重力方向感应（未来功能，仅注释说明，暂不启用） ====================
 * 目标：依据 SC7A20 三轴重力分量（驱动句柄镜像，1g ≈ 1000mg）自动判断设备姿态，
 *       为 UI 旋转（横/竖屏）、休眠策略或重力相关交互提供方向依据。
 *
 * 设想算法（阈值可调，加入迟滞防临界抖动）：
 *   - 取最大 |axis| 为主受力轴：
 *       x >= +600mg → 右侧立；x <= -600mg → 左侧立；
 *       y >= +600mg → 竖屏正向（充电口朝下）；y <= -600mg → 竖屏反向；
 *       否则（|z| 主导）→ 平放（可再分正/反面）。
 *   - 迟滞：进入新方向需超过高阈值，回到原方向需低于低阈值。
 *
 * 未来接入 load_task 0.5s 循环后调用（读驱动句柄镜像，不直接访问 I2C）。
 * 代码占位（#if 0 排除，未定义符号不影响编译）：
 */
#if 0
typedef enum {
    ORIENT_FLAT,     /* 平放 */
    ORIENT_LEFT,     /* 左立 */
    ORIENT_RIGHT,    /* 右立 */
    ORIENT_UP,       /* 竖屏正向 */
    ORIENT_DOWN,     /* 竖屏反向 */
} orient_t;

static orient_t s_orient = ORIENT_FLAT;

/* 依据 SC7A20 句柄镜像估算姿态（未来在 0.5s 循环里调用） */
static void orient_update(void)
{
    float x = SC7A20_ReadX_mg();   /* 读驱动句柄镜像（load_task 已 AccelLoad 刷新） */
    float y = SC7A20_ReadY_mg();
    float z = SC7A20_ReadZ_mg();
    /* ... 阈值判断 + 迟滞，更新 s_orient ... */
}
#endif
