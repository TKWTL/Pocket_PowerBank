/*
 * sw6306_algo.h - SW6306 application policy/state machines
 *
 * No task is created here. Normal register writes are serialized by SW6306_task;
 * pm_device may call the dedicated suspend/resume hooks during the bus-safe PM window.
 */
#ifndef SW6306_ALGO_H
#define SW6306_ALGO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pd_out;
    uint8_t pd_in;
    uint8_t pps1;
    uint8_t pps3;
    uint8_t qc;
    uint8_t fcp;
    uint8_t afc_out;
    uint8_t afc_in;
    uint8_t scp_out;
    uint8_t scp_in;
    uint8_t pe;
    uint8_t sfcp;
    uint8_t vooc_out;
    uint8_t vooc_in;
    uint8_t svooc;
    uint8_t ufcs_out;
    uint8_t ufcs_in;

    int32_t output_power_w;
    int32_t input_power_w;
} sw6306_algo_config_t;

extern sw6306_algo_config_t SW6306_AlgoConfig;

#define SW6306_ALGO_LOAD_STEP_MS 100U

/* RAM-only low-current policy: default 80mA / 1h idle. */
extern uint16_t SW6306_LowCurrentThreshold_mA;
extern uint32_t SW6306_LowCurrentIdleTimeout_ms;
typedef enum {
    SW6306_SPECIAL_NONE = 0,
    SW6306_SPECIAL_LOW_CURRENT,
    SW6306_SPECIAL_SLOW_CHARGE
} sw6306_special_mode_t;

void SW6306_AlgoInit(void);

/* SW6306_task只负责周期调度；具体Load分组与周期由algo持有。
 * Step: 100ms调用一次，ADC/Status/Port/Power约500ms一轮，Capacity约1s一轮。
 * All: 唤醒预取等场景强制刷新全部镜像。 */
void SW6306_AlgoLoadStep(void);
void SW6306_AlgoLoadAll(void);

/* 由容量Load路径在获得fresh Capacity镜像后调用。 */
void SW6306_AlgoUpdate(void);
void SW6306_AlgoProcessCommands(void);
void SW6306_AlgoService100ms(void);
void SW6306_AlgoInvalidateDischargeSession(void);
void SW6306_AlgoOnDriverReinitialized(void);  /* SW6306_task: 重新应用算法层配置 */

/* pm_device callbacks：SW6306低功耗/恢复策略由算法层统一持有。 */
void SW6306_AlgoPmSuspend(void *ctx);
void SW6306_AlgoPmResume(void *ctx);

/* UI/menu thread: RAM-only requests; no I2C. */
void SW6306_AlgoRequestProtocolApply(void);
void SW6306_AlgoRequestPPSBroadcast(void);
void SW6306_AlgoRequestUFCSBroadcast(void);
void SW6306_AlgoRequestPowerApply(void);
void SW6306_AlgoRequestRecordFactoryCapacity(void);
void SW6306_AlgoRequestReinit(void);
void SW6306_AlgoRequestSpecialToggle(void);

/* Read-only derived state for UI. */
float SW6306_AlgoGetSOHPercent(void);
float SW6306_AlgoGetEquivalentCycles(void);
uint8_t SW6306_AlgoDischargeSessionActive(void);
sw6306_special_mode_t SW6306_AlgoGetSpecialMode(void);

#ifdef __cplusplus
}
#endif

#endif /* SW6306_ALGO_H */
