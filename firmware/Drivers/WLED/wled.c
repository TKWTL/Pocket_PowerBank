/* WLED 照明灯外设实现
 * 硬件：TMR1_CH2(PB0) PWM → 外部升压 → LED，占空比 0~1023。
 * 保护：过温（NTC/芯片温度）+ 零电量（SW6306 显示 0%）自动关灯；
 * 电压判断（battery low）已删除（WLED 开启瞬间 VBAT 被拉低误触发，见 wled.h）。
 * 数据由 SW6306_task 周期更新镜像后调用 SW6306_Read* 获得。
 *
 * 亮度档位语义（三层）：
 *  - s_level：用户设定档位（UI 调光写入），0=关灯；
 *  - s_out  ：实际输出档位 = min(s_level, 温度允许上限)；0=未输出；
 *  - s_pwm  ：真正写入 TMR1 的占空比 = s_out²，并在 PWM 值层面做线性渐变。
 * 掉电/复位后档位不保留（不做 .noinit 记忆）：复位即回到默认，由用户重新调。
 *
 * 温控限档（见 wled.h 设置区）：NTC 与散热铝壳相连，其读数 = 环境 + 功率电路 +
 * LED 的合成温度，直接作为限档依据——功率电路发热会自然压低 LED 功率上限。
 * 60°C 硬关闭走保护锁存路径，需降温后手动重开。
 *
 * 渐变（两处共用同一套参数，见 wled.h 的 WLED_RAMP_TICKS）：
 *  - 开灯：PWM 从 0 线性升到目标 PWM（淡入）；
 *  - 调光：目标 PWM 一变就以当时的 PWM 为起点重新起算，线性推到新目标（连续无跳变）；
 *  - 关灯/保护关断：不渐变，直接断（wled_set_level_now）。
 * 关键是渐变作用在【PWM 值】而非档位整数上：跨度多大都平滑，且时长固定。
 */
#include "wled.h"
#include "sw6306.h"
#include "bsp_usart.h"
#include "at32f423.h"

#define WLED_PWM_MAX 1023U      /* 与 wk_tmr1_init 的 TMR1 period=1023 一致 */

/* ==================== 温度限档查表（只负责"降档"，不负责"关闭"） ====================
 * 索引 = (T - WLED_LIMIT_T_MIN_C) / WLED_LIMIT_STEP_C（2°C 步长，越界钳位）。
 * 曲线由实测/指定锚点连成三段折线（曲线上行=更亮）：
 *   0°C  → 25（0°C 及以下才开放最大挡位）
 *  30°C  → 19
 *  40°C  → 16（实测锚点：LED 板温 ≈65°C、Tj ≈80~85°C）
 *  60°C  →  0（恰好落在关闭阈值；真正的关闭由 wled_cutoff_now() 直接比温度完成）
 * 段内线性：0~30°C 每 2°C 降 0.4 挡、30~40°C 每 2°C 降 0.6 挡、40~60°C 每 2°C 降 1.6 挡。
 * 约束：最后一格必须对应 WLED_NTC_OFF_C(60°C) 且取值为 0——改阈值/曲线时同步改表，
 * 下面两条编译期断言会拦住不一致。 */
static const uint8_t s_limit_table[] = {
/*    0    2    4    6    8   10   12   14   16   18 */
     25,  25,  24,  24,  23,  23,  22,  22,  21,  21,
/*   20   22   24   26   28   30   32   34   36   38 */
     21,  20,  20,  19,  19,  19,  18,  17,  17,  16,
/*   40   42   44   46   48   50   52   54   56   58 */
     16,  14,  13,  11,  10,   8,   7,   5,   4,   2,
/*   60 */
      0
};
#define WLED_LIMIT_TABLE_LEN   (sizeof(s_limit_table) / sizeof(s_limit_table[0]))
typedef char wled_limit_table_len_check[ \
    (WLED_LIMIT_TABLE_LEN == (((WLED_NTC_OFF_C - WLED_LIMIT_T_MIN_C) / WLED_LIMIT_STEP_C) + 1)) ? 1 : -1];
/* 表尾（= 关闭阈值 60°C 那一格）必须为 0，否则"60°C 直接关闭"与曲线自相矛盾。
 * 用枚举而不是数组下标式断言：后者会被 GCC/Clang 当成 VLA（-Wgnu-folding-constant）。 */
