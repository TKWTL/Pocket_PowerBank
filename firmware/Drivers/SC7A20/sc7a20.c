#include "sc7a20.h"
#include <math.h>

/* 寄存器Dump打印依赖工程日志接口（仅SC7A20_RegisterDump使用） */
#include "bsp_usart.h"

/* ========================================================================== */
/* 内部可调参数                                                               */
/* ========================================================================== */
#ifndef SC7A20_VIB_WINDOW_MS
#define SC7A20_VIB_WINDOW_MS          1000U          //软件振动RMS窗口长度（ms），按当前ODR换算样本数
#endif
#ifndef SC7A20_VIB_ALPHA
#define SC7A20_VIB_ALPHA              0.05f          //低频/DC分量估计的EMA系数（越大跟随越快）
#endif

/* ========================================================================== */
/* 私有类型与状态                                                             */
/* ========================================================================== */
static struct SC7A20_StatusTypedef SC7A20_Status;//SC7A20状态全局变量

/* 三轴校准都作用在右对齐后的有效原始码上：
 *   axis_mg = (axis_raw - zero_raw) * slope_mg_per_lsb
 * 默认配置为 ±2g / HR，因此名义斜率均为 1mg/LSB。 */
static int16_t s_x_zero_raw = 0;
static int16_t s_y_zero_raw = 0;
static int16_t s_z_zero_raw = 0;
static float s_x_slope_mg_per_lsb = 1.0f;
static float s_y_slope_mg_per_lsb = 1.0f;
static float s_z_slope_mg_per_lsb = 1.0f;

/* 各量程灵敏度（单位：mg/digit）
 * HR(12bit) 模式：SC7A20_VERIFIED（官方数据手册明确给出 1/2/4/8 mg/digit）
 * 普通(10bit) 模式：COMPAT_INFERRED（取自LIS2DH12：4/8/16/48），SC7A20资料未给出，TODO_HW_VERIFY */
static const float sc7a20_sens_hr_mg[4]      = {1.0f, 2.0f, 4.0f, 8.0f};
static const float sc7a20_sens_normal_mg[4]  = {4.0f, 8.0f, 16.0f, 48.0f};

/* 当前量程对应的阈值LSB（单位：mg/LSB，用于THS换算）
 * SC7A20_VERIFIED（数据手册：INT1_THS 1LSB=16mg@2G / 32@4G / 64@8G / 128@16G） */
static const uint16_t sc7a20_ths_lsb_mg[4]   = {16U, 32U, 64U, 128U};

/* ========================================================================== */
/* 私有 helper                                                                */
/* ========================================================================== */

/* 当前ODR对应的采样频率（Hz）；Power-down(0000)或未定义ODR码时按100Hz近似，避免除零 */
static uint16_t sc7a20_odr_hz(void)
{
    uint8_t code = (uint8_t)((SC7A20_Status.ctrl1 & SC7A20_CTRL1_ODR_MSK) >> 4);
    static const uint16_t odr_tbl[10] = {0U, 1U, 10U, 25U, 50U, 100U, 200U, 400U, 1600U, 1250U};
    uint16_t hz = (code < 10U) ? odr_tbl[code] : 0U;
    return (hz == 0U) ? 100U : hz;
}

/* 原始16位数据（左对齐补码）右对齐到有效位（HR=12bit：>>4；普通=10bit：>>6）
 * COMPAT_INFERRED: OUT寄存器左对齐与LIS2DH一致，SC7A20资料未明示，TODO_HW_VERIFY */
static int16_t sc7a20_raw_to_axis(int16_t raw, uint8_t hr)
{
    return (hr != 0) ? (int16_t)(raw >> 4) : (int16_t)(raw >> 6);
}

/* 当前量程与HR模式对应的灵敏度（mg/digit） */
static float sc7a20_sensitivity_mg(void)
{
    uint8_t fs = (uint8_t)((SC7A20_Status.ctrl4 & SC7A20_CTRL4_FS_MSK) >> 4) & 0x03U;
    if(SC7A20_Status.ctrl4 & SC7A20_CTRL4_HR)
        return sc7a20_sens_hr_mg[fs];                 //SC7A20_VERIFIED
    return sc7a20_sens_normal_mg[fs];                 //COMPAT_INFERRED
}

/* mg → THS寄存器值（按当前量程LSB换算，限幅0~127） */
static uint8_t sc7a20_ths_from_mg(uint16_t mg)
{
    uint8_t fs = (uint8_t)((SC7A20_Status.ctrl4 & SC7A20_CTRL4_FS_MSK) >> 4) & 0x03U;
    uint32_t ths = (uint32_t)mg / sc7a20_ths_lsb_mg[fs];
    if(ths > 127U) ths = 127U;
    return (uint8_t)ths;
}

/* ms → DURATION寄存器值（以当前ODR为时钟，1个计数值=1000/ODR_Hz ms）
 * SC7A20_VERIFIED: 数据手册明确"持续时间以ODR为时钟" */
static uint8_t sc7a20_duration_from_ms(uint16_t ms)
{
    uint16_t hz = sc7a20_odr_hz();
    uint32_t n = ((uint32_t)ms * hz + 999U) / 1000U;
    if(n > 127U) n = 127U;
    return (uint8_t)n;
}

/* 中断占用者槽位（1=INT1，2=INT2） */
static uint8_t *sc7a20_owner_slot(uint8_t line)
{
    return (line == 2U) ? &SC7A20_Status.int2_owner : &SC7A20_Status.int1_owner;
}

/* 尝试占用中断线；被其他功能占用时返回0并置ERR_INT_BUSY */
static uint8_t sc7a20_int_claim(uint8_t line, uint8_t owner)
{
    uint8_t *slot = sc7a20_owner_slot(line);
    if(*slot != SC7A20_INT_OWNER_NONE && *slot != owner)
    {
        SC7A20_Status.last_error = SC7A20_ERR_INT_BUSY;
        return 0;
    }
    *slot = owner;
    SC7A20_Status.last_error = SC7A20_ERR_NONE;
    return 1;
}

/* 释放中断占用（仅当占用者匹配时） */
static void sc7a20_int_release(uint8_t line, uint8_t owner)
{
    uint8_t *slot = sc7a20_owner_slot(line);
    if(*slot == owner)
        *slot = SC7A20_INT_OWNER_NONE;
}


