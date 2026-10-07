#include "sw6306.h"
#include <math.h>

/* initialized 默认 0（零初始化，.bss）：MCU 复位后即视为未初始化，
 * SW6306_task 会对 SW6306 执行一次 ForceOff+Init（芯片复位后重置）。
 * 芯片独立复位（PowerLoad 读回失配）、ADCLoad 数据越界、MarkUninitialized
 * 手动失能同样会清 0，从而触发重新 Init。 */
static struct SW6306_StatusTypedef SW6306_Status;//SW6306状态全局变量

static uint8_t s_pomax_target = SW6306_OUTPUT_POWER_MAX;   // 运行时最大输出功率目标（W），Init 写入、可由 SetMaxOutputPower 修改

/* IBAT 两参数校准：原始码先减 int16 零点，再乘 float 斜率。
 * 库仑计/限流寄存器没有可直接修正的零点，仍只按斜率相对名义 5mA/LSB 缩放。 */
static int16_t s_ibat_zero_raw = 0;
static float s_ibat_slope_ma_per_lsb = SW6306_IBAT_SLOPE_DEFAULT;

static float sw6306_ibat_scale_read(void)
{
    return s_ibat_slope_ma_per_lsb / SW6306_IBAT_SLOPE_NOMINAL;
}

static float sw6306_ibat_scale_set(void)
{
    return SW6306_IBAT_SLOPE_NOMINAL / s_ibat_slope_ma_per_lsb;
}


/*******************************基本操作区*************************************/
SW6306_RET SW6306_ByteWrite(SW6306_ARGS(uint16_t reg, uint8_t data))
{
    SW6306_FUNC_BEGIN;
    SW6306_Status.sendbuf[0] = data;
    SW6306_EXEC(SW6306_I2C_Transmit(SW6306_I2C_ADDR, reg, (uint8_t*)SW6306_Status.sendbuf, 1, (uint8_t*)&SW6306_Status.flag));
    SW6306_UNTIL(SW6306_Status.flag);
    SW6306_FUNC_END;
}

SW6306_RET SW6306_ByteRead(SW6306_ARGS(uint16_t reg, volatile uint8_t *data))
{
    SW6306_FUNC_BEGIN;
    SW6306_EXEC(SW6306_I2C_Receive(SW6306_I2C_ADDR, reg, (uint8_t*)data, 1, (uint8_t*)&SW6306_Status.flag));
    SW6306_UNTIL(SW6306_Status.flag);
    SW6306_FUNC_END;
}

SW6306_RET SW6306_BytesRead(SW6306_ARGS(uint16_t reg, uint8_t *pdata, uint16_t len))
{
    SW6306_FUNC_BEGIN;
    SW6306_EXEC(SW6306_I2C_Receive(SW6306_I2C_ADDR, reg, pdata, len, (uint8_t*)&SW6306_Status.flag));
    SW6306_UNTIL(SW6306_Status.flag);
    SW6306_FUNC_END;
}

                
/*SW6306寄存器组切换
/通过读取版本寄存器(0x01)的值来确定目前采用的寄存器组，并进行切换
/读出0x01时为0x00~0xFF，其他值则为0x100~0x1FF
*/
SW6306_RET SW6306_RegsetSwitch(SW6306_ARGS(uint16_t regset))
{
    SW6306_FUNC_BEGIN;
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_REV, SW6306_Status.sendbuf);
    if(regset > 0xFFU)
    {
        if(SW6306_Status.sendbuf[0] == 0x01)//需要读写高位地址但目前是低地址
            SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_WREN,0x81);
    }
    else
    {
        if(SW6306_Status.sendbuf[0] != 0x01)//需要读写低位地址但目前是高地址
            SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_LREGSET,0x00);
    }
    SW6306_FUNC_END;
}

/*SW6306修改对应寄存器的特定位，不对寄存器组作检查
/遵循读-修改-写的顺序
/将mask中为1的位按data中的对应位修改成1或0，mask中为0的位不被修改
*/
SW6306_RET SW6306_ByteModify(SW6306_ARGS(uint16_t reg, uint8_t mask, uint8_t data))
{
    SW6306_FUNC_BEGIN;
    SW6306_SPAWN_ARGS(SW6306_ByteRead, reg, SW6306_Status.sendbuf);
    SW6306_Status.sendbuf[0] = (SW6306_Status.sendbuf[0] &(~mask)) | (data & mask);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, reg, SW6306_Status.sendbuf[0]);
    SW6306_FUNC_END;
}

/******************************状态读取区**************************************/

/* 单个 ADC 通道：通道号 + 输出（原始码有效标志 + 12 位原始码）。
 * 读失败时 ok=0：调用方跳过该通道，镜像保持上一次有效值——
 * 既不写入栈垃圾、也不写 0（写 0 会被下面的边界检查判为越界而触发重初始化）。 */
typedef struct {
    uint8_t  ok;
    uint16_t raw;
} sw6306_adc_slot_t;

static void sw6306_adc_read_raw(uint8_t ch, sw6306_adc_slot_t *slot)
{
    uint16_t raw = 0U;
    SW6306_ByteWrite(SW6306_CTRG_ADC_SET, ch);
    if(SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_ADCL, (uint8_t*)&raw, 2, (uint8_t*)&SW6306_Status.flag) != I2C_OK)
    {
        slot->ok = 0U;               /* 本通道读失败（I2C 层已整笔重试过） */
        return;
    }
    slot->raw = raw & 0x0FFF;        /* 12 位有效（0x31 低 8 位 + 0x32 低 4 位），高 4 位为保留位 */
    slot->ok  = 1U;
}

SW6306_RET SW6306_ADCLoad(SW6306_NOARG)
{
    /* 原始码与工程量分开：先读齐 12 位原始码，再统一换算写入镜像（换算集中在 LOAD 阶段）。
     * 失败的通道跳过更新，保证镜像不会出现"半新半旧"或被重复换算的值。 */
    sw6306_adc_slot_t adc[7];
    uint8_t i;
    static const uint8_t ch_tab[7] = {
        SW6306_ADC_SET_VBUS, SW6306_ADC_SET_IBUS, SW6306_ADC_SET_VBAT, SW6306_ADC_SET_IBAT,
        SW6306_ADC_SET_TNTC, SW6306_ADC_SET_TCHIP, SW6306_ADC_SET_VNTC
    };
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_ADC_SET);
    for(i = 0U; i < 7U; i++)
    {
        sw6306_adc_read_raw(ch_tab[i], &adc[i]);
    }

    /* 原始码 → 工程量；IBAT 在这里完成 zero + slope 校准，镜像直接保存 mA。 */
    if(adc[0].ok) SW6306_Status.vbus  = (uint16_t)(adc[0].raw << 3);//转换BUS电压
    if(adc[1].ok) SW6306_Status.ibus  = (uint16_t)(adc[1].raw << 2);//转换BUS电流
    if(adc[2].ok) SW6306_Status.vbat  = (uint16_t)(adc[2].raw *  7);//转换BAT电压
    if(adc[3].ok)
    {
        int32_t raw = (int32_t)adc[3].raw - (int32_t)s_ibat_zero_raw;
        SW6306_Status.ibat = (raw > 0) ? (uint16_t)((float)raw * s_ibat_slope_ma_per_lsb + 0.5f) : 0U;
    }
    if(adc[4].ok) SW6306_Status.tntc  = adc[4].raw;
    if(adc[5].ok) SW6306_Status.tchip = adc[5].raw;
    if(adc[6].ok) SW6306_Status.vntc  = adc[6].raw;

    /* tntc 边界 5~24 = NTC -55~40°C：上限太低，快充/快放时电池正常发热超 40°C 会误判
     * 数据不合规 → initialized=0 → ForceOff+Init → 放电掉功率（2026-08-17/18 两次实测确认）。
     * 放宽为 5~50 = -55~170°C，仅拦截真正无效的通信数据。 */
    if(SW6306_Status.tntc > 50U    || SW6306_Status.tntc <  5U)    SW6306_Status.initialized = 0;//数据边界检查
    if(SW6306_Status.tchip > 3203U || SW6306_Status.tchip < 1500U) SW6306_Status.initialized = 0;//不合规时重新初始化
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
uint16_t SW6306_ReadVBUS(void)//读取BUS电压
{
    return SW6306_Status.vbus;
}
uint16_t SW6306_ReadIBUS(void)//读取BUS电流
{
    return SW6306_Status.ibus;
}
uint16_t SW6306_ReadVBAT(void)//读取BAT电压
{
    return SW6306_Status.vbat;
}
uint16_t SW6306_ReadIBAT(void)//读取BAT电流（mA，ADCLoad 时已完成 zero+slope 校准）
{
    return SW6306_Status.ibat;
}