enum { wled_limit_table_zero_check = 1 / ((s_limit_table[WLED_LIMIT_TABLE_LEN - 1] == 0u) ? 1 : 0) };

static uint8_t  s_level     = 0;   /* 用户设定档位 0~25（0=关灯；非零钳到 4~25） */
static uint8_t  s_out       = 0;   /* 实际输出档位 = min(用户档位, 温度上限)；0=未输出 */
static uint8_t  s_limit     = WLED_BRIGHTNESS_MAX; /* 当前温度允许的最高档位（诊断用） */
static uint8_t  s_protected = 0;   /* 1=灯被保护强制关闭（零电量/过温），等待手动重开 */
static uint8_t  s_cut       = 0;   /* 1=处于过温关闭锁存：需降到 WLED_NTC_RECOVER_C 才允许重开 */

/* PWM 渐变状态：目标是"PWM 值本身的线性过渡"（不是逐档跳变）。
 * 每次目标 PWM 变化时，以当前 PWM 为起点、在 WLED_RAMP_TICKS 个 tick 内线性走到新目标；
 * 渐变途中目标再变，则从当时的 PWM 值重新起算（连续、无跳变）。 */
static uint16_t s_pwm       = 0;   /* 当前已写入 TMR1_CH2 的占空比 */
static uint16_t s_pwm_from  = 0;   /* 本段渐变的起点 PWM */
static uint16_t s_pwm_to    = 0;   /* 本段渐变的目标 PWM */
static uint16_t s_pwm_tick  = 0;   /* 本段已走过的 tick 数（0..WLED_RAMP_TICKS） */

static void wled_protect_off(void);          /* 前向声明（wled_update_output 里调用） */

/* 供电通路闸门：1=允许输出（默认）。load_task 在假插入 A1 前后用它强制排序——
 * 关闸时只冻结 PWM 渐变，不清任何状态，因此重新开闸后会从当前值继续，不会跳变。 */
static uint8_t  s_path_ok  = 1;

/* 亮度档位(0~25) → PWM(0~1023)：PWM = level²（人眼感知线性化，见 wled.h）。
 * level=0 → 关；25²=625（约占满量程 61%）。 */
static uint16_t wled_level_to_pwm(uint16_t level)
{
    uint32_t pwm = (uint32_t)level * (uint32_t)level;
    return (pwm > WLED_PWM_MAX) ? WLED_PWM_MAX : (uint16_t)pwm;
}

/* 立即输出某档位对应的 PWM（不做渐变）：
 * 只用于「关灯 / 保护关断」——这两种情况必须立刻断，不能慢慢淡出。 */
static void wled_set_level_now(uint8_t level)
{
    uint16_t pwm = wled_level_to_pwm(level);
    s_pwm       = pwm;
    s_pwm_from  = pwm;      /* 渐变状态同步到位，避免残留旧起点 */
    s_pwm_to    = pwm;
    s_pwm_tick  = WLED_RAMP_TICKS;
    tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, s_pwm);
#if WLED_DEBUG_PRINT
    /* 每次实际写入的 PWM 变化打印一行（单行 <64B）：用于热成像标记各亮度档位
     * 对应的 LED 温升，进而约束亮度范围。 */
    USART_Printf("[WLED] L%u PWM:%u\r\n", (unsigned)level, (unsigned)s_pwm);
#endif
}

void WLED_Init(void)
{
    /* 上电状态：不点灯，档位复位为 0（不做 .noinit 记忆，复位即回默认） */
    s_level     = 0;
    s_out       = 0;
    s_limit     = WLED_BRIGHTNESS_MAX;
    s_protected = 0;
    s_cut       = 0;
    s_pwm       = 0;
    s_pwm_from  = 0;
    s_pwm_to    = 0;
    s_pwm_tick  = WLED_RAMP_TICKS;
    s_path_ok   = 1;
    /* TMR1_CH2 引脚与 PWM 已由 wk_tmr1_init() 配置，此处不驱动灯（保持关闭） */
}