/*******************************基本操作区*************************************/
SC7A20_RET SC7A20_ByteWrite(SC7A20_ARGS(uint8_t reg, uint8_t data))
{
    SC7A20_FUNC_BEGIN;
    SC7A20_Status.sendbuf[0] = data;
    SC7A20_EXEC(SC7A20_I2C_Transmit(SC7A20_I2C_ADDR, reg, (uint8_t*)SC7A20_Status.sendbuf, 1, (uint8_t*)&SC7A20_Status.flag));
    SC7A20_UNTIL(SC7A20_Status.flag);
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_ByteRead(SC7A20_ARGS(uint8_t reg, volatile uint8_t *data))
{
    SC7A20_FUNC_BEGIN;
    SC7A20_EXEC(SC7A20_I2C_Receive(SC7A20_I2C_ADDR, reg, (uint8_t*)data, 1, (uint8_t*)&SC7A20_Status.flag));
    SC7A20_UNTIL(SC7A20_Status.flag);
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_BytesRead(SC7A20_ARGS(uint8_t reg, uint8_t *pdata, uint16_t len))
{
    SC7A20_FUNC_BEGIN;
    SC7A20_EXEC(SC7A20_I2C_Receive(SC7A20_I2C_ADDR, reg, pdata, len, (uint8_t*)&SC7A20_Status.flag));
    SC7A20_UNTIL(SC7A20_Status.flag);
    SC7A20_FUNC_END;
}

/*SC7A20修改对应寄存器的特定位
/遵循读-修改-写的顺序
/将mask中为1的位按data中的对应位修改成1或0，mask中为0的位不被修改
*/
SC7A20_RET SC7A20_ByteModify(SC7A20_ARGS(uint8_t reg, uint8_t mask, uint8_t data))
{
    SC7A20_FUNC_BEGIN;
    SC7A20_SPAWN_ARGS(SC7A20_ByteRead, reg, SC7A20_Status.sendbuf);
    SC7A20_Status.sendbuf[0] = (SC7A20_Status.sendbuf[0] & (~mask)) | (data & mask);
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, reg, SC7A20_Status.sendbuf[0]);
    SC7A20_FUNC_END;
}

/******************************加速度数据操作区********************************/
/*读取三轴加速度原始数据
/一次性读取0x28~0x2D共6字节（X_L,X_H,Y_L,Y_H,Z_L,Z_H），小端序
/数据为2的补码，HR模式12bit有效（左对齐）
*/
SC7A20_RET SC7A20_AccelLoad(SC7A20_NOARG)
{
    uint8_t t[6];                    /* 读缓冲：失败时镜像保持上一次有效值（不写入半帧数据） */
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    /* 一次连读 0x28~0x2D（X_L,X_H,Y_L,Y_H,Z_L,Z_H），小端序，2的补码。
     * 注意：起始地址必须置 MSB=1（SC7A20_STRG_OUT_AUTO=0xA8）才能触发
     *       芯片地址自动递增的连续读；若用 0x28（MSB=0），每字节都返回
     *       同一寄存器值 → 三轴数据被复制成相同值（实测坑，勿改回）。 */
    if(SC7A20_I2C_Receive(SC7A20_I2C_ADDR, SC7A20_STRG_OUT_AUTO, t, 6, (uint8_t*)&SC7A20_Status.flag) != I2C_OK)
    {
        SC7A20_MARK_OFFLINE_ON_I2C_FAIL();   /* 通信失败：清 initialized → 下轮 load_task 重新初始化 */
        SC7A20_MUTEX_GIVE;
        SC7A20_FUNC_END;
    }
    SC7A20_Status.x = (int16_t)(((uint16_t)t[1] << 8) | t[0]);
    SC7A20_Status.y = (int16_t)(((uint16_t)t[3] << 8) | t[2]);
    SC7A20_Status.z = (int16_t)(((uint16_t)t[5] << 8) | t[4]);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

int16_t SC7A20_ReadX(void)//读取X轴原始数据
{
    return SC7A20_Status.x;
}
int16_t SC7A20_ReadY(void)//读取Y轴原始数据
{
    return SC7A20_Status.y;
}
int16_t SC7A20_ReadZ(void)//读取Z轴原始数据
{
    return SC7A20_Status.z;
}

void SC7A20_ReadRawCode(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t hr = (SC7A20_Status.ctrl4 & SC7A20_CTRL4_HR) ? 1U : 0U;

    if (x) *x = sc7a20_raw_to_axis(SC7A20_Status.x, hr);
    if (y) *y = sc7a20_raw_to_axis(SC7A20_Status.y, hr);
    if (z) *z = sc7a20_raw_to_axis(SC7A20_Status.z, hr);
}

/*读取X轴加速度（单位：mg）
/原始数据左对齐补码 → 右对齐到有效位 → int16零点修正 → float斜率
*/
float SC7A20_ReadX_mg(void)
{
    uint8_t hr = (SC7A20_Status.ctrl4 & SC7A20_CTRL4_HR) ? 1 : 0;
    int16_t raw = sc7a20_raw_to_axis(SC7A20_Status.x, hr);
    return (float)((int32_t)raw - (int32_t)s_x_zero_raw) * s_x_slope_mg_per_lsb;
}

void SC7A20_SetXCalibration(int16_t zero_raw, float slope_mg_per_lsb)
{
    if(slope_mg_per_lsb > 0.0f)
    {
        s_x_zero_raw = zero_raw;
        s_x_slope_mg_per_lsb = slope_mg_per_lsb;
    }
}

float SC7A20_ReadY_mg(void)//读取Y轴加速度（单位：mg）
{
    uint8_t hr = (SC7A20_Status.ctrl4 & SC7A20_CTRL4_HR) ? 1 : 0;
    int16_t raw = sc7a20_raw_to_axis(SC7A20_Status.y, hr);
    return (float)((int32_t)raw - (int32_t)s_y_zero_raw) * s_y_slope_mg_per_lsb;
}

void SC7A20_SetYCalibration(int16_t zero_raw, float slope_mg_per_lsb)
{
    if(slope_mg_per_lsb > 0.0f)
    {
        s_y_zero_raw = zero_raw;
        s_y_slope_mg_per_lsb = slope_mg_per_lsb;
    }
}

float SC7A20_ReadZ_mg(void)//读取Z轴加速度（单位：mg）
{
    uint8_t hr = (SC7A20_Status.ctrl4 & SC7A20_CTRL4_HR) ? 1 : 0;
    int16_t raw = sc7a20_raw_to_axis(SC7A20_Status.z, hr);
    return (float)((int32_t)raw - (int32_t)s_z_zero_raw) * s_z_slope_mg_per_lsb;
}

void SC7A20_SetZCalibration(int16_t zero_raw, float slope_mg_per_lsb)
{
    if(slope_mg_per_lsb > 0.0f)
    {
        s_z_zero_raw = zero_raw;
        s_z_slope_mg_per_lsb = slope_mg_per_lsb;
    }
}

/******************************温度操作区**************************************/
/*读取温度原始数据
/0x0C为低8位，0x0D为高4位（12位补码）
/注意：需要先通过TEMP_CFG寄存器使能内部温度ADC（胚胎阶段暂未在Init中使能）
*/
SC7A20_RET SC7A20_TempLoad(SC7A20_NOARG)
{
    uint8_t t[2];                    /* 读缓冲：失败时不改动镜像 */
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    if(SC7A20_I2C_Receive(SC7A20_I2C_ADDR, SC7A20_STRG_OUT_TEMP_L, t, 2, (uint8_t*)&SC7A20_Status.flag) != I2C_OK)
    {
        SC7A20_MARK_OFFLINE_ON_I2C_FAIL();   /* 通信失败：清 initialized → 下轮 load_task 重新初始化 */
        SC7A20_MUTEX_GIVE;
        SC7A20_FUNC_END;
    }
    SC7A20_Status.temp = (int16_t)(((uint16_t)t[1] << 8) | t[0]);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

float SC7A20_ReadTemp(void)//读取温度（单位：°C）
{
    //TODO: 12位温度数据转换为°C的公式需要按芯片实测标定
    return (float)SC7A20_Status.temp;
}

/******************************状态操作区**************************************/
SC7A20_RET SC7A20_StatusLoad(SC7A20_NOARG)//读取状态寄存器镜像（0x27）
{
    uint8_t st = 0U;                 /* 读缓冲：失败时不改动镜像 */
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    if(SC7A20_I2C_Receive(SC7A20_I2C_ADDR, SC7A20_STRG_STATUS, &st, 1, (uint8_t*)&SC7A20_Status.flag) != I2C_OK)
    {
        SC7A20_MARK_OFFLINE_ON_I2C_FAIL();   /* 通信失败：清 initialized → 下轮 load_task 重新初始化 */
        SC7A20_MUTEX_GIVE;
        SC7A20_FUNC_END;
    }
    SC7A20_Status.status = st;
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

uint8_t SC7A20_IsDataReady(void)//三轴新数据全部就绪（ZYXDA）
{
    return SC7A20_Status.status & SC7A20_STATUS_ZYXDA;
}

uint8_t SC7A20_HasOverrun(void)//有数据覆盖（ZYXOR）
{
    return SC7A20_Status.status & SC7A20_STATUS_ZYXOR;
}

uint8_t SC7A20_ReadStatus(void)//读取状态寄存器原始值
{
    return SC7A20_Status.status;
}

/******************************配置操作区**************************************/
SC7A20_RET SC7A20_SetODR(SC7A20_ARGS(uint8_t odr))//设置输出数据率
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL1, SC7A20_CTRL1_ODR_MSK, odr & SC7A20_CTRL1_ODR_MSK);
    SC7A20_Status.ctrl1 = (SC7A20_Status.ctrl1 & ~SC7A20_CTRL1_ODR_MSK) | (odr & SC7A20_CTRL1_ODR_MSK);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_SetFullScale(SC7A20_ARGS(uint8_t fs))//设置量程
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL4, SC7A20_CTRL4_FS_MSK, fs & SC7A20_CTRL4_FS_MSK);
    SC7A20_Status.ctrl4 = (SC7A20_Status.ctrl4 & ~SC7A20_CTRL4_FS_MSK) | (fs & SC7A20_CTRL4_FS_MSK);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_SetPowerMode(SC7A20_ARGS(uint8_t lpen))//设置低功耗模式
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL1, SC7A20_CTRL1_LPEN, lpen ? SC7A20_CTRL1_LPEN : 0x00U);
    SC7A20_Status.ctrl1 = (SC7A20_Status.ctrl1 & ~SC7A20_CTRL1_LPEN) | (lpen ? SC7A20_CTRL1_LPEN : 0x00U);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_EnableAxis(SC7A20_ARGS(uint8_t axis))//设置轴使能
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL1, SC7A20_CTRL1_ZEN|SC7A20_CTRL1_YEN|SC7A20_CTRL1_XEN, axis & (SC7A20_CTRL1_ZEN|SC7A20_CTRL1_YEN|SC7A20_CTRL1_XEN));
    SC7A20_Status.ctrl1 = (SC7A20_Status.ctrl1 & ~(SC7A20_CTRL1_ZEN|SC7A20_CTRL1_YEN|SC7A20_CTRL1_XEN)) | (axis & (SC7A20_CTRL1_ZEN|SC7A20_CTRL1_YEN|SC7A20_CTRL1_XEN));
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_BDUSet(SC7A20_ARGS(uint8_t enable))//块数据更新使能
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL4, SC7A20_CTRL4_BDU, enable ? SC7A20_CTRL4_BDU : 0x00U);
    SC7A20_Status.ctrl4 = (SC7A20_Status.ctrl4 & ~SC7A20_CTRL4_BDU) | (enable ? SC7A20_CTRL4_BDU : 0x00U);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_HighResSet(SC7A20_ARGS(uint8_t enable))//高精度输出使能（1=12bit）
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL4, SC7A20_CTRL4_HR, enable ? SC7A20_CTRL4_HR : 0x00U);
    SC7A20_Status.ctrl4 = (SC7A20_Status.ctrl4 & ~SC7A20_CTRL4_HR) | (enable ? SC7A20_CTRL4_HR : 0x00U);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

