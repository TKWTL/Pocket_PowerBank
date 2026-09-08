/* WLED 照明灯外设（独立于 SW6306 驱动）
 *
 * 硬件：MCU TMR1_CH2(PB0) PWM → 外部升压芯片 → LED 照明灯，
 * PWM 占空比 0~1023（TMR1 period=1023）控制亮度。
 *
 * 保护：过温（NTC/芯片温度）或零电量（SW6306 显示 0%）时自动关灯。
 * 注：曾用 VBAT 电压判断低压保护，但 WLED 开启瞬间大电流把 VBAT 瞬时拉低
 * 误触发关灯（实测状态页 VBAT 6979mV 时保护读到 3892mV），电压判断已删除。
 *
 * 注意：SW6306 芯片自身的 WLED 引脚功能（SW6306_WLEDSet 等）
 * 与本外设无关，本工程未使用。
 */
#ifndef __WLED_H__
#define __WLED_H__

#include "stdint.h"

/******************************用户设置区开始**********************************/
/* WLED 保护阈值：仅过温（NTC/芯片温度）+ 零电量（SW6306 显示 0%）。
 * 电压判断（battery low）已删除：WLED 开启瞬间大电流把 VBAT 瞬时拉低，
 * 会误触发低压关灯（实测 VBAT 6979mV 时保护读到 3892mV）。 */
#ifndef WLED_NTC_OFF_C
#define WLED_NTC_OFF_C                  60          //WLED NTC过温关闭阈值（°C）
#endif
#ifndef WLED_NTC_RECOVER_C
#define WLED_NTC_RECOVER_C              55          //WLED NTC过温恢复阈值（°C）
#endif
#ifndef WLED_CHIP_OFF_C
#define WLED_CHIP_OFF_C                 100.0f      //WLED 芯片过温关闭阈值（°C）
#endif
#ifndef WLED_CHIP_RECOVER_C
#define WLED_CHIP_RECOVER_C             90.0f       //WLED 芯片过温恢复阈值（°C）
#endif
#ifndef WLED_BRIGHTNESS_MAX
#define WLED_BRIGHTNESS_MAX             26          //WLED 亮度值上限（0~26，PWM=(level+4)²）
#endif
/******************************用户设置区结束**********************************/

/* 亮度控制（TMR1_CH2 = PB0）：
 * 亮度值 0~26 → PWM 占空比 0~1023：(level+4)²（level=0 → PWM 0），
 * 平方曲线补偿人眼对数感知，亮度越高每级变化越缓。 */
void WLED_Init(void);                          /* 初始化 PWM 亮度状态（TMR1 已由 wk_tmr1_init 配置） */
void WLED_SetBrightness(uint16_t level);       /* 设置亮度 0~26（保护触发时强制关闭） */
void WLED_On(void);                            /* 开灯（恢复上次亮度，无则最大） */
void WLED_Off(void);                           /* 关灯 */
uint16_t WLED_GetBrightness(void);             /* 读取当前目标亮度 */
uint8_t WLED_IsProtectedOff(void);             /* 灯是否被保护强制关闭（供 UI 同步开关状态） */
void WLED_Update(void);                        /* 周期调用：保护触发关灯；解除后不自动恢复（手动开） */
uint8_t WLED_IsZeroCapacity(void);             /* 零电量（SW6306 显示电量 0%）守护判断 */

/* 实时保护判断（借用 SW6306 实时 ADC 数据，带迟滞） */
uint8_t WLED_IsOverheated(void);    /* 实时 NTC/芯片温度+迟滞判断过温 */

#endif