SW6306_RET SW6306_SetIBATCalibration(SW6306_ARGS(int16_t zero_raw, float slope_ma_per_lsb))
{
    SW6306_FUNC_BEGIN;
    if(slope_ma_per_lsb > 0.0f)
    {
        s_ibat_zero_raw = zero_raw;
        s_ibat_slope_ma_per_lsb = slope_ma_per_lsb;
    }
    SW6306_FUNC_END;
}
int16_t SW6306_ReadTNTC(void)//读取并转换NTC温度
{
    return (SW6306_Status.tntc- 16)* 5;
}
float SW6306_ReadTCHIP(void)//读取并转换芯片温度
{
    return (float)(SW6306_Status.tchip- 1839)/ 6.82f;
}
float SW6306_ReadVNTC(void)//读取并转换NTC电压
{
    return (float)SW6306_Status.vntc* 1.1f;
}
/* 由 VNTC 与 INTC 反算 NTC 电阻（Ohm） */
float SW6306_ReadNTCResistance_Ohm(void)
{
    /* 依赖：先 SW6306_ADCLoad() 更新 vntc，先 SW6306_StatusLoad() 更新 intc */
    float v_mv = (float)SW6306_Status.vntc * 1.1f;     // 你现有 ReadVNTC() 同口径
    float i_ua = (float)SW6306_Status.intc;            // 20/40/80（uA）

    if(i_ua < 1.0f) return -1.0f;
    if(v_mv < 0.1f) return -1.0f;

    /* (mV -> V): /1000；(uA -> A): *1e-6
     * R = (v_mv/1000) / (i_ua*1e-6) = v_mv*1000 / i_ua
     */
    return (v_mv * 1000.0f) / i_ua;
}
/* 用 Beta 公式计算温度（°C） */
float SW6306_TNTC_Calc(void)
{
    float r = SW6306_ReadNTCResistance_Ohm();
    if(r <= 0.0f) return 114514;

    const float t0_k = (SW6306_NTC_T0_C + 273.15f);
    const float inv_t = (1.0f / t0_k) + (1.0f / (float)SW6306_NTC_B) * logf(r / (float)SW6306_NTC_R25_OHM);
    const float t_k = 1.0f / inv_t;

    return t_k - 273.15f;
}

/* 独立计算 API：由 VNTC/INTC 反算 Rntc 再用 Beta 公式求温度（°C）。
 * 纯计算不写句柄；无 FPU MCU 可置 SW6306_NTC_TEMP_CALC_EN=0 裁剪本函数（不链接浮点计算）。
 * 依赖：先 SW6306_ADCLoad() 更新 vntc、SW6306_StatusLoad() 更新 intc。 */
#if SW6306_NTC_TEMP_CALC_EN
float SW6306_CalcNTCTemp_C(void)
{
    return SW6306_TNTC_Calc();
}
#endif

/* 计算 NTC 温度并更新句柄（NTCTempLoad：调计算 API 缓存，供 UI/菜单读取）。
 * 数据无效时 ntc_temp 为 114514（与 TNTC_Calc 一致）；无 FPU（宏=0）时句柄保持 0。 */
SW6306_RET SW6306_NTCTempLoad(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
#if SW6306_NTC_TEMP_CALC_EN
    SW6306_Status.ntc_temp = SW6306_CalcNTCTemp_C();
#else
    SW6306_Status.ntc_temp = 0.0f;
#endif
    SW6306_FUNC_END;
}

float SW6306_ReadNTCTemp(void)//读取句柄中的 NTC 温度（°C，Beta 公式计算）
{
    return SW6306_Status.ntc_temp;
}

SW6306_RET SW6306_PortStatusLoad(SW6306_NOARG)//更新端口状态镜像寄存器(0x13,0x18,0x19,0x1C,0x1D)
{
    uint8_t t[5];                    /* 读缓冲：失败时镜像保持上一次有效值 */
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_STRG_NOLOAD);//切换低地址
    if(SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_NOLOAD, &t[0], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_SYS_STAT, &t[1], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_TYPEC, &t[2], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_TYPEA_QCIN, &t[3], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_PORT_STA, &t[4], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK)
    {
        SW6306_MARK_OFFLINE_ON_I2C_FAIL();   /* I2C 失败（已整笔重试过）：当前由开关临时屏蔽 */
        SW6306_MUTEX_GIVE;
        SW6306_FUNC_END;
    }
    SW6306_Status.noload     = t[0];//0x13
    SW6306_Status.sys_stat   = t[1];//0x18
    SW6306_Status.typec_stat = t[2];//0x19
    SW6306_Status.typea_qcin = t[3];//0x1C
    SW6306_Status.port_stat  = t[4];//0x1D
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
uint8_t SW6306_IsPortC1ON(void)
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_C1ON;
}
uint8_t SW6306_IsPortC2ON(void)
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_C2ON;
}
uint8_t SW6306_IsPortA1ON(void)
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_A1ON;
}
uint8_t SW6306_IsPortA2ON(void)
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_A2ON;
}


