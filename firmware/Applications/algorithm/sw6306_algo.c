/*
 * sw6306_algo.c - SW6306 business logic
 *
 * Application policy is divided into independent algorithm sections below.
 * SW6306_task remains the single hardware executor.
 */
#include "sw6306_algo.h"
#include "sw6306.h"
#include "framework/nvm_store.h"
#include "framework/pm_api.h"
#include "FreeRTOS.h"
#include "task.h"

#define SW6306_ALGO_START_CONFIRM_SAMPLES   2U
#define SW6306_ALGO_STOP_CONFIRM_SAMPLES    4U
#define SW6306_ALGO_MIN_FULL_ENERGY_MWH   500.0f
#define SW6306_ALGO_MAX_SESSION_RATIO       1.25f

#define SW6306_CMD_PROTOCOL       (1U << 0)
#define SW6306_CMD_PPS_BROADCAST  (1U << 1)
#define SW6306_CMD_UFCS_BROADCAST (1U << 2)
#define SW6306_CMD_POWER          (1U << 3)
#define SW6306_CMD_LEARN_ENABLE   (1U << 4)
#define SW6306_CMD_RECORD_FACTORY (1U << 5)
#define SW6306_CMD_REINIT         (1U << 6)
#define SW6306_CMD_LEARN_REARM    (1U << 7)
#define SW6306_CMD_LOW_CURRENT    (1U << 8)

sw6306_algo_config_t SW6306_AlgoConfig = {
    1U, 1U,             /* PD out/in */
    1U, 1U,             /* PPS1/PPS3 */
    1U, 1U,             /* QC/FCP */
    1U, 1U,             /* AFC out/in */
    1U, 1U,             /* SCP out/in */
    1U, 1U,             /* PE/SFCP */
    1U, 1U,             /* VOOC out/in */
    1U,                 /* SVOOC */
    1U, 1U,             /* UFCS out/in */
    55, 30              /* output/input W */
};

/***************************** 通用命令请求机制开始 *****************************/
/* UI/算法只置 RAM bit；真正的 SW6306 寄存器写由 SW6306_task 调用
 * SW6306_AlgoProcessCommands() 串行执行，避免 UI 线程直接发 I2C。 */
static volatile uint16_t s_commands;
static volatile uint8_t s_special_toggle_req;
static volatile sw6306_special_mode_t s_special_mode;
static uint32_t s_last_high_load_tick;
static uint8_t s_slow_stop_count;

/* RAM-only parameters; not written to NVM. */
uint16_t SW6306_LowCurrentThreshold_mA = 80U;
uint32_t SW6306_LowCurrentIdleTimeout_ms = 3600000UL;

static void sw6306_algo_set_command(uint16_t mask)
{
    taskENTER_CRITICAL();
    s_commands |= mask;
    taskEXIT_CRITICAL();
}

static uint16_t sw6306_algo_take_commands(void)
{
    uint16_t cmd;
    taskENTER_CRITICAL();
    cmd = s_commands;
    s_commands = 0U;
    taskEXIT_CRITICAL();
    return cmd;
}

static void sw6306_algo_restore_commands(uint16_t mask)
{
    if (mask == 0U) return;
    taskENTER_CRITICAL();
    s_commands |= mask;
    taskEXIT_CRITICAL();
}
/***************************** 通用命令请求机制结束 *****************************/

/******************************* 容量学习算法开始 *******************************/
/* 容量学习固定常开，不再提供 UI enable。
 * “确保 LEARNEN=1”和“清 LEARN_END 重新武装”必须分离：
 *   1. 上电/驱动重初始化只设置 LEARNEN，不清完成标志；
 *   2. CapacityLoad 读到 DONE 后，说明本次完整 UVLO->再充电学习已经完成，
 *      SW6306 的 maxcap 已成为新的学习容量；算法只在首次观察到 DONE 时请求 re-arm；
 *   3. re-arm 仅清 LEARN_END 并保持 LEARNEN=1，等待下一次真正的完整学习条件。
 * SOH = 当前学习得到的 max energy / 手动 Record SOH 保存的 factory energy。
 * EFC session 与容量学习彼此独立：前者统计使用量，后者更新 SW6306 的满容量估计。 */