/******************************中断操作区**************************************/
SC7A20_RET SC7A20_Int1Config(SC7A20_ARGS(uint8_t cfg))//配置中断1
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_INT1_CFG, cfg);
    SC7A20_Status.int1_cfg = cfg;
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_Int1ThresholdSet(SC7A20_ARGS(uint8_t ths))//设置中断1阈值
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_INT1_THS, ths & SC7A20_INT_THS_MSK);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_Int1DurationSet(SC7A20_ARGS(uint8_t dur))//设置中断1持续时间
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_INT1_DURATION, dur & SC7A20_INT_DUR_MSK);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_ReadInt1Source(SC7A20_NOARG)//读取中断1状态镜像（读后清除锁存）
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteRead, SC7A20_STRG_INT1_SOURCE, &SC7A20_Status.int1_source);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

uint8_t SC7A20_IsInt1Active(void)//中断1激活（IA）
{
    return SC7A20_Status.int1_source & SC7A20_INT_SRC_IA;
}

SC7A20_RET SC7A20_Int2Config(SC7A20_ARGS(uint8_t cfg))//配置中断2
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_INT2_CFG, cfg);
    SC7A20_Status.int2_cfg = cfg;
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_Int2ThresholdSet(SC7A20_ARGS(uint8_t ths))//设置中断2阈值
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_INT2_THS, ths & SC7A20_INT_THS_MSK);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_Int2DurationSet(SC7A20_ARGS(uint8_t dur))//设置中断2持续时间
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_INT2_DURATION, dur & SC7A20_INT_DUR_MSK);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