SW6306_RET SW6306_PowerLoad(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_STRG_VBUS_CHG);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_VBUS_CHG, &SW6306_Status.vbus_chg);//0x0E
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_QCSTAT, &SW6306_Status.qcstat);//0x0F
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_BUSILIM_CHG, &SW6306_Status.ibuslim_chg);//0x10
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_BATILIM_CHG, &SW6306_Status.ibatlim_chg);//0x11
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_TYPEA_QCIN, &SW6306_Status.typea_qcin);//0x1C
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_CTRG_PISET, &SW6306_Status.pimax_set);//0x45
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_CTRG_POSET, &SW6306_Status.pomax_set);//0x4F
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_POMAX, &SW6306_Status.pomax);//0x51
    SW6306_SPAWN_ARGS(SW6306_ByteRead, SW6306_STRG_PIMAX, &SW6306_Status.pimax);//0x52
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
uint16_t SW6306_ReadIPortLimit(void)//读取充电时端口限流实时值（单位：mA）
{
    return SW6306_Status.ibuslim_chg* 50+ 200;
}
uint16_t SW6306_ReadIBattLimit(void)//读取充电时电池限流实时值（单位：mA，已按感测电阻校准）
{
    return (uint16_t)((float)(SW6306_Status.ibatlim_chg * 100 + 100) * sw6306_ibat_scale_read());
}
uint8_t SW6306_ReadMaxOutputPower(void)//读取最大输出功率（单位：W）
{
    return SW6306_Status.pomax;
}
uint8_t SW6306_ReadMaxInputPower(void)//读取最大输入功率（单位：W）
{
    return SW6306_Status.pimax;
}
uint8_t SW6306_ReadPiMaxSet(void)//读取PISET设置值（0x45，PowerLoad后更新，单位：W）
{
    return SW6306_Status.pimax_set;
}
uint8_t SW6306_ReadPoMaxSet(void)//读取POSET设置值（0x4F，PowerLoad后更新，单位：W）
{
    return SW6306_Status.pomax_set;
}

/* 协议名缩短到 ≤5 字符，便于 160x40 屏 UI 一行显示 */
static const char * const sw6306_quickcharge_str[] = {
    "NONE",
    "QC2.0",
    "QC3.0",
    "QC3+",
    "FCP",
    "SCP",
    "PDFIX",
    "PDPPS",
    "PE1.1",
    "PE2.0",
    "VOOC1",
    "VOOC4",
    "SVOOC",
    "SFCP",
    "AFC",
    "UFCS",
    "UNK"
};
const char *SW6306_ReadProtocol(void)//当前协议读取（返回字符串地址）
{
    /* 依赖：先 SW6306_PowerLoad() 更新 qcstat 镜像 */
    uint8_t qc = (uint8_t)(SW6306_Status.qcstat & 0x0FU);

    /* 没处于快充协议就直接 NONE（避免残留值误判） */
    if(!(SW6306_Status.qcstat & SW6306_QCSTAT_PQC))
        return sw6306_quickcharge_str[0];

    switch(qc)
    {
        case SW6306_QCSTAT_QC2:    return sw6306_quickcharge_str[1];
        case SW6306_QCSTAT_QC3:    return sw6306_quickcharge_str[2];
        case SW6306_QCSTAT_QC3P:   return sw6306_quickcharge_str[3];
        case SW6306_QCSTAT_FCP:    return sw6306_quickcharge_str[4];
        case SW6306_QCSTAT_SCP:    return sw6306_quickcharge_str[5];
        case SW6306_QCSTAT_PDFIX:  return sw6306_quickcharge_str[6];
        case SW6306_QCSTAT_PDPPS:  return sw6306_quickcharge_str[7];
        case SW6306_QCSTAT_PE11:   return sw6306_quickcharge_str[8];
        case SW6306_QCSTAT_PE20:   return sw6306_quickcharge_str[9];
        case SW6306_QCSTAT_VOOC1:  return sw6306_quickcharge_str[10];
        case SW6306_QCSTAT_VOOC4:  return sw6306_quickcharge_str[11];
        case SW6306_QCSTAT_SVOOC:  return sw6306_quickcharge_str[12];
        case SW6306_QCSTAT_SFCP:   return sw6306_quickcharge_str[13];
        case SW6306_QCSTAT_AFC:    return sw6306_quickcharge_str[14];
        case SW6306_QCSTAT_UFCS:   return sw6306_quickcharge_str[15];
        default:                   return sw6306_quickcharge_str[16];
    }
}

SW6306_RET SW6306_StatusLoad(SW6306_NOARG)//将SW6306的各种状态读取到镜像寄存器(0x12,0x14,0x15,0x18,0x1A,0x2A,0x2B,0x2C)
{
    uint8_t t[8];                    /* 读缓冲：失败时镜像保持上一次有效值 */
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_STRG_MODE);//切换低地址
    if(SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_MODE, &t[0], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_DISPLAY, &t[1], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_FAULT0, &t[2], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_SYS_STAT, &t[3], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_NTC_CURR, &t[4], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_FAULT1, &t[5], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_FAULT2, &t[6], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_FAULT3, &t[7], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK)
    {
        SW6306_MARK_OFFLINE_ON_I2C_FAIL();   /* I2C 失败（已整笔重试过）：当前由开关临时屏蔽 */
        SW6306_MUTEX_GIVE;
        SW6306_FUNC_END;
    }
    SW6306_Status.mode      = t[0];//0x12
    SW6306_Status.display   = t[1];//0x14
    SW6306_Status.fault0    = t[2];//0x15
    SW6306_Status.sys_stat  = t[3];//0x18
    SW6306_Status.intc      = t[4];//0x1A
    switch(SW6306_Status.intc & SW6306_NTC_CURR_MSK)
    {
        default:
            break;
        case SW6306_NTC_CURR_20U:
            SW6306_Status.intc = 20;
            break;
        case SW6306_NTC_CURR_40U:
            SW6306_Status.intc = 40;
            break;
        case SW6306_NTC_CURR_80U:
            SW6306_Status.intc = 80;
            break;
    }
    SW6306_Status.fault1 = t[5];//0x2A
    SW6306_Status.fault2 = t[6];//0x2B
    SW6306_Status.fault3 = t[7];//0x2C
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
uint8_t SW6306_IsWLEDON(void)//SW6306 WLED是否打开
{
    return SW6306_Status.display & SW6306_STRG_DISPLAY_WLED;
}
uint8_t SW6306_IsDisplaying(void)//SW6306显示是否打开（似乎是一直有效的）
{
    return SW6306_Status.display & SW6306_STRG_DISPLAY_LED;
}
uint8_t SW6306_IsLowCurrentMode(void)//SW6306是否处于小电流模式
{
    return SW6306_Status.mode & SW6306_MODE_BLUTH;
}
uint8_t SW6306_IsMPPTCharging(void)//SW6306是否处于MPPT充电模式
{
    return SW6306_Status.mode & SW6306_MODE_MPPTCHG;
}
uint8_t SW6306_IsCharging(void)//SW6306是否正在充电
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_CHGING;
}
uint8_t SW6306_IsDischarging(void)//SW6306是否正在放电
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_DISCHGING;
}
/**********************状态查询区（实时/事件语义拆分）**************************/
/* 重要：REG0x15/0x2A/0x2B 是「历史事件」寄存器（写1清零或下次开机自动清零），
 * 只能用于判断"曾发生过什么"，不能当作实时状态。
 * 实时状态请使用 REG0x18（系统状态）与 ADC 采样值。 */

/* ============ 实时状态（REG0x18 / ADC采样） ============ */
uint8_t SW6306_IsChargeStoppedByFault(void)//REG0x18.bit7：异常导致充电关闭（实时）
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_CHGERR;
}
uint8_t SW6306_IsDischargeStoppedByFault(void)//REG0x18.bit6：异常导致放电关闭（实时）
{
    return SW6306_Status.sys_stat & SW6306_SYS_STAT_DISCHGERR;
}