static sw6306_learn_state_t s_learn_state = SW6306_LEARN_ST_UNKNOWN;

static void sw6306_algo_update_capacity_learning(void)
{
    sw6306_learn_state_t now = SW6306_ReadLearnState();

    if (now == SW6306_LEARN_ST_DONE && s_learn_state != SW6306_LEARN_ST_DONE) {
        sw6306_algo_set_command(SW6306_CMD_LEARN_REARM);
    }
    s_learn_state = now;
}
/******************************* 容量学习算法结束 *******************************/

/************************ SW6306 分层寄存器Load算法开始 ************************/
/* 保留原SW6306_task的100ms错相访问，避免把多组I2C读集中到同一时刻：
 *   phase0: ADC
 *   phase1: Status + NTC换算
 *   phase2: Port status
 *   phase3: 留空（保持原Port→Power约200ms间隔）
 *   phase4: Power
 * 上述完整周期约500ms。Capacity/能量计只在每两个完整周期读取一次，即约1s；
 * EFC/容量学习只在fresh Capacity镜像到达后推进。 */
static uint8_t s_load_phase;
static uint8_t s_capacity_cycle;

void SW6306_AlgoLoadStep(void)
{
    if (!SW6306_IsInitialized()) return;

    switch (s_load_phase) {
    case 0U:
        SW6306_ADCLoad();
        break;
    case 1U:
        SW6306_StatusLoad();
        SW6306_NTCTempLoad();
        break;
    case 2U:
        SW6306_PortStatusLoad();
        break;
    case 3U:
        break;
    default:
        SW6306_PowerLoad();
        /* IsInitialized()现在是纯RAM查询；芯片独立复位/配置丢失只允许在
         * fresh PowerLoad之后由ValidateInitialized()判定。 */
        if (SW6306_ValidateInitialized() != 0U) {
            if (++s_capacity_cycle >= 2U) {
                s_capacity_cycle = 0U;
                SW6306_CapacityLoad();
                /* 失败时镜像仍是旧数据，不允许用 stale capacity 推进学习/EFC。 */
                if (SW6306_CapacityLoadOK() != 0U) {
                    SW6306_AlgoUpdate();
                }
            }
        }
        break;
    }

    s_load_phase++;
    if (s_load_phase >= 5U) s_load_phase = 0U;
}

void SW6306_AlgoLoadAll(void)
{
    if (!SW6306_IsInitialized()) return;

    /* 唤醒预取可由load_task调用，因此这里只刷新driver镜像，不推进由
     * SW6306_task独占的Load相位/EFC状态。 */
    SW6306_ADCLoad();
    SW6306_StatusLoad();
    SW6306_NTCTempLoad();
    SW6306_PortStatusLoad();
    SW6306_PowerLoad();
    if (SW6306_ValidateInitialized() != 0U) {
        /* 唤醒预取只刷新镜像；成功/失败都不推进学习或EFC状态机。 */
        SW6306_CapacityLoad();
    }
}
/************************ SW6306 分层寄存器Load算法结束 ************************/

/***************************** EFC 容量统计算法开始 *****************************/
/* SW6306 内部能量计负责积分，MCU 不做 V*I*time。
 * 每个连续放电 session 开始时锁存 E_start 与当时的 E_full；
 * 结束时使用最后一个明确处于放电态的 E_end：
 *   ΔEFC = (E_start - E_end) / E_full_start
 * 这样循环数始终按当次电池实际可用容量归一化，SOH 下降后不会继续按出厂 Wh 计算。
 * 开始/停止分别用连续样本确认；重初始化、能量倒增或异常超额 session 直接丢弃。 */