/* 温度镜像是否可用：开机初期 SW6306 尚未初始化，TNTC 读数为 0（摄氏），
 * 若直接按 0°C 处理会被当成"极冷"而放开 25 挡满功率，故需保守处理。
 * 判据沿用 WLED_IsZeroCapacity() 里"镜像未建立"的写法（VBAT<1000）。 */
static uint8_t wled_temp_valid(void)
{
    if(SW6306_IsInitialized() == 0) return 0;
    if(SW6306_ReadVBAT() < 1000)    return 0;
    return 1;
}

/* 温度 → 允许的最高档位（查表；不做除法/浮点，适合 10ms 节拍）
 * 本函数只负责【降档】，不负责关闭：
 *  - T ≤ 0°C       ：25（只有 0°C 及以下才开放最大挡位）
 *  - 0~30°C        ：25 → 19
 *  - 30~40°C       ：19 → 16（实测锚点）
 *  - 40~60°C       ：16 → 0（趋近关闭点）
 *  - 镜像不可用    ：保守取 WLED_LIMIT_SENSOR_FAULT_LV，避免开机即满功率
 * 60°C 及以上的【直接关闭】不走本表，见 wled_cutoff_now()。 */
static uint8_t wled_temp_limit(void)
{
    int16_t t = SW6306_ReadTNTC();
    int32_t idx;

    if(wled_temp_valid() == 0) return WLED_LIMIT_SENSOR_FAULT_LV;

    idx = ((int32_t)t - (int32_t)WLED_LIMIT_T_MIN_C) / (int32_t)WLED_LIMIT_STEP_C;
    if(idx < 0) idx = 0;
    if(idx > (int32_t)WLED_LIMIT_TABLE_LEN - 1) idx = (int32_t)WLED_LIMIT_TABLE_LEN - 1;
    return s_limit_table[idx];
}

/* NTC ≥ 60°C【直接关闭】（不是降档）——瞬时判断，与查表无关：
 * 只认 NTC：NTC 贴在散热铝壳上，代表 LED 实际热状态，60°C 关闭已经取代原先
 * 按芯片结温的 90/100°C 保护（那两个宏已删除），不再单独判芯片温度。
 * 锁存与回升：关闭后进入 s_cut 锁存，须 NTC ≤ WLED_NTC_RECOVER_C(45°C) 才解除，
 * 避免"关→立刻重开→又超温"的死循环；解除后灯仍是关的，等手动开。 */
static uint8_t wled_cutoff_now(void)
{
    int16_t tntc;

    /* 镜像未就绪（上电初期 TNTC 读 0）：不判过温，避免开机误关灯 */
    if(wled_temp_valid() == 0) return 0;

    tntc = SW6306_ReadTNTC();

    /* 阈值与表尾解耦：直接比温度，不依赖 s_limit_table 的最后一格 */
    if(tntc >= WLED_NTC_OFF_C)
    {
        s_cut = 1;
        return 1;
    }
    if(s_cut)
    {
        if(tntc <= WLED_NTC_RECOVER_C)
        {
            s_cut = 0;          /* 已冷却，解除过温锁存（灯仍保持关闭，等手动开） */
        }
        return 1;               /* 锁存期间保持关闭 */
    }
    return 0;
}

/* 保护触发：锁存并立即关断，同时把用户档位与输出档位清 0。
 * 必须清档位：否则 10ms 的 WLED_Tick 会把灯重新推回目标档，保护形同虚设。
 * 保护解除后不自动恢复点亮，由用户手动再开（WLED_On）。 */
static void wled_protect_off(void)
{
    s_protected = 1;
    s_level     = 0;
    s_out       = 0;
    wled_set_level_now(0);
}

/* 按用户档位与温度上限算出实际输出档位（s_out）。
 * 返回是否发生变化，便于降档/关灯时打点（串口标记热成像）。
 * 顺序很关键：先判 60°C【直接关闭】，再按允许上限【降档】。 */