/* ============ 历史事件（REG0x15/0x2A/0x2B） ============ */
uint8_t SW6306_HasUVLOEvent(void)//REG0x15.bit4：曾发生UVLO事件
{
    return !!(SW6306_Status.fault0 & SW6306_FAULT0_UVLO);
}
uint8_t SW6306_HasChargeErrorEvent(void)//REG0x15.bit3：曾发生充电异常事件
{
    return !!(SW6306_Status.fault0 & SW6306_FAULT0_CHGERR);
}
uint8_t SW6306_HasDischargeErrorEvent(void)//REG0x15.bit2：曾发生放电异常事件
{
    return !!(SW6306_Status.fault0 & SW6306_FAULT0_DISCHGERR);
}
uint8_t SW6306_HasKeyEvent(void)//REG0x15.bit1：曾发生按键事件
{
    return !!(SW6306_Status.fault0 & SW6306_FAULT0_KEY);
}
uint8_t SW6306_HasSceneEvent(void)//REG0x15.bit0：曾发生场景变化事件
{
    return !!(SW6306_Status.fault0 & SW6306_FAULT0_SCENE);
}
uint8_t SW6306_HasFullChargeEvent(void)//REG0x2B.bit5：曾发生充满事件（下次开机自动清零）
{
    return !!(SW6306_Status.fault2 & SW6306_FAULT2_FULL);
}
uint8_t SW6306_ReadEventFlags(void)//读取REG0x15原始事件值
{
    return SW6306_Status.fault0;
}
uint8_t SW6306_ReadFaultDischarge(void)//读取REG0x2A放电异常历史原因
{
    return SW6306_Status.fault1;
}
uint8_t SW6306_ReadFaultCharge(void)//读取REG0x2B充电异常历史原因
{
    return SW6306_Status.fault2;
}
uint8_t SW6306_ReadSystemStatus(void)//读取REG0x18系统实时状态
{
    return SW6306_Status.sys_stat;
}
SW6306_RET SW6306_ClearEvents(SW6306_ARGS(uint8_t events))//写1清除REG0x15已处理的事件位（W1C）
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_STRG_FAULT0);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_STRG_FAULT0, events & SW6306_FAULT0_MSK);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}


SW6306_RET SW6306_CapacityLoad(SW6306_NOARG)//更新容量与库仑计镜像寄存器(0x86~0x8A,0x99,0xA2)
{
    uint8_t t[7];                    /* 读缓冲：失败时镜像保持上一次有效值 */
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;

    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_STRG_BATLVL_DISPLAY);//切换低地址
    /* 0x99 显示电量、0xA2 学习状态、0x86/0x87 最大容量（分开读，见下）、0x88~0x8A 当前容量（3B） */
    if(SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_BATLVL_DISPLAY, &t[0], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_STRG_LEARN, &t[1], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       /* 0x86~0x87：最大容量（12 位：0x86 为低 8 位，0x87 低 4 位为高 4 位；0x87 高 4 位保留）。
        * 不能连续读 2 字节进小端 uint16（会把 0x87 完整 8 位当高位），必须单独组合。 */
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_CTRG_GAUGE_MCAPL, &t[2], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_CTRG_GAUGE_MCAPH, &t[3], 1, (uint8_t*)&SW6306_Status.flag) != I2C_OK
       /* 0x88~0x8A：当前容量（3B） */
       || SW6306_I2C_Receive(SW6306_I2C_ADDR, SW6306_CTRG_CURR_CAPL, &t[4], 3, (uint8_t*)&SW6306_Status.flag) != I2C_OK)
    {
        SW6306_MARK_OFFLINE_ON_I2C_FAIL();   /* I2C 失败（已整笔重试过）：当前由开关临时屏蔽 */
        SW6306_MUTEX_GIVE;
        SW6306_FUNC_END;
    }
    SW6306_Status.capacity   = t[0];//0x99
    SW6306_Status.learn_stat = t[1];//0xA2
    SW6306_Status.maxcap     = (uint16_t)(((uint16_t)(t[3] & 0x0F) << 8) | t[2]);
    SW6306_Status.presentcap = (uint32_t)t[4] | ((uint32_t)t[5] << 8) | ((uint32_t)t[6] << 16);

    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
         
uint8_t SW6306_ReadCapacity(void)//读取SW6306显示电量
{
    return SW6306_Status.capacity;
}
/* 注意：0x86/0x88 库仑计寄存器记录的是【能量】而非电荷容量（mAh）！
 *  - 最大能量 = maxcap × 326.2236 mWh（12bit 粗分辨率）
 *  - 当前能量 = presentcap × 0.07964 mWh（24bit 细分辨率）
 *  - 326.2236 / 0.07964 ≈ 4096，两者仅分辨率不同。
 * 例：2S1P 30Q 充满后 maxcap=65 → 65×326.22 ≈ 21203 mWh ≈ 21.2 Wh（≈7.2V×3.0Ah）。 */
float SW6306_ReadMaxEnergy_mWh(void)//读取库仑计最大能量（单位：mWh，已按感测电阻校准）
{
    return SW6306_Status.maxcap * 326.2236f * sw6306_ibat_scale_read();
}
float SW6306_ReadRemainEnergy_mWh(void)//读取库仑计当前（剩余）能量（单位：mWh，已按感测电阻校准）
{
    return SW6306_Status.presentcap * 0.07964f * sw6306_ibat_scale_read();
}
sw6306_learn_state_t SW6306_ReadLearnState(void)//读取容量学习状态（0xA2 镜像，3 态 + Unknown）
{
    /* bit5=END 在前为高位、bit6=ING 为低位：
     * 00=等待学习 01=学习中 10=完成 11=共存(不存在) */
    uint8_t raw = SW6306_Status.learn_stat;
    return (sw6306_learn_state_t)(((raw & SW6306_LEARN_END) ? 2U : 0U) |
                                  ((raw & SW6306_LEARN_ING) ? 1U : 0U));
}

/* 容量学习使能/失能：enable=1 使能 0x14E[4] LEARNEN + 清 0xA2[5] 历史完成标志（重新武装）；
 * enable=0 关闭 LEARNEN。
 * 注意：SW6306 实际学习在「触发 UVLO 后再次开始充电」时启动，使能不保证立即开始。 */