typedef struct {
    uint8_t active;
    uint8_t start_count;
    uint8_t stop_count;
    float candidate_start_mwh;
    float candidate_full_mwh;
    float candidate_end_mwh;
    float last_discharge_mwh;
    float start_mwh;
    float full_mwh;
} sw6306_discharge_session_t;

static sw6306_discharge_session_t s_session;


static void sw6306_algo_finalize_session(float end_mwh)
{
    float delta_mwh;
    float delta_cycles;

    if (!s_session.active) return;

    if (s_session.full_mwh < SW6306_ALGO_MIN_FULL_ENERGY_MWH ||
        end_mwh >= s_session.start_mwh) {
        SW6306_AlgoInvalidateDischargeSession();
        return;
    }

    delta_mwh = s_session.start_mwh - end_mwh;
    if (delta_mwh > s_session.full_mwh * SW6306_ALGO_MAX_SESSION_RATIO) {
        SW6306_AlgoInvalidateDischargeSession();
        return;
    }

    delta_cycles = delta_mwh / s_session.full_mwh;
    if (delta_cycles > 0.0f) {
        nvm_add_equivalent_cycles(delta_cycles);
    }

    SW6306_AlgoInvalidateDischargeSession();
}

void SW6306_AlgoInit(void)
{
    SW6306_AlgoInvalidateDischargeSession();
    s_learn_state = SW6306_LEARN_ST_UNKNOWN;
    s_load_phase = 0U;
    s_capacity_cycle = 1U;
    s_special_mode = SW6306_SPECIAL_NONE;
    s_special_toggle_req = 0U;
    s_slow_stop_count = 0U;
    s_last_high_load_tick = xTaskGetTickCount();

    /* 算法层配置是唯一真实配置源；容量学习固定常开。
     * 注意这里只“确保使能”，不清 LEARN_END。 */
    taskENTER_CRITICAL();
    s_commands = SW6306_CMD_PROTOCOL |
                 SW6306_CMD_POWER |
                 SW6306_CMD_LEARN_ENABLE;
    taskEXIT_CRITICAL();
}

void SW6306_AlgoInvalidateDischargeSession(void)
{
    s_session.active = 0U;
    s_session.start_count = 0U;
    s_session.stop_count = 0U;
    s_session.candidate_start_mwh = 0.0f;
    s_session.candidate_full_mwh = 0.0f;
    s_session.candidate_end_mwh = 0.0f;
    s_session.last_discharge_mwh = 0.0f;
    s_session.start_mwh = 0.0f;
    s_session.full_mwh = 0.0f;
}

void SW6306_AlgoUpdate(void)
{
    uint8_t discharging;
    float remain_mwh;
    float full_mwh;

    if (!SW6306_IsInitialized()) {
        s_learn_state = SW6306_LEARN_ST_UNKNOWN;
        SW6306_AlgoInvalidateDischargeSession();
        return;
    }

    sw6306_algo_update_capacity_learning();

    discharging = SW6306_IsDischarging() ? 1U : 0U;
    remain_mwh = SW6306_ReadRemainEnergy_mWh();
    full_mwh = SW6306_ReadMaxEnergy_mWh();

    if (!s_session.active) {
        if (discharging) {
            if (s_session.start_count == 0U) {
                /* Keep the first sample, not the confirmation sample, so startup energy
                 * loss during debounce is still included in the session delta. */
                s_session.candidate_start_mwh = remain_mwh;
                s_session.candidate_full_mwh = full_mwh;
            }
            if (s_session.start_count < SW6306_ALGO_START_CONFIRM_SAMPLES) {
                s_session.start_count++;
            }
            if (s_session.start_count >= SW6306_ALGO_START_CONFIRM_SAMPLES) {
                if (s_session.candidate_full_mwh >= SW6306_ALGO_MIN_FULL_ENERGY_MWH &&
                    s_session.candidate_start_mwh > 0.0f) {
                    s_session.start_mwh = s_session.candidate_start_mwh;
                    s_session.full_mwh = s_session.candidate_full_mwh;
                    s_session.last_discharge_mwh = remain_mwh;
                    s_session.active = 1U;
                    s_session.stop_count = 0U;
                } else {
                    s_session.start_count = 0U;
                }
            }
        } else {
            s_session.start_count = 0U;
        }
        return;
    }

    if (discharging) {
        /* Keep the most recent sample that is still unquestionably inside discharge.
         * If the next state is charging, using the first non-discharge sample would
         * subtract some newly charged energy and under-count the session. */
        s_session.last_discharge_mwh = remain_mwh;
        s_session.stop_count = 0U;
        return;
    }

    if (s_session.stop_count == 0U) {
        s_session.candidate_end_mwh = s_session.last_discharge_mwh;
    }
    if (s_session.stop_count < SW6306_ALGO_STOP_CONFIRM_SAMPLES) {
        s_session.stop_count++;
    }
    if (s_session.stop_count >= SW6306_ALGO_STOP_CONFIRM_SAMPLES) {
        sw6306_algo_finalize_session(s_session.candidate_end_mwh);
    }
}
/***************************** EFC 容量统计算法结束 *****************************/



