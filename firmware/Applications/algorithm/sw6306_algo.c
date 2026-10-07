/*
 * sw6306_algo.c - SW6306 business logic
 *
 * The SW6306 internal coulomb/energy gauge is the integrator.  Equivalent full
 * cycles are therefore calculated from energy snapshots at discharge-session
 * boundaries instead of MCU V*I*time integration.
 */
#include "sw6306_algo.h"
#include "sw6306.h"
#include "framework/nvm_store.h"
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
#define SW6306_CMD_LEARN          (1U << 4)
#define SW6306_CMD_RECORD_FACTORY (1U << 5)
#define SW6306_CMD_REINIT         (1U << 6)

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
    45, 30,             /* output/input W */
    0U                  /* capacity learning */
};

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
static volatile uint8_t s_commands;

static void sw6306_algo_set_command(uint8_t mask)
{
    taskENTER_CRITICAL();
    s_commands |= mask;
    taskEXIT_CRITICAL();
}

static uint8_t sw6306_algo_take_commands(void)
{
    uint8_t cmd;
    taskENTER_CRITICAL();
    cmd = s_commands;
    s_commands = 0U;
    taskEXIT_CRITICAL();
    return cmd;
}

static void sw6306_algo_restore_commands(uint8_t mask)
{
    if (mask == 0U) return;
    taskENTER_CRITICAL();
    s_commands |= mask;
    taskEXIT_CRITICAL();
}

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

    /* 让算法层配置成为唯一真实配置源：任务启动后也主动写一次，而不是依赖
     * driver 的编译期默认值（例如 driver 默认55W而菜单默认45W）。 */
    taskENTER_CRITICAL();
    s_commands = SW6306_CMD_PROTOCOL |
                 SW6306_CMD_POWER |
                 SW6306_CMD_LEARN;
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
        SW6306_AlgoInvalidateDischargeSession();
        return;
    }

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

void SW6306_AlgoProcessCommands(void)
{
    uint8_t cmd = sw6306_algo_take_commands();
    uint8_t retry = 0U;

    if (cmd == 0U) return;

    if (cmd & SW6306_CMD_REINIT) {
        SW6306_AlgoInvalidateDischargeSession();
        SW6306_MarkUninitialized();
        cmd &= (uint8_t)~SW6306_CMD_REINIT;
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
        SW6306_SetMaxInputPower((uint8_t)SW6306_AlgoConfig.input_power_w);
    }

    if (cmd & SW6306_CMD_LEARN) {
        SW6306_CapacityLearningSet(SW6306_AlgoConfig.learn_enable ? 1U : 0U);
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
    /* Driver Init restores compile-time defaults.  Re-apply the application's current
     * config after every successful re-init so menu state and hardware cannot diverge. */
    SW6306_AlgoInvalidateDischargeSession();
    sw6306_algo_set_command(SW6306_CMD_PROTOCOL |
                            SW6306_CMD_POWER |
                            SW6306_CMD_LEARN);
}

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

void SW6306_AlgoRequestCapacityLearning(void)
{
    sw6306_algo_set_command(SW6306_CMD_LEARN);
}

void SW6306_AlgoRequestRecordFactoryCapacity(void)
{
    sw6306_algo_set_command(SW6306_CMD_RECORD_FACTORY);
}

void SW6306_AlgoRequestReinit(void)
{
    sw6306_algo_set_command(SW6306_CMD_REINIT);
}

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