SW6306_RET SW6306_CapacityLearningSet(SW6306_ARGS(uint8_t enable))
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_GAUGE0);
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_GAUGE0, SW6306_GAUGE0_LEARNEN, enable ? SW6306_GAUGE0_LEARNEN : 0x00);
    if(enable)
    {
        /* 使能时清历史完成标志，重新武装学习 */
        SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_STRG_LEARN);
        SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_STRG_LEARN, SW6306_LEARN_END, 0x00);
    }
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/********************************操作区****************************************/
SW6306_RET SW6306_Click(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_CLICK);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CLICK, SW6306_CLICK);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_ForceOff(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_DISCHG_OFF);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_DISCHG_OFF, SW6306_PORTEVT_C1_RMV|SW6306_PORTEVT_C2_RMV|SW6306_PORTEVT_A1_RMV|SW6306_PORTEVT_A2_RMV);//关闭输出口
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_DISCHG_OFF, SW6306_DISCHG_OFF);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_Unlock(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_CLICK);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CLICK, SW6306_CLICK);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_LPSET, SW6306_LPSET_EN);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_WREN, 0x20);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_WREN, 0x40);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_WREN, 0x80);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_Unlock_Nolock(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    //SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_CLICK);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CLICK, SW6306_CLICK);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_LPSET, SW6306_LPSET_EN);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_WREN, 0x20);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_WREN, 0x40);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_WREN, 0x80);
    //SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/* 设置最大输出功率（单位W，如45/18；最大100W）。
 * 解锁并写入 POSET(0x4F)，同步更新镜像与运行时目标值。
 * 注意：新功率在下次插拔/重新协商后才完全生效。 */
SW6306_RET SW6306_SetMaxOutputPower(SW6306_ARGS(uint8_t watt))
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_NOARG(SW6306_Unlock_Nolock);
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_POSET);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_POSET, watt);
    SW6306_Status.pomax_set = watt;
    s_pomax_target = watt;
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

uint8_t SW6306_GetMaxOutputPowerSetting(void)
{
    return s_pomax_target;
}

/* 端口快充总开关（0x11F PORTQC）：0=使能快充，1=禁止快充。
 * A 口仅输出方向；C 口可选输入/输出。 */
SW6306_RET SW6306_PortFastChargeSet(SW6306_ARGS(sw6306_port_t port, sw6306_port_dir_t dir, uint8_t enable))
{
    uint8_t mask = 0;
    SW6306_FUNC_BEGIN;

    switch(port)
    {
        case SW6306_PORT_C1: mask = (dir == SW6306_PORT_DIR_IN) ? SW6306_PORTQC_NOC1IN : SW6306_PORTQC_NOC1OUT; break;
        case SW6306_PORT_C2: mask = (dir == SW6306_PORT_DIR_IN) ? SW6306_PORTQC_NOC2IN : SW6306_PORTQC_NOC2OUT; break;
        case SW6306_PORT_A1: mask = SW6306_PORTQC_NOA1; break;
        case SW6306_PORT_A2: mask = SW6306_PORTQC_NOA2; break;
        default: SW6306_FUNC_END; return;
    }

    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTQC);
    /* enable=1 使能 → 清禁止位(0)；enable=0 禁止 → 置禁止位(1) */
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PORTQC, mask, enable ? 0x00 : mask);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/* 全局协议开关（source/sink 级，非端口×协议矩阵）。
 * PD source(0x133[7])、PD sink(0x137[0])、PPS source(0x134，保持 Init 的 PPS1/3)；
 * QC/FCP/AFC/SCP/PE/SFCP/VOOC/SVOOC/UFCS 用 0x12A/0x12B/0x12C（手册 V0.3.0）。
 * 极性注意：绝大多数协议 0=使能/1=禁止（active low）；VOOC 系列相反（active HIGH，1=使能/0=禁止）。
 * 手册无独立开关的方向（QC sink、FCP sink、PE sink、SFCP sink、SVOOC sink）不操作直接返回。 */
SW6306_RET SW6306_ProtocolEnable(SW6306_ARGS(sw6306_proto_t proto, sw6306_proto_dir_t dir, uint8_t enable))
{
    uint16_t reg = 0;
    uint8_t mask = 0;
    uint8_t active_high = 0;
    SW6306_FUNC_BEGIN;

    switch(proto)
    {
        case SW6306_PROTO_PD:
            if(dir == SW6306_PROTO_DIR_SOURCE) { reg = SW6306_CTRG_PD0; mask = SW6306_PD0_NOPDSRC; }
            else { reg = SW6306_CTRG_PD4; mask = SW6306_PD4_NOPDSNK; }
            break;
        case SW6306_PROTO_PPS:
            if(dir != SW6306_PROTO_DIR_SOURCE) { SW6306_FUNC_END; return; } /* PPS 仅 source */
            /* 保持 Init 配置：PPS1/PPS3 使能（4 个 PPS 最多响应 2 个） */
            reg  = SW6306_CTRG_PD1;
            mask = SW6306_PD1_NOPPS1 | SW6306_PD1_NOPPS3;
            break;
        case SW6306_PROTO_QC:
            if(dir != SW6306_PROTO_DIR_SOURCE) { SW6306_FUNC_END; return; } /* 0x12C 无 QC sink 位 */
            reg = SW6306_CTRG_P_DPDM3;
            mask = SW6306_P_DPDM3_NOQC2 | SW6306_P_DPDM3_NOQC3 | SW6306_P_DPDM3_NOQC3P;
            break;
        case SW6306_PROTO_FCP:
            if(dir != SW6306_PROTO_DIR_SOURCE) { SW6306_FUNC_END; return; } /* 手册无 FCP sink 位 */
            reg = SW6306_CTRG_P_DPDM3; mask = SW6306_P_DPDM3_NOFCP;
            break;
        case SW6306_PROTO_AFC:
            if(dir == SW6306_PROTO_DIR_SOURCE) { reg = SW6306_CTRG_P_DPDM3; mask = SW6306_P_DPDM3_NOAFC; }
            else { reg = SW6306_CTRG_P_DPDM5; mask = SW6306_P_DPDM5_NOAFC; }
            break;
        case SW6306_PROTO_SCP:
            if(dir == SW6306_PROTO_DIR_SOURCE) { reg = SW6306_CTRG_P_DPDM3; mask = SW6306_P_DPDM3_NOSCP_HV | SW6306_P_DPDM3_NOSCP_LV; }
            else { reg = SW6306_CTRG_P_DPDM5; mask = SW6306_P_DPDM5_NOSCP; }
            break;
        case SW6306_PROTO_PE:
            if(dir != SW6306_PROTO_DIR_SOURCE) { SW6306_FUNC_END; return; } /* 手册无 PE sink 位 */
            reg = SW6306_CTRG_P_DPDM4; mask = SW6306_P_DPDM4_NOPE;
            break;
        case SW6306_PROTO_SFCP:
            if(dir != SW6306_PROTO_DIR_SOURCE) { SW6306_FUNC_END; return; } /* 手册无 SFCP sink 位 */
            reg = SW6306_CTRG_P_DPDM4; mask = SW6306_P_DPDM4_NOSFCP;
            break;
        case SW6306_PROTO_VOOC:
            /* VOOC 系列 active HIGH */
            if(dir == SW6306_PROTO_DIR_SOURCE) { reg = SW6306_CTRG_P_DPDM4; mask = SW6306_P_DPDM4_VOOC1 | SW6306_P_DPDM4_VOOC4; }
            else { reg = SW6306_CTRG_P_DPDM5; mask = SW6306_P_DPDM5_VOOC; }
            active_high = 1;
            break;
        case SW6306_PROTO_SVOOC:
            if(dir != SW6306_PROTO_DIR_SOURCE) { SW6306_FUNC_END; return; } /* 手册无 SVOOC sink 位 */
            reg = SW6306_CTRG_P_DPDM4; mask = SW6306_P_DPDM4_SVOOC;
            active_high = 1;
            break;
        case SW6306_PROTO_UFCS:
            if(dir == SW6306_PROTO_DIR_SOURCE) { reg = SW6306_CTRG_P_DPDM4; mask = SW6306_P_DPDM4_NOUFCS; }
            else { reg = SW6306_CTRG_P_DPDM5; mask = SW6306_P_DPDM5_NOUFCS; }
            break;
        default:
            SW6306_FUNC_END; return;
    }

    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, reg);
    if(active_high)
    {
        /* VOOC 系列 active HIGH：enable=1 使能 → 置位(1)；enable=0 禁止 → 清位(0) */
        SW6306_SPAWN_ARGS(SW6306_ByteModify, reg, mask, enable ? mask : 0x00);
    }
    else
    {
        /* 常规协议 active low（0=使能/1=禁止）：enable=1 使能 → 清位；enable=0 禁止 → 置位 */
        SW6306_SPAWN_ARGS(SW6306_ByteModify, reg, mask, enable ? 0x00 : mask);
    }
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/* PPS 档位开关：操作 0x134 PD1 的 NOPPS1/NOPPS3（active low：1=禁止/0=使能）。
 * 只提供 PPS1/PPS3 两个档位；PPS0/PPS2 始终不使用（Init 已禁止 NOPPS0/NOPPS2） */
