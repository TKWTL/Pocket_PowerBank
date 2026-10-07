/*
 * sw6306_algo.h - SW6306 application policy/state machines
 *
 * No task is created here. SW6306_task is the single writer/executor.
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
    uint8_t learn_enable;
} sw6306_algo_config_t;

extern sw6306_algo_config_t SW6306_AlgoConfig;

void SW6306_AlgoInit(void);

/* SW6306_task only: call after fresh StatusLoad + CapacityLoad. */
void SW6306_AlgoUpdate(void);
void SW6306_AlgoProcessCommands(void);
void SW6306_AlgoInvalidateDischargeSession(void);
void SW6306_AlgoOnDriverReinitialized(void);  /* SW6306_task: 重新应用算法层配置 */

/* UI/menu thread: RAM-only requests; no I2C. */
void SW6306_AlgoRequestProtocolApply(void);
void SW6306_AlgoRequestPPSBroadcast(void);
void SW6306_AlgoRequestUFCSBroadcast(void);
void SW6306_AlgoRequestPowerApply(void);
void SW6306_AlgoRequestCapacityLearning(void);
void SW6306_AlgoRequestRecordFactoryCapacity(void);
void SW6306_AlgoRequestReinit(void);

/* Read-only derived state for UI. */
float SW6306_AlgoGetSOHPercent(void);
float SW6306_AlgoGetEquivalentCycles(void);
uint8_t SW6306_AlgoDischargeSessionActive(void);

#ifdef __cplusplus
}
#endif

#endif /* SW6306_ALGO_H */