SC7A20_RET SC7A20_ReadInt2Source(SC7A20_NOARG)//读取中断2状态镜像
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteRead, SC7A20_STRG_INT2_SOURCE, &SC7A20_Status.int2_source);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

uint8_t SC7A20_IsInt2Active(void)//中断2激活（IA）
{
    return SC7A20_Status.int2_source & SC7A20_INT_SRC_IA;
}

/*设置INT1/INT2中断路由
/ctrl3为0x22（INT1上输出哪些中断），ctrl6为0x25（INT2上输出哪些中断）
*/
SC7A20_RET SC7A20_IntRouteSet(SC7A20_ARGS(uint8_t ctrl3, uint8_t ctrl6))
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_CTRL3, ctrl3);
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_CTRL6, ctrl6);
    SC7A20_Status.ctrl3 = ctrl3;
    SC7A20_Status.ctrl6 = ctrl6;
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

/******************************自测试操作区************************************/
SC7A20_RET SC7A20_SelfTest(SC7A20_ARGS(uint8_t st))//设置自测试模式
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL4, SC7A20_CTRL4_ST_MSK, st & SC7A20_CTRL4_ST_MSK);
    SC7A20_Status.ctrl4 = (SC7A20_Status.ctrl4 & ~SC7A20_CTRL4_ST_MSK) | (st & SC7A20_CTRL4_ST_MSK);
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

/*******************************低功耗设置区***********************************/
/*SC7A20低功耗设置（进入Power-down + 保持INT极性正确）
 * ① 修正INT极性：H_LACTIVE=1（低有效触发）。
 *    SC7A20上电默认H_LACTIVE=0（高有效），且INT1为推挽输出（非开漏），
 *    无中断事件时会把INT1主动驱动为低电平。
 *    若INT1与SW6306的IRQ脚共用同一条EXINT线（本工程GPIOB8/EXINT8），
 *    这个默认低电平会掩盖SW6306的低电平唤醒脉冲，导致MCU无法被SW6306唤醒。
 *    改为低有效后，无事件时INT1输出高电平，不干扰共享线。
 * ② 进入Power-down：ODR=0000（CTRL_REG1=0x00），电流约0.5uA，整机休眠功耗最低。
 * 注意：本函数只做低功耗设置，不校验在线、不改变initialized状态（可重复调用）；
 *       供休眠策略进 DeepSleep 前调用；唤醒恢复运行时再调用 SC7A20_Init()
 *       或 SC7A20_SetODR()/SC7A20_SetPowerMode() 恢复输出。
 */
SC7A20_RET SC7A20_LowPowerSet(SC7A20_NOARG)
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    //① 修正INT极性：H_LACTIVE=1（低有效），无事件时INT1输出高电平
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_H_LACTIVE, SC7A20_CTRL6_H_LACTIVE);
    //② 进入Power-down模式：ODR=0000（CTRL_REG1=0x00）
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_CTRL1, 0x00U);
    SC7A20_Status.ctrl6 = SC7A20_CTRL6_H_LACTIVE;
    SC7A20_Status.ctrl1 = 0x00U;
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

/*******************************初始化区***************************************/
SC7A20_RET SC7A20_Init(SC7A20_NOARG)
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    //读取WHO_AM_I校验芯片在线（应为0x11）
    SC7A20_SPAWN_ARGS(SC7A20_ByteRead, SC7A20_STRG_WHO_AM_I, SC7A20_Status.sendbuf);
    if(SC7A20_Status.sendbuf[0] == SC7A20_WHO_AM_I_VALUE)
    {
        //① 修正INT极性：H_LACTIVE=1（低有效），无事件时INT1输出高电平。
        //   上电默认H_LACTIVE=0（高有效）且INT1推挽输出，无事件时主动拉低，
        //   与SW6306 IRQ共用EXINT8(GPIOB8)时会掩盖SW6306的低电平唤醒脉冲。
        SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_H_LACTIVE, SC7A20_CTRL6_H_LACTIVE);
        SC7A20_Status.ctrl6 = SC7A20_CTRL6_H_LACTIVE;
        //控制寄存器4：BDU + 量程 + 高精度（其余位保持默认）
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_CTRL4,
                          (SC7A20_DEFAULT_BDU ? SC7A20_CTRL4_BDU : 0x00U) |
                          (SC7A20_DEFAULT_FULLSCALE & SC7A20_CTRL4_FS_MSK) |
                          (SC7A20_DEFAULT_HIGHRES ? SC7A20_CTRL4_HR : 0x00U));
        //控制寄存器1：ODR（当前默认10Hz，供低速姿态检测）+ 轴使能
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_CTRL1,
                          (SC7A20_DEFAULT_ODR & SC7A20_CTRL1_ODR_MSK) |
                          (SC7A20_DEFAULT_AXIS_EN & (SC7A20_CTRL1_ZEN|SC7A20_CTRL1_YEN|SC7A20_CTRL1_XEN)));
        //保存设置存档
        SC7A20_Status.ctrl4 = (SC7A20_DEFAULT_BDU ? SC7A20_CTRL4_BDU : 0x00U) | (SC7A20_DEFAULT_FULLSCALE & SC7A20_CTRL4_FS_MSK) | (SC7A20_DEFAULT_HIGHRES ? SC7A20_CTRL4_HR : 0x00U);
        SC7A20_Status.ctrl1 = (SC7A20_DEFAULT_ODR & SC7A20_CTRL1_ODR_MSK) | (SC7A20_DEFAULT_AXIS_EN & (SC7A20_CTRL1_ZEN|SC7A20_CTRL1_YEN|SC7A20_CTRL1_XEN));
        /* 初始化软件振动窗口（按当前ODR换算）与运动状态 */
        SC7A20_SPAWN_NOARG(SC7A20_VibrationReset);
        SC7A20_Status.motion_state = SC7A20_MOTION_STATE_UNKNOWN;
        SC7A20_Status.initialized = 1;
    }
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