SW6306_RET SW6306_PPSEnable(SW6306_ARGS(sw6306_pps_t pps, uint8_t enable))
{
    uint8_t mask;
    SW6306_FUNC_BEGIN;
    switch (pps) {
    case SW6306_PPS_1: mask = SW6306_PD1_NOPPS1; break;
    case SW6306_PPS_3: mask = SW6306_PD1_NOPPS3; break;
    default: SW6306_FUNC_END; return;
    }
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PD1);
    /* enable=1 使能 → 清禁止位(0)；enable=0 禁止 → 置禁止位(1) */
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PD1, mask, enable ? 0x00 : mask);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/* 设置最大输入功率（单位W，最大100W）：解锁并写 PISET(0x45)，同步镜像 */
SW6306_RET SW6306_SetMaxInputPower(SW6306_ARGS(uint8_t watt))
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_NOARG(SW6306_Unlock_Nolock);
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PISET);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PISET, watt);
    SW6306_Status.pimax_set = watt;
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/* 手动触发 PD/PPS 电流能力播发（写 0x2E SRCCAP：重新广播 Source Capability，使已连接对端重新协商）。
 * PPS 档位/功率设置后调用，让新能力立即生效（无需等待重新插拔） */
SW6306_RET SW6306_PPSBroadcast(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PD_CMD);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD_CMD, SW6306_PD_CMD_SRCCAP);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/* 手动触发 UFCS 电流能力播发（写 UFCS_CMD SRCCAP） */
SW6306_RET SW6306_UFCSBroadcast(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_UFCS_CMD);
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_UFCS_CMD, SW6306_UFCS_CMD_SRCCAP);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_LPSet(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_LPSET);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_LPSET, SW6306_LPSET_EN, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_PortC1Remove(SW6306_NOARG)//触发C1口拔出事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_C1_RMV);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_PortC1Insert(SW6306_NOARG)//触发C1口插入事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_C1_INS);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_PortC2Remove(SW6306_NOARG)//触发C2口拔出事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_C2_RMV);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_PortC2Insert(SW6306_NOARG)//触发C2口插入事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_C2_INS);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_PortA1Remove(SW6306_NOARG)//触发A1口拔出事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_A1_RMV);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_PortA1Insert(SW6306_NOARG)//触发A1口插入事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_A1_INS);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_PortA2Remove(SW6306_NOARG)//触发A2口拔出事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_A2_RMV);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_PortA2Insert(SW6306_NOARG)//触发A2口插入事件
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PORTEVT);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PORTEVT, SW6306_PORTEVT_A2_INS);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_WLEDSet(SW6306_ARGS(uint8_t wledstatus))
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_MODE);//切换低地址
    if(wledstatus) SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_MODE, SW6306_MODE_WLEDON, SW6306_MODE_WLEDON);
    else SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_MODE, SW6306_MODE_WLEDON, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_IO1Set(SW6306_ARGS(uint8_t io1status))//控制IO1脚电平
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_IOCTL);//切换低地址
    if(io1status) SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_IOCTL, SW6306_IO1ON, SW6306_IO1ON);
    else SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_IOCTL, SW6306_IO1ON, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/*******************************外部系统兼容***********************************/
SW6306_RET SW6306_IextEnSet(SW6306_ARGS(uint8_t status))//是否计算外部系统的电流
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_EXTSYS_STA);//切换低地址
    if(status) SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_EXTSYS_STA, SW6306_EXTSYS_STA_EN, SW6306_EXTSYS_STA_EN);
    else SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_EXTSYS_STA, SW6306_EXTSYS_STA_EN, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_IextDirSet(SW6306_ARGS(uint8_t status))//外部系统的电流方向设置，0为充电，1为放电
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_EXTSYS_STA);//切换低地址
    if(status) SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_EXTSYS_STA, SW6306_EXTSYS_STA_DIS, SW6306_EXTSYS_STA_DIS);
    else SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_EXTSYS_STA, SW6306_EXTSYS_STA_DIS, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
    
SW6306_RET SW6306_IextSet(SW6306_ARGS(uint16_t current))//外部系统的电流大小设置（单位:mA，范围：0~20475，必须是5的倍数）
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_EXTSYS_IBATL);//切换低地址
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_EXTSYS_IBATL, (current/5)&0xFFU);//低8位
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_EXTSYS_IBATH, ((current/5)>>8)&0x0FU);//高4位
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/********************************强制控制区************************************/
SW6306_RET SW6306_VbusSet(SW6306_ARGS(uint16_t voltage))//设置强制输出的电压值（单位:mV，范围：3300~27300）
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_DCHG_VBUSL);//切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_DCHG_VBUSL, (voltage/10)&0xFFU);//低8位
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_DCHG_VBUSH, (voltage/5)>>9);//高4位
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_VbusForceCtrlSet(SW6306_ARGS(uint8_t status))//设置是否强制控制输出电压
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_FORCECTL);//切换低地址
    if(status) SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_FORCECTL, SW6306_FORCECTL_VBUS, SW6306_FORCECTL_VBUS);
    else SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_FORCECTL, SW6306_FORCECTL_VBUS, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_VbatSet(SW6306_ARGS(uint16_t voltage))//设置强制浮充电压值（单位:mV，范围：3300~27300）
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_CHG_VBATL);//切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG_VBATL, (voltage/10)&0xFFU);//低8位
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG_VBATH, (voltage/5)>>9);//高4位
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_VbatForceCtrlSet(SW6306_ARGS(uint8_t status))//设置是否强制控制浮充电压
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_FORCECTL);//切换低地址
    if(status) SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_FORCECTL, SW6306_FORCECTL_VBUS, SW6306_FORCECTL_VBUS);
    else SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_FORCECTL, SW6306_FORCECTL_VBUS, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

