/*
 * sc7a20_algo.c - low-rate 180-degree orientation estimator
 */
#include "sc7a20_algo.h"
#include "sc7a20.h"

sc7a20_algo_config_t SC7A20_AlgoConfig = {
    0U, /* pickup_wake：默认关闭，保持现有最低休眠功耗 */
    0U  /* motion_wake：默认关闭 */
};

/************************* SC7A20 180°姿态判断算法开始 **************************/
/* 应用层以 5Hz 读取校准后的 X 轴：
 * X≈-1g 判为 NORMAL，X≈+1g 判为 FLIPPED，中间区域不累计；
 * 新方向需连续约2s满足阈值才确认，避免手持抖动导致屏幕反复翻转。 */

static volatile sc7a20_orientation_t s_orientation;
static sc7a20_orientation_t s_candidate;
static uint8_t s_candidate_count;

void SC7A20_AlgoInit(void)
{
    s_orientation = SC7A20_ORIENT_UNKNOWN;
    s_candidate = SC7A20_ORIENT_UNKNOWN;
    s_candidate_count = 0U;
}

void SC7A20_AlgoUpdate(float x_mg)
{
    sc7a20_orientation_t next;

    /* 本机安装方向：X≈-1g为正常，X≈+1g为倒置；中间区域不推进计时。 */
    if (x_mg <= -SC7A20_ALGO_ENTER_MG) {
        next = SC7A20_ORIENT_NORMAL;
    } else if (x_mg >= SC7A20_ALGO_ENTER_MG) {
        next = SC7A20_ORIENT_FLIPPED;
    } else {
        s_candidate = SC7A20_ORIENT_UNKNOWN;
        s_candidate_count = 0U;
        return;
    }

    if (next == s_orientation) {
        s_candidate = SC7A20_ORIENT_UNKNOWN;
        s_candidate_count = 0U;
        return;
    }

    if (next != s_candidate) {
        s_candidate = next;
        s_candidate_count = 0U;
        return;
    }

    if (++s_candidate_count >= SC7A20_ALGO_CONFIRM_SAMPLES) {
        s_orientation = next;
        s_candidate = SC7A20_ORIENT_UNKNOWN;
        s_candidate_count = 0U;
    }
}

sc7a20_orientation_t SC7A20_AlgoGetOrientation(void)
{
    return s_orientation;
}
/************************* SC7A20 180°姿态判断算法结束 **************************/

/*********************** SC7A20 低功耗唤醒策略算法开始 ************************/
/* Pickup Wake / Motion Wake 目前只建立电源策略基础，不配置具体阈值或INT路由：
 * - 任一开关开启：DeepSleep期间SC7A20保持10Hz ODR，后续可直接增加对应中断条件；
 * - 两者都关闭：沿用最低功耗路径，CTRL_REG1清零进入Power-down。
 * 唤醒后统一恢复正常10Hz采样；若休眠时Power-down清掉了轴使能，则用Init完整恢复。
 * 这两个开关仅为RAM运行时配置，不做NVM持久化。 */
void SC7A20_AlgoPmSuspend(void *ctx)
{
    (void)ctx;

    if (SC7A20_AlgoConfig.pickup_wake || SC7A20_AlgoConfig.motion_wake) {
        /* 正常运行时本来就是10Hz；若此前I2C故障把initialized清掉，则先尝试恢复。
         * Init失败时退回Power-down，至少保证共享INT线极性和休眠功耗处于安全状态。 */
        if (!SC7A20_IsInitialized()) {
            (void)SC7A20_Init();
        }
        if (SC7A20_IsInitialized()) {
            (void)SC7A20_SetODR(SC7A20_ODR_10HZ);
            /* TODO: 分别按 Pickup/Motion 开关配置阈值、持续时间与 INT 路由。 */
        } else {
            (void)SC7A20_LowPowerSet();
        }
    } else {
        (void)SC7A20_LowPowerSet();
    }
}

void SC7A20_AlgoPmResume(void *ctx)
{
    (void)ctx;

    if (SC7A20_AlgoConfig.pickup_wake || SC7A20_AlgoConfig.motion_wake) {
        /* 保持唤醒检测时并未Power-down，通常只需确认10Hz；若通信异常导致
         * initialized被清，则重新Init恢复完整默认配置。 */
        if (SC7A20_IsInitialized()) {
            (void)SC7A20_SetODR(SC7A20_ODR_10HZ);
        } else {
            (void)SC7A20_Init();
        }
    } else {
        /* LowPowerSet把CTRL_REG1整体写0，轴使能位也被清除，必须Init而不能只SetODR。 */
        (void)SC7A20_Init();
    }
}
/*********************** SC7A20 低功耗唤醒策略算法结束 ************************/
