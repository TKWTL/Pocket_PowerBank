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
 * 会误触发低压关灯（实测 VBAT 6979mV 时保护读到 3892mV）。
 * 注：NTC 侧 60°C 是【直接关闭 WLED】（不是降档），由 wled.c 的 wled_cutoff_now()
 *     直接与本宏比较完成（与限档表无关，表只负责降档）；改本宏需同步改限档表尾部。 */
#ifndef WLED_NTC_OFF_C
#define WLED_NTC_OFF_C                  60          //WLED NTC 过温【直接关闭】阈值（°C）
#endif
#ifndef WLED_NTC_RECOVER_C
#define WLED_NTC_RECOVER_C              45          //关闭后降至此温度才允许双击重开（迟滞防振荡）
#endif
#ifndef WLED_CHIP_OFF_C
#define WLED_CHIP_OFF_C                 100.0f      //WLED 芯片过温关闭阈值（°C）
#endif
#ifndef WLED_CHIP_RECOVER_C
#define WLED_CHIP_RECOVER_C             90.0f       //WLED 芯片过温恢复阈值（°C）
#endif
#ifndef WLED_BRIGHTNESS_MIN
#define WLED_BRIGHTNESS_MIN             4           //调光亮度值下限（4²=16 → PWM 最小 16；调光不灭灯）
#endif
#ifndef WLED_BRIGHTNESS_MAX
#define WLED_BRIGHTNESS_MAX             25          //WLED 亮度值上限（0~25，PWM=level²；25=5² 取平方数→最大 PWM 625）
#endif

/* ===== 温度限档（NTC 与散热铝壳相连，读数 = 环境 + 功率电路 + LED 的合成温度）=====
 * 实测温升（挡位→ΔT）：11→18°C、16→40°C、19→50°C、25→80°C，
 * 即约 2.8~3.2°C/挡、与挡位近似线性；据此把"允许挡位"当作壳温的函数（三段折线）：
 *
 *   NTC ≤ 0°C   → 允许 25 挡（最大挡位只在 0°C 及以下开放）
 *   NTC = 30°C  → 允许 19 挡
 *   NTC = 40°C  → 允许 16 挡（实测锚点：此时 LED 板温 ≈65°C、Tj ≈80~85°C）
 *   NTC 40~60°C → 继续降档（16 → 0）
 *   NTC ≥ 60°C  → 【直接关闭 WLED】（不是降档；锁存，需降温后手动重开）
 *
 * 因为 NTC 贴在铝壳上，功率电路发热会把壳温抬上去，从而自然地压低 LED 功率上限
 * ——这正是我们要的联动，不需要单独区分"环境"与"自热"。
 * 分工：
 *   - 降档：wled.c 的 s_limit_table[]（2°C 步长查表，热路径不做除法/浮点）；
 *   - 关闭：wled_cutoff_now() 运行时直接与 WLED_NTC_OFF_C(60°C) 比较，与表无关。
 * 修改本组宏后必须同步修改该表，wled.c 有编译期长度断言与"表尾必须为 0"断言兜底。 */
#ifndef WLED_LIMIT_T_MIN_C
#define WLED_LIMIT_T_MIN_C              0           //查表起点温度（°C，同时也是"开放最大挡位"的温度上限）
#endif
#ifndef WLED_LIMIT_STEP_C
#define WLED_LIMIT_STEP_C               2           //查表步长（°C）：越小越平滑，决定表占用的 Flash
#endif
#ifndef WLED_LIMIT_SENSOR_FAULT_LV
#define WLED_LIMIT_SENSOR_FAULT_LV      16          //温度镜像不可用时的保守挡位（防开机满功率）
#endif

/* 渐变（开灯缓升 + 调光平滑共用同一套参数）：
 * 渐变发生在【PWM 值层面】——把当前 PWM 线性推到目标 PWM，每一级 PWM 都会真实写出，
 * 因此不会出现"档位整数跳变"造成的台阶（原先逐档步进就是这个毛病）。
 * 渐变时长固定（与跨度无关）：WLED_RAMP_TICKS × 10ms。
 *  - 开灯：从 0（关灯态）线性升到目标 PWM；
 *  - 调光：目标 PWM 一变，就以当时的 PWM 为起点重新起算，连续无跳变；
 *  - 关灯 / 保护关断：不渐变，直接断。 */
#ifndef WLED_RAMP_TICKS
#define WLED_RAMP_TICKS                 16          //渐变 tick 数（10ms/tick）：16 = 160ms
#endif

/* 调光串口打印：打印实际写入的 PWM 值，用于配合热成像核对。
 * 注意：改成 PWM 级渐变后，渐变期间几乎每 tick 都变一次，打印量比逐档时代大得多；
 * 需要干净日志或做热成像标记时，把它置 0。 */
#ifndef WLED_DEBUG_PRINT
#define WLED_DEBUG_PRINT                1
#endif
/******************************用户设置区结束**********************************/

/* 亮度控制（TMR1_CH2 = PB0）：
 * 亮度值 4~25 → PWM 占空比 16~625：PWM = level²（调光下限 4²=16，避免调光把灯调灭；
 * 0 仅用于显式关灯 WLED_Off / 保护关灯；25²=625，约占满量程 1023 的 61%）。
 * 人眼对亮度的感知近似光通量的平方根（非线性），故送 PWM 前先对亮度值取平方，
 * 使每级“视觉亮度”变化均匀（低亮度端精细、高亮度端粗）。
 * 调用约定：写目标亮度用 WLED_SetBrightness/WLED_On；实际输出由 10ms 的 WLED_Tick
 * 按 NTC 温度限档后渐变逼近（关灯与保护关断除外，那两种立即断）。
 * 注意：WLED_GetBrightness() 返回【用户设定值】，实际生效值看 WLED_GetOutputLevel()。 */
void WLED_Init(void);                          /* 初始化（恢复上次实际输出档位，不点灯；TMR1 已由 wk_tmr1_init 配置） */
void WLED_SetBrightness(uint16_t level);       /* 写用户目标亮度：0=关灯（立即断）；非零钳到 4~25，受温度限档后渐变 */
void WLED_Tick(void);                          /* 10ms 调用：温度限档（含 60°C 关闭）+ 亮度渐变 */
void WLED_On(void);                            /* 开灯（恢复到上次档位；无记录则亮度中点；过温未冷却时拒绝点亮） */
void WLED_Off(void);                           /* 关灯（保留档位记忆，下次开灯回到该档位） */
uint16_t WLED_GetBrightness(void);             /* 读取用户设定档位（温度限档前的目标值，UI 调光用） */
uint16_t WLED_GetOutputLevel(void);            /* 读取实际输出档位（已含温度限档；0=未输出） */
uint8_t WLED_IsProtectedOff(void);             /* 灯是否被保护强制关闭（供 UI 同步开关状态） */
void WLED_Update(void);                        /* 500ms 兜底保护：过温/零电量锁存关灯；解除后不自动恢复（手动开） */
uint8_t WLED_IsZeroCapacity(void);             /* 零电量（SW6306 显示电量 0%）守护判断 */

#endif