SW6306_RET SW6306_IbusinDischargeSet(SW6306_ARGS(uint16_t current))//设置放电时的端口限流值（单位:mA，范围：200~7000）
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_DCHG_IBUS);//切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_DCHG_IBUS, (current/50)&0xFFU);//设置强制输出时的端口限流值
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_IbusinChargeSet(SW6306_ARGS(uint16_t current))//设置充电时的端口限流值（单位:mA，范围：200~7000）
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_CHG_IBUS);//切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG_IBUS, (current/50)&0xFFU);//设置充电时的端口限流值
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
SW6306_RET SW6306_IbusForceCtrlSet(SW6306_ARGS(uint8_t status))//设置是否强制控制端口限流
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_FORCECTL);//切换低地址
    if(status) SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_FORCECTL, SW6306_FORCECTL_IBUS, SW6306_FORCECTL_IBUS);
    else SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_FORCECTL, SW6306_FORCECTL_IBUS, 0x00);
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}

/********************************初始化区**************************************/
static SW6306_RET SW6306_PDSet(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    //切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PD0);
    //响应所有协议
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PD0, SW6306_PD0_MSK, 0x00);
    //不给出PPS0/2（6V/16V组）（只能同时使能两组）
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PD1, SW6306_PD1_MSK, SW6306_PD1_NOPPS0|SW6306_PD1_NOPPS2);
    //响应所有协议,PPS最低3.3V，手动设置电流
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD2, SW6306_PD2_PPS3V3|SW6306_PD2_FIXREGSET|SW6306_PD2_PPSREGSET|SW6306_PD2_REJECT);
    //使能dr vconn swap
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PD3, SW6306_PD3_MSK, SW6306_PD3_ENDRSWAP|SW6306_PD3_ENVCONNSWAP);
    //响应所有协议
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PD4, SW6306_PD4_MSK, 0x00);
    //5V Fix低8位设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD5, (SW6306_PD_5V_FIX_CURR/10U)&0xFFU);
    //9V Fix低8位设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD6, (SW6306_PD_9V_FIX_CURR/10U)&0xFFU);
    //12V Fix低8位设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD7, (SW6306_PD_12V_FIX_CURR/10U)&0xFFU);
    //15V Fix低8位设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD8, (SW6306_PD_15V_FIX_CURR/10U)&0xFFU);
    //Fix高8位设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD9, (((SW6306_PD_5V_FIX_CURR/5)>>3)&0xC0)|(((SW6306_PD_9V_FIX_CURR/5)>>5)&0x30)|(((SW6306_PD_12V_FIX_CURR/5)>>7)&0x0C)|((SW6306_PD_15V_FIX_CURR/5)>>9));
    //20V Fix低8位设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD10, (SW6306_PD_20V_FIX_CURR/10U)&0xFFU);
    //20V Fix高2位设置，PPS支持恒功率
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PD11, SW6306_PD11_MSK|0x03, SW6306_PD11_CP_PPS0|SW6306_PD11_CP_PPS1|SW6306_PD11_CP_PPS2|SW6306_PD11_CP_PPS3|((SW6306_PD_20V_FIX_CURR/5)>>9));
    //PPS0电流设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PPS0, SW6306_PPS0_ENCP|(SW6306_PD_PPS0_CURR/50U));
    //PPS1电流设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PPS1, SW6306_PPS1_ENCP|(SW6306_PD_PPS1_CURR/50U));
    //PPS2电流设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PPS2, SW6306_PPS2_ENCP|(SW6306_PD_PPS2_CURR/50U));
    //PPS3电流设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PPS3, SW6306_PPS3_ENCP|(SW6306_PD_PPS3_CURR/50U));
    //切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_PD_CMD);
    //重新广播电流能力
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PD_CMD, SW6306_PD_CMD_SRCCAP);
    SW6306_FUNC_END;
}

static SW6306_RET SW6306_UFCSSet(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    //切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_P_UFCS);
    //手动设置电流
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_P_UFCS, SW6306_P_UFCS_CURRSET_MAN, SW6306_P_UFCS_CURRSET_MAN);
    //设置各挡位电流
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_C_UFCS0, (SW6306_UFCS_5V_MAX_MA*SW6306_UFCS_CURR_STEP_MA)&SW6306_UFCS_CURR_CODE_MSK);   /* 0x12E */
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_C_UFCS1, (SW6306_UFCS_10V_MAX_MA*SW6306_UFCS_CURR_STEP_MA)&SW6306_UFCS_CURR_CODE_MSK);  /* 0x12F */
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_C_UFCS2, (SW6306_UFCS_20V_MAX_MA*SW6306_UFCS_CURR_STEP_MA)&SW6306_UFCS_CURR_CODE_MSK);  /* 0x130 */
    //切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_UFCS_CMD);
    //重新广播电流能力
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_UFCS_CMD, SW6306_UFCS_CMD_SRCCAP);
    SW6306_FUNC_END;
}