uint8_t SC7A20_IsInitialized(void)//检测SC7A20是否已初始化过（通信失败时会返回 0）
{
    return SC7A20_Status.initialized;
}

/******************************错误查询区**************************************/
uint8_t SC7A20_GetLastError(void)//读取最近一次配置/使能操作错误码（sc7a20_err_t）
{
    return SC7A20_Status.last_error;
}

/******************************自由跌落检测区**********************************/
/*配置自由跌落检测（阈值/时长/锁存/输出线）
 * 调用前提：已SC7A20_Init()或已设置ODR（阈值/时长按当前量程与ODR换算）。
 * 阈值与时长在配置时写入THS/DUR寄存器，事件逻辑在Enable时写入。
 */
SC7A20_RET SC7A20_FreefallConfig(SC7A20_ARGS(const sc7a20_freefall_config_t *cfg))
{
    uint8_t line;
    SC7A20_FUNC_BEGIN;
    if(cfg == 0 || (cfg->int_line != 1U && cfg->int_line != 2U))
    {
        SC7A20_Status.last_error = SC7A20_ERR_BAD_PARAM;
    }
    else
    {
        line = cfg->int_line;
        SC7A20_Status.freefall_cfg = *cfg;   //存档（后续Enable/GetStatus使用）
        SC7A20_MUTEX_TAKE;
        /* 阈值：mg → THS（按当前量程LSB，SC7A20_VERIFIED） */
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_THS : SC7A20_CTRG_INT1_THS,
                          sc7a20_ths_from_mg(cfg->threshold_mg));
        /* 时长：ms → DURATION（以当前ODR为时钟，SC7A20_VERIFIED） */
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_DURATION : SC7A20_CTRG_INT1_DURATION,
                          sc7a20_duration_from_ms(cfg->duration_ms));
        /* 锁存：CTRL5.LIR_INT1/2（SC7A20_VERIFIED位定义） */
        SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL5,
                          (line == 2U) ? SC7A20_CTRL5_LIR_INT2 : SC7A20_CTRL5_LIR_INT1,
                          (cfg->latch) ? ((line == 2U) ? SC7A20_CTRL5_LIR_INT2 : SC7A20_CTRL5_LIR_INT1) : 0x00U);
        SC7A20_MUTEX_GIVE;
        SC7A20_Status.last_error = SC7A20_ERR_NONE;
    }
    SC7A20_FUNC_END;
}

/*使能自由跌落检测
 * INT_CFG = AND + 三轴低事件（AOI=1, ZLIE|YLIE|XLIE）= 0x95（bring-up默认，需实测调整）
 * 自由跌落 = |X|<阈值 AND |Y|<阈值 AND |Z|<阈值 持续DURATION后触发。
 * 若目标INT线已被其他功能占用（owner机制），返回ERR_INT_BUSY且不写入。
 */
SC7A20_RET SC7A20_FreefallEnable(SC7A20_NOARG)
{
    uint8_t line;
    SC7A20_FUNC_BEGIN;
    line = SC7A20_Status.freefall_cfg.int_line;
    if(SC7A20_Status.initialized == 0)
    {
        SC7A20_Status.last_error = SC7A20_ERR_NOT_ONLINE;
    }
    else if(line == 0U)
    {
        SC7A20_Status.last_error = SC7A20_ERR_BAD_PARAM;   //未先FreefallConfig
    }
    else if(sc7a20_int_claim(line, SC7A20_INT_OWNER_FREEFALL) == 0)
    {
        /* 占用失败，错误码已设置 */
    }
    else
    {
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_CFG : SC7A20_CTRG_INT1_CFG,
                          SC7A20_INTMODE_AND | SC7A20_INT_CFG_ZLIE | SC7A20_INT_CFG_YLIE | SC7A20_INT_CFG_XLIE);
        /* 路由：AOI1 → INT1（CTRL3.I1_AOI1）或 INT2（CTRL6.I2_INT1，COMPAT_INFERRED） */
        if(line == 2U)
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_I2_INT1, SC7A20_CTRL6_I2_INT1);
        else
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL3, SC7A20_CTRL3_I1_AOI1, SC7A20_CTRL3_I1_AOI1);
        /* 读取INT_SRC清除可能残留事件（初始化时序：配置后清理） */
        SC7A20_SPAWN_ARGS(SC7A20_ByteRead, (line == 2U) ? SC7A20_STRG_INT2_SOURCE : SC7A20_STRG_INT1_SOURCE,
                          (line == 2U) ? (volatile uint8_t *)&SC7A20_Status.int2_source : (volatile uint8_t *)&SC7A20_Status.int1_source);
        SC7A20_MUTEX_GIVE;
        SC7A20_Status.ff_enabled = 1;
        SC7A20_Status.last_error = SC7A20_ERR_NONE;
    }
    SC7A20_FUNC_END;
}

/*禁止自由跌落检测（清事件源、断开路由、释放INT占用） */
SC7A20_RET SC7A20_FreefallDisable(SC7A20_NOARG)
{
    uint8_t line;
    SC7A20_FUNC_BEGIN;
    line = SC7A20_Status.freefall_cfg.int_line;
    if(line != 0U)
    {
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_CFG : SC7A20_CTRG_INT1_CFG, 0x00U);
        if(line == 2U)
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_I2_INT1, 0x00U);
        else
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL3, SC7A20_CTRL3_I1_AOI1, 0x00U);
        SC7A20_MUTEX_GIVE;
        sc7a20_int_release(line, SC7A20_INT_OWNER_FREEFALL);
    }
    SC7A20_Status.ff_enabled = 0;
    SC7A20_FUNC_END;
}

/*读取自由跌落事件状态（同步读INT_SRC；锁存模式读后自动清除） */
SC7A20_RET SC7A20_FreefallGetStatus(SC7A20_ARGS(uint8_t *active))
{
    uint8_t line;
    uint8_t *src;
    SC7A20_FUNC_BEGIN;
    line = SC7A20_Status.freefall_cfg.int_line;
    src  = (line == 2U) ? &SC7A20_Status.int2_source : &SC7A20_Status.int1_source;
    if(line != 0U)
    {
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteRead, (line == 2U) ? SC7A20_STRG_INT2_SOURCE : SC7A20_STRG_INT1_SOURCE,
                          (volatile uint8_t *)src);
        SC7A20_MUTEX_GIVE;
    }
    if(active != 0)
        *active = (uint8_t)((*src & SC7A20_INT_SRC_IA) ? 1U : 0U);
    SC7A20_FUNC_END;
}