/*************************** 小电流与慢充策略开始 *******************************/
/* UI requests stay in RAM; this runs in SW6306_task. Low current ends after
 * 1h without IBUS>80mA. Slow charge halves current configured input power. */
void SW6306_AlgoService100ms(void)
{
    uint8_t toggle;
    sw6306_special_mode_t next;
    uint32_t now = xTaskGetTickCount();

    if (!SW6306_IsInitialized()) return;

    taskENTER_CRITICAL();
    toggle = s_special_toggle_req;
    s_special_toggle_req = 0U;
    taskEXIT_CRITICAL();

    next = s_special_mode;
    if (toggle != 0U) {
        next = (next != SW6306_SPECIAL_NONE) ? SW6306_SPECIAL_NONE :
               (SW6306_IsCharging() ? SW6306_SPECIAL_SLOW_CHARGE : SW6306_SPECIAL_LOW_CURRENT);
    } else if (next == SW6306_SPECIAL_SLOW_CHARGE) {
        if (SW6306_IsCharging()) s_slow_stop_count = 0U;
        else if (++s_slow_stop_count >= 20U) next = SW6306_SPECIAL_NONE; /* 2s debounce */
    } else if (next == SW6306_SPECIAL_LOW_CURRENT) {
        if (SW6306_IsCharging()) {
            next = SW6306_SPECIAL_NONE;
        } else {
            if (SW6306_ReadIBUS() > SW6306_LowCurrentThreshold_mA) s_last_high_load_tick = now;
            if ((uint32_t)(now - s_last_high_load_tick) >=
                pdMS_TO_TICKS(SW6306_LowCurrentIdleTimeout_ms)) next = SW6306_SPECIAL_NONE;
        }
    }

    if (next != s_special_mode) {
        s_special_mode = next;
        s_slow_stop_count = 0U;
        s_last_high_load_tick = now;
        sw6306_algo_set_command(SW6306_CMD_POWER | SW6306_CMD_LOW_CURRENT);
    }
}

sw6306_special_mode_t SW6306_AlgoGetSpecialMode(void)
{
    return s_special_mode;
}
/*************************** 小电流与慢充策略结束 *******************************/

/*************************** SW6306 配置应用算法开始 ****************************/
/* 协议、功率、容量学习常开、SOH基准记录和手动重初始化都由命令位合并，
 * 只在 SW6306_task 中执行实际寄存器操作。 */