static uint8_t wled_update_output(void)
{
    uint8_t lim;
    uint8_t out;

    /* 60°C（或芯片过温）→ 立即关闭并锁存，不是降档。
     * 只在"本次刚关闭"时打一行，否则 10ms 节拍会在整个锁存期间刷屏。 */
    if(wled_cutoff_now())
    {
        uint8_t changed = 0;
        if(s_protected == 0 || s_out != 0)
        {
            wled_protect_off();
            changed = 1;
#if WLED_DEBUG_PRINT
            USART_Printf("[WLED] OFF: over temp, Tntc:%dC\r\n", (int)SW6306_ReadTNTC());
#endif
        }
        return changed;
    }

    lim = wled_temp_limit();
    s_limit = lim;
    /* 用户关灯时输出恒 0；否则取 用户档位 与 温度上限 的较小者 */
    out = (s_level == 0) ? 0 : ((s_level < lim) ? s_level : lim);
    if(out != s_out)
    {
        s_out = out;
        return 1;
    }
    return 0;
}

/* 设置用户档位（只写目标，实际输出由 10ms 的 WLED_Tick 做温控钳位 + PWM 渐变）：
 *  - UI 调光每级都调它；档位之间不做"逐档渐变"，PWM 目标一变就直接交给 PWM 线性渐变；
 *  - level=0 是关灯，属于异常/明确关断路径：立即断，不做淡出；
 *  - 保护（过温/零电量）触发时拒绝点亮并立即关断；解除后允许手动再开。 */
void WLED_SetBrightness(uint16_t level)
{
    if(level > WLED_BRIGHTNESS_MAX) level = WLED_BRIGHTNESS_MAX;
    /* 非零亮度钳到下限（PWM 最小 16 = 4²）：调光不会把灯调灭，否则难以确认灯状态；
     * 只有显式 WLED_SetBrightness(0)（关灯）才真正输出 0 */
    if(level > 0 && level < WLED_BRIGHTNESS_MIN) level = WLED_BRIGHTNESS_MIN;
    s_level = (uint8_t)level;

    /* 关灯：立即断（不做渐变）；保护/过温锁存一并解除。
     * 解除 s_cut 不影响安全：下次点亮时 wled_cutoff_now() 仍会用当前温度重新判断。 */
    if(s_level == 0)
    {
        s_protected = 0;
        s_cut       = 0;
        s_out       = 0;
        wled_set_level_now(0);
        return;
    }

    /* 过温锁存中（60°C 关闭后未冷却）：拒绝点亮。
     * wled_cutoff_now() 在 NTC 回落到 WLED_NTC_RECOVER_C(45°C) 且芯片回落后
     * 会自行解除，届时用户双击即可重新点亮。 */
    if(wled_cutoff_now())
    {
        if(s_protected == 0)
        {
            wled_protect_off();
        }
#if WLED_DEBUG_PRINT
        USART_Printf("[WLED] OFF: over temp, wait cool, Tntc:%dC\r\n", (int)SW6306_ReadTNTC());
#endif
        return;
    }

    /* 零电量保护：拒绝点亮并立即关断 */
    if(WLED_IsZeroCapacity())
    {
        wled_protect_off();
        return;
    }

    /* 点亮/换档：清除保护状态；当前 PWM 不动，由 WLED_Tick 从当前 PWM 线性推到新目标
     * （灯原本是灭的 → s_pwm=0 → 自然从 0 缓升，即开灯淡入） */
    s_protected = 0;
    (void)wled_update_output();
}

/* PWM 线性渐变：把 s_pwm 从 s_pwm_from 在 WLED_RAMP_TICKS 个 tick 内线性推到 s_pwm_to。
 * 直接对 PWM 值插值（不是对档位），因此把每一级 PWM 都真实写出去，
 * 不存在"档位整数跳变"造成的台阶。用整数累加，无除法、无浮点。 */
static void wled_pwm_ramp(void)
{
    uint16_t want;

    if(s_pwm_tick >= WLED_RAMP_TICKS) return;    /* 已到位 */

    s_pwm_tick++;
    if(s_pwm_tick >= WLED_RAMP_TICKS)
    {
        want = s_pwm_to;                          /* 末步直接对齐目标，消除累积误差 */
    }
    else if(s_pwm_to >= s_pwm_from)
    {
        want = (uint16_t)((uint32_t)s_pwm_from
               + ((uint32_t)(s_pwm_to - s_pwm_from) * s_pwm_tick) / WLED_RAMP_TICKS);
    }
    else
    {
        want = (uint16_t)((uint32_t)s_pwm_from
               - ((uint32_t)(s_pwm_from - s_pwm_to) * s_pwm_tick) / WLED_RAMP_TICKS);
    }

    if(want != s_pwm)
    {
        s_pwm = want;
        tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, s_pwm);
#if WLED_DEBUG_PRINT
        /* 打印实际写入的 PWM 值（单行 <64B）：渐变期间几乎每 tick 都变，
         * 需要干净的热成像标记时可临时把 WLED_DEBUG_PRINT 置 0。 */
        USART_Printf("[WLED] PWM:%u\r\n", (unsigned)s_pwm);
#endif
    }
}