/******************************运动/静止检测区*********************************/
/*配置运动/静止检测（阈值/静止时长/INT2路由）
 * 软件判定（SC7A20_VERIFIED算法）：总幅值偏离1g超过阈值视为运动。
 * 硬件路径（ACT_THS/ACT_DUR）：COMPAT_INFERRED + TODO_HW_VERIFY，见MotionEnable注释。
 */
SC7A20_RET SC7A20_MotionConfig(SC7A20_ARGS(const sc7a20_motion_config_t *cfg))
{
    SC7A20_FUNC_BEGIN;
    if(cfg == 0 || cfg->motion_threshold_mg == 0U)
    {
        SC7A20_Status.last_error = SC7A20_ERR_BAD_PARAM;
    }
    else
    {
        SC7A20_Status.motion_cfg = *cfg;
        SC7A20_Status.last_error = SC7A20_ERR_NONE;
    }
    SC7A20_FUNC_END;
}

/*使能运动/静止检测
 * 软件状态机始终可用（由SC7A20_MotionUpdate按ODR周期驱动）。
 * 硬件路径（实验性）：
 *  - 写ACT_THS/ACT_DUR（COMPAT_INFERRED：LSB与INT_THS相同、单位=1/ODR，TODO_HW_VERIFY）
 *  - 若route_to_int2，占用INT2并路由ACT状态（COMPAT_INFERRED，上板前不得视为确定行为）
 *  - LIS2DH12存在"静止后自动降ODR/进低功耗"，SC7A20是否相同须实测（TODO_HW_VERIFY）
 */
SC7A20_RET SC7A20_MotionEnable(SC7A20_NOARG)
{
    SC7A20_FUNC_BEGIN;
    if(SC7A20_Status.initialized == 0)
    {
        SC7A20_Status.last_error = SC7A20_ERR_NOT_ONLINE;
    }
    else if(SC7A20_Status.motion_cfg.motion_threshold_mg == 0U)
    {
        SC7A20_Status.last_error = SC7A20_ERR_BAD_PARAM;   //未先MotionConfig
    }
    else
    {
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_ACT_THS,
                          sc7a20_ths_from_mg(SC7A20_Status.motion_cfg.motion_threshold_mg) & SC7A20_ACT_THS_MSK);
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_ACT_DURATION,
                          sc7a20_duration_from_ms((uint16_t)SC7A20_Status.motion_cfg.stillness_time_ms) & SC7A20_ACT_DUR_MSK);
        if(SC7A20_Status.motion_cfg.route_to_int2)
        {
            if(sc7a20_int_claim(2U, SC7A20_INT_OWNER_MOTION) != 0)
            {
                SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_I2_INT2, SC7A20_CTRL6_I2_INT2);
            }
        }
        SC7A20_MUTEX_GIVE;
        SC7A20_Status.motion_tick = 0;
        SC7A20_Status.motion_state = SC7A20_MOTION_STATE_UNKNOWN;
        SC7A20_Status.motion_enabled = 1;
        if(SC7A20_Status.last_error != SC7A20_ERR_INT_BUSY)
            SC7A20_Status.last_error = SC7A20_ERR_NONE;
    }
    SC7A20_FUNC_END;
}

/*禁止运动/静止检测（关ACT寄存器、断开INT2路由、释放占用） */
SC7A20_RET SC7A20_MotionDisable(SC7A20_NOARG)
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    /* ACT_THS/ACT_DUR写0关闭（COMPAT_INFERRED: LIS2DH12语义） */
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_ACT_THS, 0x00U);
    SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, SC7A20_CTRG_ACT_DURATION, 0x00U);
    SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_I2_INT2, 0x00U);
    SC7A20_MUTEX_GIVE;
    sc7a20_int_release(2U, SC7A20_INT_OWNER_MOTION);
    SC7A20_Status.motion_enabled = 0;
    SC7A20_Status.motion_state = SC7A20_MOTION_STATE_UNKNOWN;
    SC7A20_FUNC_END;
}

/*用最近一次加速度样本更新运动状态（软件判定，按ODR周期调用）
 * 判据：总幅值偏离1g的绝对值（|sqrt(x²+y²+z²) - 1000|mg）超过阈值 → 运动；
 *       持续低于阈值达到stillness_time_ms → 静止。
 * 注意：与振动检测（HPF后动态分量）物理含义不同，缓慢拿起属运动但振动很小。
 */
SC7A20_RET SC7A20_MotionUpdate(SC7A20_NOARG)
{
    float mx, my, mz, mag, dev;
    uint32_t still_ms, period_ms;
    SC7A20_FUNC_BEGIN;
    mx = SC7A20_ReadX_mg();
    my = SC7A20_ReadY_mg();
    mz = SC7A20_ReadZ_mg();
    mag = sqrtf(mx * mx + my * my + mz * mz);
    dev = fabsf(mag - 1000.0f);
    still_ms  = SC7A20_Status.motion_cfg.stillness_time_ms;
    period_ms = 1000U / (uint32_t)sc7a20_odr_hz();

    if(dev > (float)SC7A20_Status.motion_cfg.motion_threshold_mg)
    {
        SC7A20_Status.motion_tick = 0;
        SC7A20_Status.motion_state = SC7A20_MOTION_STATE_MOVING;
    }
    else
    {
        if(SC7A20_Status.motion_tick < 0xFFFFU) SC7A20_Status.motion_tick++;
        if((uint32_t)SC7A20_Status.motion_tick * period_ms >= still_ms)
            SC7A20_Status.motion_state = SC7A20_MOTION_STATE_STILL;
    }
    SC7A20_FUNC_END;
}

/*查询运动状态（sc7a20_motion_state_t，由MotionUpdate维护） */
uint8_t SC7A20_MotionGetState(void)
{
    return SC7A20_Status.motion_state;
}

/******************************硬件振动事件区**********************************/
/*配置硬件振动事件（HPF + AOI阈值中断）
 * 阈值/时长/锁存/输出线在配置时写入THS/DUR/LIR寄存器，事件逻辑在Enable时写入。
 */