void SW6306_AlgoProcessCommands(void)
{
    uint16_t cmd = sw6306_algo_take_commands();
    uint8_t retry = 0U;

    if (cmd == 0U) return;

    if (cmd & SW6306_CMD_REINIT) {
        SW6306_AlgoInvalidateDischargeSession();
        SW6306_MarkUninitialized();
        cmd &= (uint16_t)~SW6306_CMD_REINIT;
    }

    if (!SW6306_IsInitialized()) {
        /* Configuration commands survive until the task has reinitialized the device. */
        sw6306_algo_restore_commands(cmd);
        return;
    }

    if (cmd & SW6306_CMD_PROTOCOL) {
        SW6306_ProtocolEnable(SW6306_PROTO_PD,    SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.pd_out);
        SW6306_ProtocolEnable(SW6306_PROTO_PD,    SW6306_PROTO_DIR_SINK,   SW6306_AlgoConfig.pd_in);
        SW6306_PPSEnable(SW6306_PPS_1, SW6306_AlgoConfig.pps1);
        SW6306_PPSEnable(SW6306_PPS_3, SW6306_AlgoConfig.pps3);
        SW6306_ProtocolEnable(SW6306_PROTO_QC,    SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.qc);
        SW6306_ProtocolEnable(SW6306_PROTO_FCP,   SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.fcp);
        SW6306_ProtocolEnable(SW6306_PROTO_AFC,   SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.afc_out);
        SW6306_ProtocolEnable(SW6306_PROTO_AFC,   SW6306_PROTO_DIR_SINK,   SW6306_AlgoConfig.afc_in);
        SW6306_ProtocolEnable(SW6306_PROTO_SCP,   SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.scp_out);
        SW6306_ProtocolEnable(SW6306_PROTO_SCP,   SW6306_PROTO_DIR_SINK,   SW6306_AlgoConfig.scp_in);
        SW6306_ProtocolEnable(SW6306_PROTO_PE,    SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.pe);
        SW6306_ProtocolEnable(SW6306_PROTO_SFCP,  SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.sfcp);
        SW6306_ProtocolEnable(SW6306_PROTO_VOOC,  SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.vooc_out);
        SW6306_ProtocolEnable(SW6306_PROTO_VOOC,  SW6306_PROTO_DIR_SINK,   SW6306_AlgoConfig.vooc_in);
        SW6306_ProtocolEnable(SW6306_PROTO_SVOOC, SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.svooc);
        SW6306_ProtocolEnable(SW6306_PROTO_UFCS,  SW6306_PROTO_DIR_SOURCE, SW6306_AlgoConfig.ufcs_out);
        SW6306_ProtocolEnable(SW6306_PROTO_UFCS,  SW6306_PROTO_DIR_SINK,   SW6306_AlgoConfig.ufcs_in);
    }

    if (cmd & SW6306_CMD_POWER) {
        SW6306_SetMaxOutputPower((uint8_t)SW6306_AlgoConfig.output_power_w);
        uint8_t input_w = (uint8_t)SW6306_AlgoConfig.input_power_w;
        if (s_special_mode == SW6306_SPECIAL_SLOW_CHARGE) {
            input_w = (uint8_t)((input_w + 1U) / 2U);
        }
        SW6306_SetMaxInputPower(input_w);
    }

    if (cmd & SW6306_CMD_LOW_CURRENT) {
        SW6306_SetLowCurrentMode(s_special_mode == SW6306_SPECIAL_LOW_CURRENT);
    }

    if (cmd & SW6306_CMD_LEARN_ENABLE) {
        SW6306_CapacityLearningSet(1U);
    }

    if (cmd & SW6306_CMD_LEARN_REARM) {
        SW6306_CapacityLearningRearm();
    }

    if (cmd & SW6306_CMD_RECORD_FACTORY) {
        float wh = SW6306_ReadMaxEnergy_mWh() / 1000.0f;
        if (wh > 0.1f) {
            nvm_set_factory_capacity_wh(wh);
        } else {
            retry |= SW6306_CMD_RECORD_FACTORY;
        }
    }

    if (cmd & SW6306_CMD_PPS_BROADCAST) {
        SW6306_PPSBroadcast();
    }
    if (cmd & SW6306_CMD_UFCS_BROADCAST) {
        SW6306_UFCSBroadcast();
    }

    sw6306_algo_restore_commands(retry);
}

