/*
 * wled_algo.h - WLED application policy
 *
 * UI only sends RAM requests. load_task advances the policy and is the only
 * executor of PWM writes / fake-A1 port events. No I2C is performed here.
 */
#ifndef WLED_ALGO_H
#define WLED_ALGO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WLED_BRIGHTNESS_MIN             4U
#define WLED_BRIGHTNESS_MAX            25U

#define WLED_NTC_OFF_C                 60
#define WLED_NTC_RECOVER_C             45
#define WLED_LIMIT_T_MIN_C              0
#define WLED_LIMIT_STEP_C               2
#define WLED_LIMIT_SENSOR_FAULT_LV     16U

#define WLED_ALGO_TICK_MS              10U
#define WLED_RAMP_TICKS                16U   /* 160ms */
/* 假插入口固定为 A1：实测 A2/C2 均无效（芯片只为"检测到有负载的口"开通路，
 * 本板 A2/C2 无连接器，写 PORTEVT 事件后 sys_stat 始终为 0），故回退 A1。
 * A1 上的假插入由 UI 用 BUS 电流区分（见 main_screen.c a1_is_real_load）。 */
#define WLED_FAKE_A1_DETACH_TICKS     100U   /* 1s hand-over debounce */
#define WLED_FAKE_A1_POST_OFF_TICKS    20U   /* 200ms after PWM reaches 0 */
#define WLED_FAKE_A1_RETRY_TICKS      100U   /* 1s: covers the SW6306 status refresh period */

#ifndef WLED_DEBUG_PRINT
#define WLED_DEBUG_PRINT                1
#endif

typedef enum {
    WLED_PORT_CMD_NONE = 0,
    WLED_PORT_CMD_A1_INSERT,
    WLED_PORT_CMD_A1_REMOVE
} wled_port_cmd_t;

void WLED_AlgoInit(void);

/* UI thread: RAM-only requests. */
void WLED_AlgoRequestToggle(void);
void WLED_AlgoRequestBrightnessStep(int8_t step);
void WLED_AlgoRequestSOS(uint8_t enable); /* overrides normal light, never restores */

/* load_task only. */
void WLED_AlgoTick10ms(void);
void WLED_AlgoUpdate500ms(void);
uint8_t WLED_AlgoTakePwm(uint16_t *pwm);
wled_port_cmd_t WLED_AlgoTakePortCommand(void);

/* Read-only state for UI / PM. */
uint16_t WLED_AlgoGetBrightness(void);
uint16_t WLED_AlgoGetOutputLevel(void);
uint16_t WLED_AlgoGetPwm(void);
uint8_t WLED_AlgoIsOn(void);
uint8_t WLED_AlgoIsProtectedOff(void);
uint8_t WLED_AlgoOwnsFakeA1(void);

#ifdef __cplusplus
}
#endif

#endif