SC7A20_RET SC7A20_VibrationEventConfig(SC7A20_ARGS(const sc7a20_vibration_event_config_t *cfg))
{
    uint8_t line;
    SC7A20_FUNC_BEGIN;
    if(cfg == 0 || (cfg->int_line != 1U && cfg->int_line != 2U) || cfg->axis_mask == 0U)
    {
        SC7A20_Status.last_error = SC7A20_ERR_BAD_PARAM;
    }
    else
    {
        line = cfg->int_line;
        SC7A20_Status.vibration_cfg = *cfg;
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_THS : SC7A20_CTRG_INT1_THS,
                          sc7a20_ths_from_mg(cfg->threshold_mg));
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_DURATION : SC7A20_CTRG_INT1_DURATION,
                          sc7a20_duration_from_ms(cfg->duration_ms));
        SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL5,
                          (line == 2U) ? SC7A20_CTRL5_LIR_INT2 : SC7A20_CTRL5_LIR_INT1,
                          (cfg->latch) ? ((line == 2U) ? SC7A20_CTRL5_LIR_INT2 : SC7A20_CTRL5_LIR_INT1) : 0x00U);
        SC7A20_MUTEX_GIVE;
        SC7A20_Status.last_error = SC7A20_ERR_NONE;
    }
    SC7A20_FUNC_END;
}

/*使能硬件振动事件
 * 思路：HPF去掉重力/姿态/慢速倾斜 → AOI阈值中断（OR模式 + 高事件）只对快速振动/敲击响应。
 *  - CTRL2.HPIS1/HPIS2 使能对应中断线AOI的高通滤波（SC7A20_VERIFIED位定义）
 *  - HPCF截止频率取值SC7A20资料未给出 → COMPAT_INFERRED（当前用默认00），TODO_HW_VERIFY
 *  - bring-up参数建议：threshold 200~300mg、duration=0、XYZ OR，需实测调整
 */
SC7A20_RET SC7A20_VibrationEventEnable(SC7A20_NOARG)
{
    uint8_t line, cfg, axis;
    SC7A20_FUNC_BEGIN;
    line = SC7A20_Status.vibration_cfg.int_line;
    if(SC7A20_Status.initialized == 0)
    {
        SC7A20_Status.last_error = SC7A20_ERR_NOT_ONLINE;
    }
    else if(line == 0U || SC7A20_Status.vibration_cfg.axis_mask == 0U)
    {
        SC7A20_Status.last_error = SC7A20_ERR_BAD_PARAM;   //未先VibrationEventConfig
    }
    else if(sc7a20_int_claim(line, SC7A20_INT_OWNER_VIBRATION) == 0)
    {
        /* 占用失败，错误码已设置 */
    }
    else
    {
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL2,
                          (line == 2U) ? SC7A20_CTRL2_HPIS2 : SC7A20_CTRL2_HPIS1,
                          (line == 2U) ? SC7A20_CTRL2_HPIS2 : SC7A20_CTRL2_HPIS1);
        /* INT_CFG：OR模式 + 各轴高事件 */
        cfg  = SC7A20_INTMODE_OR;
        axis = SC7A20_Status.vibration_cfg.axis_mask;
        if(axis & SC7A20_AXIS_X) cfg |= SC7A20_INT_CFG_XHIE;
        if(axis & SC7A20_AXIS_Y) cfg |= SC7A20_INT_CFG_YHIE;
        if(axis & SC7A20_AXIS_Z) cfg |= SC7A20_INT_CFG_ZHIE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_CFG : SC7A20_CTRG_INT1_CFG, cfg);
        /* 路由：AOI1 → INT1/INT2（与自由跌落共用AOI1路由位，owner机制防冲突） */
        if(line == 2U)
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_I2_INT1, SC7A20_CTRL6_I2_INT1);
        else
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL3, SC7A20_CTRL3_I1_AOI1, SC7A20_CTRL3_I1_AOI1);
        /* 清残留事件 */
        SC7A20_SPAWN_ARGS(SC7A20_ByteRead, (line == 2U) ? SC7A20_STRG_INT2_SOURCE : SC7A20_STRG_INT1_SOURCE,
                          (line == 2U) ? (volatile uint8_t *)&SC7A20_Status.int2_source : (volatile uint8_t *)&SC7A20_Status.int1_source);
        SC7A20_MUTEX_GIVE;
        SC7A20_Status.vib_evt_enabled = 1;
        SC7A20_Status.last_error = SC7A20_ERR_NONE;
    }
    SC7A20_FUNC_END;
}

/*禁止硬件振动事件（关HPF、清事件源、断开路由、释放占用） */
SC7A20_RET SC7A20_VibrationEventDisable(SC7A20_NOARG)
{
    uint8_t line;
    SC7A20_FUNC_BEGIN;
    line = SC7A20_Status.vibration_cfg.int_line;
    if(line != 0U)
    {
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteWrite, (line == 2U) ? SC7A20_CTRG_INT2_CFG : SC7A20_CTRG_INT1_CFG, 0x00U);
        SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL2,
                          (line == 2U) ? SC7A20_CTRL2_HPIS2 : SC7A20_CTRL2_HPIS1, 0x00U);
        if(line == 2U)
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL6, SC7A20_CTRL6_I2_INT1, 0x00U);
        else
            SC7A20_SPAWN_ARGS(SC7A20_ByteModify, SC7A20_CTRG_CTRL3, SC7A20_CTRL3_I1_AOI1, 0x00U);
        SC7A20_MUTEX_GIVE;
        sc7a20_int_release(line, SC7A20_INT_OWNER_VIBRATION);
    }
    SC7A20_Status.vib_evt_enabled = 0;
    SC7A20_FUNC_END;
}

/*读取硬件振动事件状态（同步读INT_SRC；锁存模式读后自动清除） */
SC7A20_RET SC7A20_VibrationEventGetStatus(SC7A20_ARGS(uint8_t *active))
{
    uint8_t line;
    uint8_t *src;
    SC7A20_FUNC_BEGIN;
    line = SC7A20_Status.vibration_cfg.int_line;
    src  = (line == 2U) ? &SC7A20_Status.int2_source : &SC7A20_Status.int1_source;
    if(line != 0U)
    {
        SC7A20_MUTEX_TAKE;
        SC7A20_SPAWN_ARGS(SC7A20_ByteRead, (line == 2U) ? SC7A20_STRG_INT2_SOURCE : SC7A20_STRG_INT1_SOURCE,
                          (volatile uint8_t *)src);
        SC7A20_MUTEX_GIVE;
    }
    if(active != 0)
        *active = (uint8_t)((*src & SC7A20_INT_SRC_IA) ? 1U : 0U);
    SC7A20_FUNC_END;
}