void SW6306_AlgoOnDriverReinitialized(void)
{
    /* Driver Init 恢复编译期默认值：重新应用当前配置并确保容量学习开启，
     * 但仍不清 LEARN_END；若已有 DONE，下一次 AlgoUpdate 会消费并 re-arm。 */
    SW6306_AlgoInvalidateDischargeSession();
    s_learn_state = SW6306_LEARN_ST_UNKNOWN;
    s_load_phase = 0U;
    s_capacity_cycle = 1U;
    sw6306_algo_set_command(SW6306_CMD_PROTOCOL |
                            SW6306_CMD_POWER |
                            SW6306_CMD_LEARN_ENABLE |
                            SW6306_CMD_LOW_CURRENT);
}
/*************************** SW6306 配置应用算法结束 ****************************/

/***************************** UI/菜单请求接口开始 ******************************/
void SW6306_AlgoRequestProtocolApply(void)
{
    sw6306_algo_set_command(SW6306_CMD_PROTOCOL);
}

void SW6306_AlgoRequestPPSBroadcast(void)
{
    sw6306_algo_set_command(SW6306_CMD_PPS_BROADCAST);
}

void SW6306_AlgoRequestUFCSBroadcast(void)
{
    sw6306_algo_set_command(SW6306_CMD_UFCS_BROADCAST);
}

void SW6306_AlgoRequestPowerApply(void)
{
    sw6306_algo_set_command(SW6306_CMD_POWER);
}

void SW6306_AlgoRequestSpecialToggle(void)
{
    taskENTER_CRITICAL();
    s_special_toggle_req ^= 1U;
    taskEXIT_CRITICAL();
}

void SW6306_AlgoRequestRecordFactoryCapacity(void)
{
    sw6306_algo_set_command(SW6306_CMD_RECORD_FACTORY);
}

void SW6306_AlgoRequestReinit(void)
{
    sw6306_algo_set_command(SW6306_CMD_REINIT);
}
/***************************** UI/菜单请求接口结束 ******************************/

/************************** SOH / 统计结果读取算法开始 **************************/
/* SOH 使用 SW6306 最近一次学习得到的满能量与 NVM 中手动记录的出厂基准相比；
 * EFC 则直接读取每个放电 session 累加后的 NVM 值。 */
float SW6306_AlgoGetSOHPercent(void)
{
    float factory_wh = nvm_get_factory_capacity_wh();

    if (!nvm_is_valid() || factory_wh <= 0.1f) return 0.0f;
    return SW6306_ReadMaxEnergy_mWh() / (factory_wh * 1000.0f) * 100.0f;
}

float SW6306_AlgoGetEquivalentCycles(void)
{
    return nvm_is_valid() ? nvm_get_equivalent_cycles() : 0.0f;
}

uint8_t SW6306_AlgoDischargeSessionActive(void)
{
    return s_session.active;
}
/************************** SOH / 统计结果读取算法结束 **************************/

/************************ SW6306 低功耗回调算法开始 ***************************/
/* DeepSleep前让SW6306进入LP；按键或VBUS事件仍可经IRQ唤醒MCU。
 * resume先Unlock；若器件状态已失效则重新Init，并通知算法层重新应用当前配置。 */
void SW6306_AlgoPmSuspend(void *ctx)
{
    (void)ctx;

    if (pm_api_transport_mode_requested() != 0U) {
        /* 运输模式不保留任何输出/充放电活动：先强制关闭端口，再进入芯片最低功耗。 */
        SW6306_ForceOff();
        SW6306_LPSet();
    } else {
        SW6306_LPSet();
    }
}

void SW6306_AlgoPmResume(void *ctx)
{
    (void)ctx;

    SW6306_Unlock();
    if (SW6306_IsInitialized() == 0U) {
        SW6306_Init();
        if (SW6306_IsInitialized() != 0U) {
            SW6306_AlgoOnDriverReinitialized();
        }
    }
}
/************************ SW6306 低功耗回调算法结束 ***************************/
