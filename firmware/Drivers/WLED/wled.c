/* WLED 照明灯外设实现
 * 硬件：TMR1_CH2(PB0) PWM → 外部升压 → LED，占空比 0~1023。
 * 保护：过温（NTC/芯片温度）+ 零电量（SW6306 显示 0%）自动关灯；
 * 电压判断（battery low）已删除（WLED 开启瞬间 VBAT 被拉低误触发，见 wled.h）。
 * 数据由 SW6306_task 周期更新镜像后调用 SW6306_Read* 获得。
 */
#include "wled.h"
#include "sw6306.h"
#include "bsp_usart.h"
#include "at32f423.h"

#define WLED_PWM_MAX 1023U      /* 与 wk_tmr1_init 的 TMR1 period=1023 一致 */

static uint16_t s_brightness = 0;   /* 目标亮度值 0~26（保护触发时实际输出 0） */
static uint8_t  s_protected  = 0;   /* 1=灯被保护强制关闭（供 UI 同步开关状态，等待手动恢复） */

/* 亮度值(0~26) → PWM(0~1023)：level=0 → 0（关）；1~26 → (level+4)²（人眼感知线性） */
static uint16_t wled_level_to_pwm(uint16_t level)
{
    uint32_t pwm;
    if (level == 0) return 0;
    pwm = (uint32_t)(level + 4U) * (uint32_t)(level + 4U);
    return (pwm > WLED_PWM_MAX) ? WLED_PWM_MAX : (uint16_t)pwm;
}

void WLED_Init(void)
{
    s_brightness = 0;
    /* TMR1_CH2 引脚与 PWM 已由 wk_tmr1_init() 配置，此处仅复位亮度状态 */
}

void WLED_SetBrightness(uint16_t level)
{
    if(level > WLED_BRIGHTNESS_MAX) level = WLED_BRIGHTNESS_MAX;
    s_brightness = level;

    /* 保护（过温 / 零电量）触发时拒绝点亮，保持关闭；保护解除后才允许手动打开 */
    if(WLED_IsOverheated() || WLED_IsZeroCapacity())
    {
        s_protected = 1;
        tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, 0);
        /* 诊断：打印拒绝点亮的原因与实际读值 */
        USART_Printf("[WLED] set rejected: overtemp=%u zerocap=%u NTC=%dC CHIP=%.1fC Cap=%d%%\n",
                     WLED_IsOverheated(), WLED_IsZeroCapacity(),
                     SW6306_ReadTNTC(), SW6306_ReadTCHIP(), SW6306_ReadCapacity());
        return;
    }
    s_protected = 0;   /* 点亮成功，清除保护状态 */
    tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, wled_level_to_pwm(level));
}

void WLED_On(void)
{
    WLED_SetBrightness(s_brightness ? s_brightness : WLED_BRIGHTNESS_MAX);
}

void WLED_Off(void)
{
    WLED_SetBrightness(0);
}

uint16_t WLED_GetBrightness(void)
{
    return s_brightness;
}

uint8_t WLED_IsProtectedOff(void)//返回灯是否被保护强制关闭（供 UI 同步开关状态）
{
    return s_protected;
}

uint8_t WLED_IsZeroCapacity(void)//零电量：SW6306 显示电量 0%（WLED 守护用）
{
    if(SW6306_ReadVBAT() < 1000) return 0;//镜像未建立（VBAT=0）不算零电量，避免开机误判
    return (SW6306_ReadCapacity() <= 0) ? 1 : 0;
}

void WLED_Update(void)
{
    /* 仅 WLED 开启时做保护判断：灯关着无需保护，也避免开机初期镜像未建立时
     * 误判 zero capacity（Cap/VBAT 均为 0）。开启期间按调用周期（500ms）判断。 */
    static uint8_t last_protected = 0;
    uint8_t prot;

    if (s_brightness == 0) {
        last_protected = 0;
        return;
    }

    /* 保护触发（过温 60°C / 零电量）→ 强制关灯。
     * 保护解除后不自动恢复点亮，保持关闭，由用户手动再开（WLED_On）。
     * 进入保护瞬间串口打印一次原因（避免刷屏）。 */
    prot = (WLED_IsOverheated() || WLED_IsZeroCapacity());
    if (prot) {
        if (last_protected == 0) {
            if (WLED_IsOverheated()) {
                USART_Printf("[WLED] OFF: over temp, NTC=%dC CHIP=%.1fC\n",
                             SW6306_ReadTNTC(), SW6306_ReadTCHIP());
            } else {
                /* 方案 C 诊断：打印实际读值，区分「镜像读到 0」与「系统复位后电量归零」。
                 * Cap=0 且 VBAT 正常(~7V) → SW6306 电量显示被清零（可能复位）；
                 * Cap=0 且 VBAT 也低 → 电压确实被拉低。 */
                USART_Printf("[WLED] OFF: zero capacity, Cap=%d%% VBAT=%dmV\n",
                             SW6306_ReadCapacity(), SW6306_ReadVBAT());
            }
        }
        s_protected = 1;
        tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, 0);
    }
    /* prot==0（保护解除）：不自动恢复，保持当前状态；允许用户手动再开 */
    last_protected = prot;
}

uint8_t WLED_IsOverheated(void)//实时NTC/芯片温度+迟滞判断过温（WLED保护用）
{
    static uint8_t overheated = 0;
    int16_t tntc = SW6306_ReadTNTC();
    float tchip = SW6306_ReadTCHIP();

    if(overheated)
    {
        if(tntc <= WLED_NTC_RECOVER_C && tchip <= WLED_CHIP_RECOVER_C)
            overheated = 0;
    }
    else
    {
        if(tntc >= WLED_NTC_OFF_C || tchip >= WLED_CHIP_OFF_C)
            overheated = 1;
    }
    return overheated;
}