/******************************软件振动强度区**********************************/
/*用最近一次加速度样本累计振动窗口（按ODR周期调用）
 * 算法：EMA低通估计低频/DC分量（重力/慢变姿态）→ 去除后得动态分量 →
 *       瞬时幅值 v = sqrt(vx²+vy²+vz²) → 窗口内累计平方和与峰值。
 * 窗口结束（样本数达到vib_window_samples）时计算 RMS = sqrt(sum_sq/N) 并清零重开。
 */
SC7A20_RET SC7A20_VibrationAnalyze(SC7A20_NOARG)
{
    float x, y, z, vx, vy, vz, mag;
    SC7A20_FUNC_BEGIN;
    x = SC7A20_ReadX_mg();
    y = SC7A20_ReadY_mg();
    z = SC7A20_ReadZ_mg();
    /* 低频/DC估计（一阶EMA低通） */
    SC7A20_Status.grav_x += SC7A20_VIB_ALPHA * (x - SC7A20_Status.grav_x);
    SC7A20_Status.grav_y += SC7A20_VIB_ALPHA * (y - SC7A20_Status.grav_y);
    SC7A20_Status.grav_z += SC7A20_VIB_ALPHA * (z - SC7A20_Status.grav_z);
    /* 动态分量幅值 */
    vx = x - SC7A20_Status.grav_x;
    vy = y - SC7A20_Status.grav_y;
    vz = z - SC7A20_Status.grav_z;
    mag = sqrtf(vx * vx + vy * vy + vz * vz);
    /* 窗口累计（平方和，窗口末尾一次sqrtf） */
    SC7A20_Status.vib_sum_sq += mag * mag;
    if(mag > SC7A20_Status.vib_peak) SC7A20_Status.vib_peak = mag;
    SC7A20_Status.vib_sample_count++;
    if(SC7A20_Status.vib_sample_count >= SC7A20_Status.vib_window_samples)
    {
        SC7A20_Status.vibration_rms.rms_mg = sqrtf(SC7A20_Status.vib_sum_sq / (float)SC7A20_Status.vib_sample_count);
        SC7A20_Status.vibration_rms.peak_mg = SC7A20_Status.vib_peak;
        SC7A20_Status.vib_sum_sq = 0.0f;
        SC7A20_Status.vib_peak = 0.0f;
        SC7A20_Status.vib_sample_count = 0;
    }
    SC7A20_FUNC_END;
}

/*清空软件振动窗口累计，并按当前ODR重算窗口样本数 */
SC7A20_RET SC7A20_VibrationReset(SC7A20_NOARG)
{
    SC7A20_FUNC_BEGIN;
    SC7A20_Status.vib_sum_sq = 0.0f;
    SC7A20_Status.vib_peak = 0.0f;
    SC7A20_Status.vib_sample_count = 0;
    SC7A20_Status.vibration_rms.rms_mg = 0.0f;
    SC7A20_Status.vibration_rms.peak_mg = 0.0f;
    SC7A20_Status.vib_window_samples = (uint16_t)(((uint32_t)sc7a20_odr_hz() * SC7A20_VIB_WINDOW_MS) / 1000U);
    if(SC7A20_Status.vib_window_samples == 0U) SC7A20_Status.vib_window_samples = 1U;
    SC7A20_FUNC_END;
}

/*获取软件振动窗口结果（RMS/峰值，单位mg） */
const sc7a20_vibration_result_t *SC7A20_VibrationGetResult(void)
{
    return &SC7A20_Status.vibration_rms;
}

/******************************寄存器Dump区************************************/
/*读取关键寄存器到out：out[0]=WHO_AM_I(0x0F)，out[1..32]=0x20~0x3F（共33字节）
 * 调用端负责格式化打印；len须≥33。
 */
SC7A20_RET SC7A20_RegisterDumpRead(SC7A20_ARGS(uint8_t *out, uint16_t len))
{
    SC7A20_FUNC_BEGIN;
    SC7A20_MUTEX_TAKE;
    if(out != 0 && len >= 33U)
    {
        SC7A20_SPAWN_ARGS(SC7A20_ByteRead, SC7A20_STRG_WHO_AM_I, out);            //0x0F
        /* 0x20|0x80=0xA0：MSB=1 触发连续读 0x20~0x3F */
        SC7A20_SPAWN_ARGS(SC7A20_BytesRead, SC7A20_CTRG_CTRL1 | 0x80U, out + 1, 32);       //0x20~0x3F
    }
    SC7A20_MUTEX_GIVE;
    SC7A20_FUNC_END;
}

/*读取并通过USART_Printf打印寄存器Dump（调试用） */
SC7A20_RET SC7A20_RegisterDump(SC7A20_NOARG)
{
    static uint8_t dump[33];
    static const char * const reg_name[33] = {
        "WHO_AM_I", "CTRL_REG1", "CTRL_REG2", "CTRL_REG3", "CTRL_REG4",
        "CTRL_REG5", "CTRL_REG6", "REFERENCE", "STATUS",
        "OUT_X_L", "OUT_X_H", "OUT_Y_L", "OUT_Y_H", "OUT_Z_L", "OUT_Z_H",
        "FIFO_CTRL", "FIFO_SRC", "INT1_CFG", "INT1_SRC", "INT1_THS", "INT1_DUR",
        "INT2_CFG", "INT2_SRC", "INT2_THS", "INT2_DUR",
        "CLICK_CFG", "CLICK_SRC", "CLICK_THS", "TIME_LIMIT", "TIME_LATENCY", "TIME_WINDOW",
        "ACT_THS", "ACT_DUR"
    };
    uint8_t i;
    SC7A20_FUNC_BEGIN;
    SC7A20_SPAWN_ARGS(SC7A20_RegisterDumpRead, dump, (uint16_t)sizeof(dump));
    USART_Printf("\nSC7A20 register dump\n");
    for(i = 0; i < 33U; i++)
    {
        USART_Printf("0x%02X %-10s = 0x%02X\n",
                     (i == 0U) ? 0x0FU : (uint16_t)(0x20U + i - 1U),
                     reg_name[i], dump[i]);
    }
    SC7A20_FUNC_END;
}