SW6306_RET SW6306_Init(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_MUTEX_TAKE;
    //切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_CLICK);
    //触发一次短按键
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CLICK, SW6306_CLICK);
    //解锁寄存器写入
    SW6306_SPAWN_NOARG(SW6306_Unlock_Nolock);
    //使能UVLO、充放电异常与场景变化中断（插拔/唤醒事件可经IRQ脚发脉冲唤醒MCU）
    //注意：硬件KEY引脚未引出，无需使能按键事件中断（SW6306_KEY_INT_EN）
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_INT_EN, SW6306_UVLO_INT_EN|SW6306_CHGERR_INT_EN|SW6306_DISCHGERR_INT_EN|SW6306_SCENE_INT_EN);
    //IRQ脚拉低10ms
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_IOCTL, SW6306_IRQ1);
    //强制控制输入输出功率和电池电流
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_FORCECTL, SW6306_FORCECTL_POUT|SW6306_FORCECTL_PIN|SW6306_FORCECTL_IBAT);
    //设置放电电池端限流值（按 IBAT 校准斜率缩放写入值）
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_DCHG_IBAT, (uint8_t)((float)SW6306_BAT_DCHG_CURR_MAX * sw6306_ibat_scale_set() / 100.0f + 0.5f));
    //输入功率设置
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_PISET, SW6306_INPUT_POWER_MAX);
    //设置充电电池端限流值（按 IBAT 校准斜率缩放写入值）
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG_IBAT, (uint8_t)((float)SW6306_BAT_CHG_CURR_MAX * sw6306_ibat_scale_set() / 100.0f + 0.5f));
    //输出功率设置（写入运行时目标，默认 SW6306_OUTPUT_POWER_MAX）
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_POSET, s_pomax_target);
    //输出功率设置
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_STRG_LEARN, SW6306_LEARN_END, 0x00);
    //切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_CTRG_DCHG4);
    //禁止放电恒温环
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_DCHG0, SW6306_DCHG0_NOCT);
    //放电UVLO 2.8V，0.3V迟滞
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_DCHG4, SW6306_DCHG4_MSK, SW6306_DCHG4_UVLOHYS_V3|SW6306_DCHG4_UVLO_2V8);
    //4.2V电池，2S
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG1, SW6306_CHG1_BTYPE_REGSET|SW6306_CHG1_BTYPE_4V2|SW6306_CHG1_SERIES_REGSET|SW6306_CHG1_2S);
    //涓流充电400mA，电压迟滞0.2V，48H超时
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG6, SW6306_CHG6_TCHG_400M|SW6306_CHG6_TCHGHYS_0V1|SW6306_CHG6_TCHG_48H);
    //充电截止电流200mA，C口5V充电电流3.3A
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_CHG7, SW6306_CHG7_MSK, SW6306_CHG7_CHGEND_200M|SW6306_CHG7_C_3A3);
    //B口5V充电电流2.3A，NTC-10°C低温保护，60°C高温保护
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG8, SW6306_CHG8_B_2A3|SW6306_CHG8_NTCUTP_M10C|SW6306_CHG8_NTCOTP_60C);
    //触发62368高低温保护时减小功率为原来的1/2，高温范围10°C（60~50°C）
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG9, SW6306_CHG9_UT_4DIV8|SW6306_CHG9_OT_2DIV4|SW6306_CHG9_TR_10C);
    //62368充电常温范围45°C
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG10, SW6306_CHG10_TN_45C);
    //62368放电常温范围60~-10°C
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CHG11, SW6306_CHG11_TR_NTC);
    //禁止充电恒温环，阈值100°C
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_CHG12, SW6306_CHG12_MSK, SW6306_CHG12_NOCT|SW6306_CHG12_CT_100C);
    //BUS端下管Rdson 5mR
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_DCDC1, SW6306_DCDC1_MSK, SW6306_DCDC1_5R);
    //充放电温度,62368功能,恒温环由寄存器控制
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_DCDC2, SW6306_DCDC2_MSK, SW6306_DCDC2_NONTC|SW6306_DCDC2_TEMP_REGSET|SW6306_DCDC2_62368_REGSET|SW6306_DCDC2_CT_REGSET);
    //禁止A口插入检测
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_INDET0, SW6306_INDET0_MSK, SW6306_INDET0_NOA1VBUS|SW6306_INDET0_NOA2VBUS);
    //禁止各端口空载（防休眠）
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_INDET1, SW6306_INDET1_MSK, SW6306_INDET1_NOC1NL|SW6306_INDET1_NOC2NL|SW6306_INDET1_NOA1NL|SW6306_INDET1_NOA2NL);
    //高压时拔出检测电流减半
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_INDET2, SW6306_INDET2_HVHALFNL|SW6306_INDET2_SPNL_MSK, SW6306_INDET2_HVHALFNL|SW6306_INDET2_SPNL_8S);
    //A1 A2端口空载电流60mA
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_INDET3, SW6306_INDET3_A1_60|SW6306_INDET3_A2_60);
    //端口快充全部使能
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_PORTQC, SW6306_PORTQC_MSK, 0x00);
    //输入快充申请的电压跟随根据电池电压设置优先级，输入快充最高申请15V电压
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_VCHG, SW6306_VCHG_MSK, SW6306_VCHG_BATT_VRSQ|SW6306_VCHG_MVRSQ_15V);
    //高压快充协议功率跟随系统设置
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_P_DPDM0, SW6306_P_DPDM0_MSK, SW6306_P_DPDM0_PSYS);
    //QC3+协议最大功率45W,QC2协议最大电压20V
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_P_DPDM1, SW6306_DPDM1_QC3P_45W|SW6306_DPDM1_QC2_20V);
    //SFCP协议最大电压12V,PE协议最大电压12V
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_P_DPDM2, SW6306_DPDM2_MSK, SW6306_DPDM2_SFCP_12V|SW6306_DPDM2_PE_12V);
    //VOOC协议使能
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_P_DPDM4, SW6306_P_DPDM4_MSK, SW6306_P_DPDM4_VOOC1|SW6306_P_DPDM4_VOOC4|SW6306_P_DPDM4_SVOOC);
    //VOOC协议使能
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_P_DPDM5, SW6306_P_DPDM5_MSK, SW6306_P_DPDM5_VOOC|SW6306_P_DPDM5_SDP2A);
    //数码管驱动电流5mA,轻载5s后关闭输出
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_DISPLAY, SW6306_CTRG_DISPLAY_MSK, SW6306_CTRG_DISPLAY_2_5M);
    //Rdc计算使能 + 无条件关闭容量学习（LEARNEN=0：data 只含 RDCEN，mask=0x90 覆盖 LEARNEN 位即清零；
    //需要容量学习时由菜单/守护程序调 SW6306_CapacityLearningSet(1) 手动开启）
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_GAUGE0, SW6306_GAUGE0_MSK, SW6306_GAUGE0_RDCEN);
    //短按键功能由寄存器决定
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_KEY0, SW6306_KEY0_MSK, SW6306_KEY0_REGSET);
    //短按键打开灯显与已经接入的输出口,长按关闭下游口,双击打开WLED
    SW6306_SPAWN_ARGS(SW6306_ByteModify, SW6306_CTRG_KEY1, SW6306_KEY1_MSK, SW6306_KEY1_DISPLAY|SW6306_KEY1_LPOFF|SW6306_KEY1_DCWLED);
    
    //PD设置
    SW6306_SPAWN_NOARG(SW6306_PDSet);
    //UFCS设置
    SW6306_SPAWN_NOARG(SW6306_UFCSSet);
    
    //切换寄存器组
    SW6306_SPAWN_ARGS(SW6306_RegsetSwitch, SW6306_STRG_FAULT0);
    //清标志位
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_STRG_FAULT0, SW6306_FAULT0_MSK);
    //软件触发输出：补发一次短按键事件（0x20=1），打开灯显与已接入的输出口。
    //初始化开头的短按键发生在KEY0/KEY1配置之前，可能按旧按键行为处理；
    //在KEY配置与清标志完成后再次触发，确保输出按新配置打开。
    SW6306_SPAWN_ARGS(SW6306_ByteWrite, SW6306_CTRG_CLICK, SW6306_CLICK);
    SW6306_Status.initialized = 1;
    SW6306_MUTEX_GIVE;
    SW6306_FUNC_END;
}
    
uint8_t SW6306_IsInitialized(void)//检测SW6306是否已初始化过，须在SW6306_PowerLoad()后执行
{
    /* 判据：initialized 标志 + PowerLoad 读回配置双重校验。
     *  - initialized==1 且读回匹配 → 正常返回 1；
     *  - initialized==1 但读回失配 → 芯片被独立复位/配置丢失，失能并返回 0（触发重新 Init）；
     *  - initialized==0（MCU 复位默认 / 手动失能 / ADCLoad 数据越界 / 曾失配）→ 一律视为
     *    需重新 Init，返回 0（MCU 复位后 SW6306 走一次重置；手动失能必须真正重新初始化）。 */
    if(SW6306_Status.initialized == 0)
        return 0;
    if((SW6306_Status.pimax_set == SW6306_INPUT_POWER_MAX)&&(SW6306_Status.pomax_set == s_pomax_target))
        return 1;
    SW6306_Status.initialized = 0;
    return 0;
}

/* 手动失能已初始化标志（供 UI 调用强制重新初始化）。
 * 置 0 后，下次 SW6306_task 的 IsInitialized 一律判定为失配（返回 0），
 * 触发 ForceOff+Init 重新初始化 SW6306。 */
SW6306_RET SW6306_MarkUninitialized(SW6306_NOARG)
{
    SW6306_FUNC_BEGIN;
    SW6306_Status.initialized = 0;
    SW6306_FUNC_END;
}