/* 10ms 周期调用：温度限档 + PWM 线性渐变。两处共用同一套渐变参数：
 *  - 开灯缓升：s_pwm 从 0（关灯态）线性推到目标 PWM；
 *  - 调光平滑：目标 PWM 变化后，以当前 PWM 为起点重新起算，线性推到新目标；
 *  - 关闭（含 60°C 保护）：不走这里，由 wled_set_level_now() 直接断。
 * 渐变时长 = WLED_RAMP_TICKS × 10ms（默认 160ms），与跨度无关。 */
void WLED_Tick(void)
{
    uint8_t  changed = wled_update_output();
    uint16_t target;

    if(changed)
    {
#if WLED_DEBUG_PRINT
        /* 温度限档/关灯导致输出档位变化时打点（输出变化才打印，不会刷屏）：
         * 便于配合热成像确认限档介入点与最终稳定档位。 */
        USART_Printf("[WLED] limit L%u->L%u Tntc:%dC\r\n",
                     (unsigned)s_level, (unsigned)s_out, (int)SW6306_ReadTNTC());
#endif
    }

    /* 目标 PWM：由输出档位平方映射得到；目标一变就从当前 PWM 重新起算一段渐变 */
    target = wled_level_to_pwm(s_out);
    if(target != s_pwm_to)
    {
        s_pwm_from = s_pwm;
        s_pwm_to   = target;
        s_pwm_tick = 0U;
    }

    /* 供电通路未就绪（load_task 正在插/拔假 A1）→ 冻结渐变；就绪后从当前值继续。
     * 关灯（目标 0）不受闸门限制：断电要立刻生效。 */
    if(s_path_ok == 0U && target != 0U)
    {
        return;
    }
    wled_pwm_ramp();
}

/* 供电通路闸门（load_task 假插入 A1 前后调用，用于强制"先开 A1 再出 PWM"、
 * "先停 PWM 再拔 A1"的时序）。只冻结渐变，不改档位/保护状态。 */
void WLED_SetPowerPath(uint8_t ready)
{
    s_path_ok = (ready != 0U) ? 1U : 0U;
}

uint16_t WLED_GetPwm(void)//读取当前实际 PWM（供电通路排序用：0 表示灯已完全熄灭）
{
    return s_pwm;
}

void WLED_On(void)
{
    /* 首次点亮用「亮度值中点」（不是 PWM 中点）：平方映射下 PWM = 12² = 144。
     * 若已有档位（本次运行内调过光）则沿用，同样是从当前位置缓升上去。 */
    WLED_SetBrightness(s_level ? s_level : (WLED_BRIGHTNESS_MAX / 2U));
}

void WLED_Off(void)
{
    WLED_SetBrightness(0);
}

uint16_t WLED_GetBrightness(void)//读取用户设定档位（温度限档前的目标值，用于 UI 显示/调光）
{
    return s_level;
}

uint16_t WLED_GetOutputLevel(void)//读取实际输出档位（已含温度限档；0=未输出）
{
    return s_out;
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
     * 误判 zero capacity（Cap/VBAT 均为 0）。开启期间按调用周期（500ms）判断。
     * 注：正常工况下 10ms 的 WLED_Tick 温度限档会先把功率压下来，
     *     这里是兜底（NTC 60°C / 芯片 100°C 锁存 + 零电量）。 */
    if (s_level == 0) {
        return;
    }

    /* 保护触发（60°C 过温 / 零电量）→ 锁存并强制关灯。
     * 保护解除后不自动恢复，保持关闭，由用户手动再开（WLED_On）。 */
    if (wled_cutoff_now() || WLED_IsZeroCapacity()) {
        wled_protect_off();
    }
}
