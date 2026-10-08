#include "sd3078.h"

static struct SD3078_StatusTypedef SD3078_Status;//SD3078状态全局变量


/*******************************基本操作区*************************************/
SD3078_RET SD3078_ByteWrite(SD3078_ARGS(uint8_t reg, uint8_t data))
{
    SD3078_FUNC_BEGIN;
    SD3078_Status.sendbuf[0] = data;
    SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, reg, (uint8_t*)SD3078_Status.sendbuf, 1, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    SD3078_FUNC_END;
}

SD3078_RET SD3078_ByteRead(SD3078_ARGS(uint8_t reg, volatile uint8_t *data))
{
    SD3078_FUNC_BEGIN;
    SD3078_EXEC(SD3078_I2C_Receive(SD3078_I2C_ADDR, reg, (uint8_t*)data, 1, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    SD3078_FUNC_END;
}

SD3078_RET SD3078_BytesRead(SD3078_ARGS(uint8_t reg, uint8_t *pdata, uint16_t len))
{
    SD3078_FUNC_BEGIN;
    SD3078_EXEC(SD3078_I2C_Receive(SD3078_I2C_ADDR, reg, pdata, len, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    SD3078_FUNC_END;
}

/*SD3078修改对应寄存器的特定位
/遵循读-修改-写的顺序
/将mask中为1的位按data中的对应位修改成1或0，mask中为0的位不被修改
/注意：写操作前需要SD3078_Unlock()
*/
SD3078_RET SD3078_ByteModify(SD3078_ARGS(uint8_t reg, uint8_t mask, uint8_t data))
{
    SD3078_FUNC_BEGIN;
    SD3078_SPAWN_ARGS(SD3078_ByteRead, reg, SD3078_Status.sendbuf);
    SD3078_Status.sendbuf[0] = (SD3078_Status.sendbuf[0] & (~mask)) | (data & mask);
    SD3078_SPAWN_ARGS(SD3078_ByteWrite, reg, SD3078_Status.sendbuf[0]);
    SD3078_FUNC_END;
}

/*******************************写保护操作区***********************************/
/*SD3078写保护解锁
/SD3078有WRTC1、WRTC2、WRTC3三个写保护位，必须全为1才能写寄存器
/解锁顺序：先置WRTC1=1（0x10），后置WRTC2/3=1（0x0F）
/使用读-改-写以保留OSF/INTAF/INTDF/BLF等标志位
*/
SD3078_RET SD3078_Unlock(SD3078_NOARG)
{
    i2c_status_type st;
    i2c_status_type rollback_st;

    SD3078_MUTEX_TAKE;
    st = SD3078_ByteModify(SD3078_CTRG_CTR2,
                           SD3078_CTR2_WRTC1, SD3078_CTR2_WRTC1);
    if (st == I2C_OK) {
        st = SD3078_ByteModify(SD3078_CTRG_CTR1,
                               SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2,
                               SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2);
        if (st != I2C_OK) {
            /* 第二步失败时撤回 WRTC1，避免留下半解锁状态。 */
            rollback_st = SD3078_ByteModify(SD3078_CTRG_CTR2,
                                            SD3078_CTR2_WRTC1, 0x00U);
            (void)rollback_st;
        }
    }
    SD3078_Status.unlocked = (st == I2C_OK) ? 1U : 0U;
    SD3078_MUTEX_GIVE;
    return st;
}

/*SD3078写保护上锁
/上锁顺序：先置WRTC2/3=0（0x0F），后置WRTC1=0（0x10）
/使用读-改-写以保留OSF/INTAF/INTDF/BLF等标志位
*/
SD3078_RET SD3078_Lock(SD3078_NOARG)
{
    i2c_status_type st1;
    i2c_status_type st2;

    SD3078_MUTEX_TAKE;
    st1 = SD3078_ByteModify(SD3078_CTRG_CTR1,
                            SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2, 0x00U);
    /* 即使第一步失败也尝试清 WRTC1，尽最大可能关闭写窗口。 */
    st2 = SD3078_ByteModify(SD3078_CTRG_CTR2, SD3078_CTR2_WRTC1, 0x00U);

    /* 三个位必须全为1才允许写；任一步成功清零后都已回到不可写状态。 */
    if (st1 == I2C_OK || st2 == I2C_OK) SD3078_Status.unlocked = 0U;
    SD3078_MUTEX_GIVE;
    return (st1 != I2C_OK) ? st1 : st2;
}

/* ==================== RTC 时间格式收发 ====================
 * RTC 0x00~0x06 固定按 24 小时制处理；驱动不兼容 12 小时制。
 * Alarm 的 hour/week 编码与 RTC 不完全相同，单独处理。
 * 历史温度发生时间为 6 字节（分/时/周/日/月/年），也不复用 RTC 7 字节格式。 */

/* 读 RTC 时间到 t（BCD 原始值）。返回 i2c_status_type；24h 位未置也视为格式错误。 */
static i2c_status_type sd3078_time_read(uint8_t base, sd3078_time_t *t)
{
    uint8_t b[SD3078_TIME_FIELDS];
    i2c_status_type st;

    st = SD3078_I2C_Receive(SD3078_I2C_ADDR, base, b, SD3078_TIME_FIELDS,
                            (uint8_t*)&SD3078_Status.flag);
    if (st != I2C_OK) return st;

    /* 本项目仅支持 24h。12h 数据不做兼容转换。 */
    if ((b[2] & SD3078_HOUR_1224) == 0U) return I2C_ERR_STEP_2;

    SD3078_SEC(t)   = b[0];
    SD3078_MIN(t)   = b[1];
    SD3078_HOUR(t)  = (uint8_t)(b[2] & SD3078_HOUR_MSK);
    SD3078_WEEK(t)  = (uint8_t)(b[3] & SD3078_WEEK_MSK);
    SD3078_DAY(t)   = b[4];
    SD3078_MONTH(t) = b[5];
    SD3078_YEAR(t)  = b[6];
    return I2C_OK;
}

/* 由 BCD 镜像生成十进制镜像（week 非 BCD，原样搬运） */
static void sd3078_time_to_dec(sd3078_time_t *src, sd3078_time_t *dst)
{
    SD3078_SEC(dst)   = SD3078_BcdToDec(SD3078_SEC(src));
    SD3078_MIN(dst)   = SD3078_BcdToDec(SD3078_MIN(src));
    SD3078_HOUR(dst)  = SD3078_BcdToDec(SD3078_HOUR(src));
    SD3078_WEEK(dst)  = SD3078_WEEK(src);
    SD3078_DAY(dst)   = SD3078_BcdToDec(SD3078_DAY(src));
    SD3078_MONTH(dst) = SD3078_BcdToDec(SD3078_MONTH(src));
    SD3078_YEAR(dst)  = SD3078_BcdToDec(SD3078_YEAR(src));
}

/* 写一组 RTC 时间（t 为十进制；固定 24h，内部转 BCD 并置 bit7）。
 * 一次性写 7 字节：单独写某一位会引起时间数据错误进位。 */
static i2c_status_type sd3078_time_write(uint8_t base, const sd3078_time_t *t)
{
    uint8_t b[SD3078_TIME_FIELDS];

    b[0] = SD3078_DecToBcd((uint8_t)SD3078_SEC(t));
    b[1] = SD3078_DecToBcd((uint8_t)SD3078_MIN(t));
    b[2] = (uint8_t)(SD3078_DecToBcd((uint8_t)SD3078_HOUR(t)) | SD3078_HOUR_1224);
    b[3] = (uint8_t)(SD3078_WEEK(t) & SD3078_WEEK_MSK);
    b[4] = SD3078_DecToBcd((uint8_t)SD3078_DAY(t));
    b[5] = SD3078_DecToBcd((uint8_t)SD3078_MONTH(t));
    b[6] = SD3078_DecToBcd((uint8_t)SD3078_YEAR(t));

    return SD3078_I2C_Transmit(SD3078_I2C_ADDR, base, b, SD3078_TIME_FIELDS,
                               (uint8_t*)&SD3078_Status.flag);
}

/******************************实时时钟操作区**********************************/
/*读取实时时钟
/当芯片收到读实时时钟数据命令时，所有实时时钟数据被锁存（走时不受影响），可避免错读
/因此一次性读取00H~06H共7字节
*/
SD3078_RET SD3078_TimeLoad(SD3078_NOARG)
{
    i2c_status_type st;

    SD3078_MUTEX_TAKE;
    st = sd3078_time_read(SD3078_STRG_SEC, &SD3078_Status.time);
    if (st == I2C_OK) {
        sd3078_time_to_dec(&SD3078_Status.time, &SD3078_Status.time_dec);
    }
    SD3078_MUTEX_GIVE;
    return st;
}
/* 时间读取 API：十进制镜像（TimeLoad 后有效）与原始 BCD 镜像各一组。
 * 现在只是统一结构体的取值，不再是 14 个手写样板。 */
uint8_t SD3078_ReadSec(void)   { return (uint8_t)SD3078_SEC(&SD3078_Status.time_dec); }
uint8_t SD3078_ReadMin(void)   { return (uint8_t)SD3078_MIN(&SD3078_Status.time_dec); }
uint8_t SD3078_ReadHour(void)  { return (uint8_t)SD3078_HOUR(&SD3078_Status.time_dec); }
uint8_t SD3078_ReadWeek(void)  { return (uint8_t)SD3078_WEEK(&SD3078_Status.time_dec); }
uint8_t SD3078_ReadDay(void)   { return (uint8_t)SD3078_DAY(&SD3078_Status.time_dec); }
uint8_t SD3078_ReadMonth(void) { return (uint8_t)SD3078_MONTH(&SD3078_Status.time_dec); }
uint8_t SD3078_ReadYear(void)  { return (uint8_t)SD3078_YEAR(&SD3078_Status.time_dec); }

uint8_t SD3078_ReadSecBCD(void)   { return (uint8_t)SD3078_SEC(&SD3078_Status.time); }
uint8_t SD3078_ReadMinBCD(void)   { return (uint8_t)SD3078_MIN(&SD3078_Status.time); }
uint8_t SD3078_ReadHourBCD(void)  { return (uint8_t)SD3078_HOUR(&SD3078_Status.time); }
uint8_t SD3078_ReadWeekBCD(void)  { return (uint8_t)SD3078_WEEK(&SD3078_Status.time); }
uint8_t SD3078_ReadDayBCD(void)   { return (uint8_t)SD3078_DAY(&SD3078_Status.time); }
uint8_t SD3078_ReadMonthBCD(void) { return (uint8_t)SD3078_MONTH(&SD3078_Status.time); }
uint8_t SD3078_ReadYearBCD(void)  { return (uint8_t)SD3078_YEAR(&SD3078_Status.time); }

uint8_t SD3078_BcdToDec(uint8_t bcd)//BCD → 十进制（时间/日期寄存器为 BCD 码）
{
    return (uint8_t)(((bcd >> 4) & 0x0FU) * 10U + (bcd & 0x0FU));
}
uint8_t SD3078_DecToBcd(uint8_t dec)//十进制 → BCD（时间/日期寄存器为 BCD 码）
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

/*设置实时时钟
/写实时时间数据时不可以单独写其中某一位，必须一次性写入全部7个实时时钟数据（00H~06H），
/否则可能引起时间数据错误进位
/t 为十进制值；固定按 24 小时制编码
*/
SD3078_RET SD3078_TimeSetDec(SD3078_ARGS(const sd3078_time_t *t))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(sd3078_time_write, SD3078_STRG_SEC, t);
    //写秒寄存器时会对秒以下内部计数器清零，实现时间同步
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/* RTC 编辑请求/合并/提交策略已移至 Applications/algorithm/sd3078_algo.c。
 * Driver 保留 TimeLoad/TimeSetDec 两个原子硬件操作。 */

/******************************时间报警操作区**********************************/
SD3078_RET SD3078_AlarmLoad(SD3078_NOARG)
{
    uint8_t b[SD3078_TIME_FIELDS + 1U];
    i2c_status_type st;

    SD3078_MUTEX_TAKE;
    st = SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_ALARM_SEC,
                            b, sizeof(b), (uint8_t*)&SD3078_Status.flag);
    if (st == I2C_OK) {
        SD3078_SEC(&SD3078_Status.alarm_time)   = b[0];
        SD3078_MIN(&SD3078_Status.alarm_time)   = b[1];
        SD3078_HOUR(&SD3078_Status.alarm_time)  = (uint8_t)(b[2] & SD3078_HOUR_MSK);
        SD3078_WEEK(&SD3078_Status.alarm_time)  = (uint8_t)(b[3] & SD3078_ALARMEN_MSK);
        SD3078_DAY(&SD3078_Status.alarm_time)   = b[4];
        SD3078_MONTH(&SD3078_Status.alarm_time) = b[5];
        SD3078_YEAR(&SD3078_Status.alarm_time)  = b[6];
        SD3078_Status.alarm_en = (uint8_t)(b[7] & SD3078_ALARMEN_MSK);
    }
    SD3078_MUTEX_GIVE;
    return st;
}

/*设置时间报警
/en为报警允许位（SD3078_ALARMEN_*的按位或），如：
/  SD3078_ALARMEN_EAS|SD3078_ALARMEN_EAMN|SD3078_ALARMEN_EAH  每天固定时分秒报警
/  SD3078_ALARMEN_EAD|SD3078_ALARMEN_EAMO|SD3078_ALARMEN_EAY  每年固定日期报警
/注意：日报警与星期报警同时允许时只有日报警有效
*/
SD3078_RET SD3078_AlarmSetDec(SD3078_ARGS(const sd3078_time_t *t, uint8_t en))
{
    uint8_t b[SD3078_TIME_FIELDS + 1U];
    i2c_status_type st;

    b[0] = SD3078_DecToBcd((uint8_t)SD3078_SEC(t));
    b[1] = SD3078_DecToBcd((uint8_t)SD3078_MIN(t));
    b[2] = (uint8_t)(SD3078_DecToBcd((uint8_t)SD3078_HOUR(t)) & SD3078_HOUR_MSK);
    b[3] = (uint8_t)(SD3078_WEEK(t) & SD3078_ALARMEN_MSK); /* 星期 bitmask */
    b[4] = SD3078_DecToBcd((uint8_t)SD3078_DAY(t));
    b[5] = SD3078_DecToBcd((uint8_t)SD3078_MONTH(t));
    b[6] = SD3078_DecToBcd((uint8_t)SD3078_YEAR(t));
    b[7] = (uint8_t)(en & SD3078_ALARMEN_MSK);

    SD3078_MUTEX_TAKE;
    st = SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_STRG_ALARM_SEC,
                             b, sizeof(b), (uint8_t*)&SD3078_Status.flag);
    SD3078_MUTEX_GIVE;
    return st;
}

SD3078_RET SD3078_AlarmClear(SD3078_NOARG)//清除报警中断标志（INTAF写0）
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR1, SD3078_CTR1_INTAF, 0x00);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

uint8_t SD3078_HasAlarm(void)//查询报警中断标志（INTAF）
{
    return SD3078_Status.ctr1 & SD3078_CTR1_INTAF;
}

/******************************温度操作区**************************************/
SD3078_RET SD3078_TempLoad(SD3078_NOARG)
{
    uint8_t t = 0U;
    i2c_status_type st;

    SD3078_MUTEX_TAKE;
    st = SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_TEMP,
                            &t, 1U, (uint8_t*)&SD3078_Status.flag);
    if (st == I2C_OK) SD3078_Status.temp = (int8_t)t;
    SD3078_MUTEX_GIVE;
    return st;
}

int8_t SD3078_ReadTemp(void)//读取温度（°C，补码，如0x10=16°C、0xFE=-2°C）
{
    return SD3078_Status.temp;
}

SD3078_RET SD3078_TempAlarmSet(SD3078_ARGS(int8_t low, int8_t high))//设置高低温报警阈值（同时使能）
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteWrite, SD3078_CTRG_TEMP_AL, (uint8_t)low);
    SD3078_SPAWN_ARGS(SD3078_ByteWrite, SD3078_CTRG_TEMP_AH, (uint8_t)high);
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR4, SD3078_CTR4_INTTHE|SD3078_CTR4_INTTLE, SD3078_CTR4_INTTHE|SD3078_CTR4_INTTLE);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

SD3078_RET SD3078_TempHistoryLoad(SD3078_NOARG)//读取历史高低温值（0x1E/0x1F）
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    //TODO: 历史温度与发生时间（0x1E~0x2B）的读取，需要时在此补充镜像字段
    SD3078_SPAWN_ARGS(SD3078_BytesRead, SD3078_STRG_TEMP_HIS_L, SD3078_Status.sendbuf, 2);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/******************************电池与充电操作区********************************/
SD3078_RET SD3078_BattLoad(SD3078_NOARG)
{
    uint8_t ctr5 = 0U;
    uint8_t bval = 0U;
    i2c_status_type st;

    SD3078_MUTEX_TAKE;
    st = SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_CTR5,
                            &ctr5, 1U, (uint8_t*)&SD3078_Status.flag);
    if (st == I2C_OK) {
        st = SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_BAT_VAL,
                                &bval, 1U, (uint8_t*)&SD3078_Status.flag);
    }
    if (st == I2C_OK) {
        SD3078_Status.ctr5 = ctr5;
        SD3078_Status.sendbuf[0] = bval;
        SD3078_Status.batt =
            ((uint16_t)(ctr5 & SD3078_CTR5_BAT8_VAL) << 1) | bval;
    }
    SD3078_MUTEX_GIVE;
    return st;
}

uint16_t SD3078_ReadBatt(void)//读取电池电压（单位：mV，如3040）
{
    return (uint16_t)SD3078_Status.batt * 10U;
}

uint8_t SD3078_IsBattLow(void)//电池欠压标志（BLF，低于2.2V）
{
    return SD3078_Status.ctr5 & SD3078_CTR5_BLF;
}

uint8_t SD3078_IsBattHigh(void)//电池高压标志（BHF，高于3.3V）
{
    return SD3078_Status.ctr5 & SD3078_CTR5_BHF;
}

/* 设置充电功能与限流电阻。
 * 本函数只做寄存器访问；MS621FE Off/On/Auto 管理在 sd3078_algo.c。 */
SD3078_RET SD3078_ChargeSet(SD3078_ARGS(uint8_t enable, uint8_t res_sel))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteWrite, SD3078_CTRG_CHARGE, (enable ? SD3078_CHARGE_ENCH : 0x00U) | (res_sel & SD3078_CHARGE_RES_MSK));
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/******************************倒计时定时器操作区******************************/
/*设置24位倒计时值与时钟源
/count：24位计数值（0~0xFFFFFF），定时时间 = count / 时钟源频率
/src：SD3078_CTR3_TDS_*（4096Hz/1024Hz/1S/1MIN）
/注意：重新配置倒计时中断时需要先禁止INTDE再使能，方可生效
*/
SD3078_RET SD3078_CountdownSet(SD3078_ARGS(uint32_t count, uint8_t src))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_Status.sendbuf[0] = (uint8_t)(count & 0xFFU);
    SD3078_Status.sendbuf[1] = (uint8_t)((count >> 8) & 0xFFU);
    SD3078_Status.sendbuf[2] = (uint8_t)((count >> 16) & 0xFFU);
    SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_CTRG_TD0, SD3078_Status.sendbuf, 3, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR3, SD3078_CTR3_TDS1|SD3078_CTR3_TDS0, src);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

SD3078_RET SD3078_CountdownEnable(SD3078_ARGS(uint8_t enable))//使能/禁止倒计时中断
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR2, SD3078_CTR2_INTDE, enable ? SD3078_CTR2_INTDE : 0x00U);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

uint8_t SD3078_HasCountdown(void)//倒计时中断标志（INTDF）
{
    return SD3078_Status.ctr1 & SD3078_CTR1_INTDF;
}

/******************************频率/32K输出操作区******************************/
/*设置INT脚频率中断输出频率
/需同时使能频率中断（CTR2的INTFE=1）并选择INT输出为频率中断（INTS=10）
/fs取SD3078_CTR3_FS_*系列宏
*/
SD3078_RET SD3078_FreqOutSet(SD3078_ARGS(uint8_t fs))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR3, SD3078_CTR3_FS_MSK, fs & SD3078_CTR3_FS_MSK);
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR2, SD3078_CTR2_INTFE|SD3078_CTR2_INTS1|SD3078_CTR2_INTS0,
                      SD3078_CTR2_INTFE|SD3078_CTR2_INTS_FREQ);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/*32K输出控制（F32K脚）
/enable：1=允许输出32768Hz方波，0=禁止
*/
SD3078_RET SD3078_F32KSet(SD3078_ARGS(uint8_t enable))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR3, SD3078_CTR3_F32K, enable ? 0x00U : SD3078_CTR3_F32K);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/******************************用户RAM与ID操作区*******************************/
SD3078_RET SD3078_SramWrite(
    SD3078_ARGS(uint8_t offset, const uint8_t *pdata, uint16_t len))
{
    i2c_status_type st = I2C_OK;
    i2c_status_type tmp;

    if ((len != 0U && pdata == NULL) ||
        ((uint32_t)offset + (uint32_t)len > SD3078_RAM_LEN)) {
        return I2C_ERR_STEP_1;
    }
    if (len == 0U) return I2C_OK;

    /* 用户 RAM 受 WRTC 写保护；解锁/写入/上锁在同一个 I2C mutex 窗口内。 */
    SD3078_MUTEX_TAKE;

    tmp = SD3078_ByteModify(SD3078_CTRG_CTR2, SD3078_CTR2_WRTC1, SD3078_CTR2_WRTC1);
    if (tmp != I2C_OK) st = tmp;
    if (st == I2C_OK) {
        tmp = SD3078_ByteModify(SD3078_CTRG_CTR1,
                                SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2,
                                SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2);
        if (tmp != I2C_OK) st = tmp;
    }
    if (st == I2C_OK) {
        SD3078_Status.unlocked = 1U;
        st = SD3078_I2C_Transmit(SD3078_I2C_ADDR,
                                 (uint8_t)(SD3078_RAM_START + offset),
                                 (uint8_t *)pdata, len,
                                 (uint8_t*)&SD3078_Status.flag);
    }

    /* 无论 payload 是否成功都尽力恢复写保护，优先返回首个错误。 */
    tmp = SD3078_ByteModify(SD3078_CTRG_CTR1,
                            SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2, 0x00U);
    if (st == I2C_OK && tmp != I2C_OK) st = tmp;
    tmp = SD3078_ByteModify(SD3078_CTRG_CTR2, SD3078_CTR2_WRTC1, 0x00U);
    if (st == I2C_OK && tmp != I2C_OK) st = tmp;
    if (tmp == I2C_OK) SD3078_Status.unlocked = 0U;

    SD3078_MUTEX_GIVE;
    return st;
}

SD3078_RET SD3078_SramRead(
    SD3078_ARGS(uint8_t offset, uint8_t *pdata, uint16_t len))
{
    i2c_status_type st;

    if ((len != 0U && pdata == NULL) ||
        ((uint32_t)offset + (uint32_t)len > SD3078_RAM_LEN)) {
        return I2C_ERR_STEP_1;
    }
    if (len == 0U) return I2C_OK;

    SD3078_MUTEX_TAKE;
    st = SD3078_I2C_Receive(SD3078_I2C_ADDR,
                            (uint8_t)(SD3078_RAM_START + offset),
                            pdata, len, (uint8_t*)&SD3078_Status.flag);
    SD3078_MUTEX_GIVE;
    return st;
}

SD3078_RET SD3078_IDLoad(SD3078_NOARG)//读取芯片ID（72H~79H共8字节）
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_BytesRead, SD3078_STRG_ID, SD3078_Status.id, 8);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/******************************上电状态标志区**********************************/
/*以下三个标志读取初始化时的一次性快照（SD3078_Status.ctr1），不随状态页刷新：
 *  - RTCF 会在上电后第一次有效写时被芯片自动清零，因此必须用初始化开头（任何写之前）的快照值
 *  - PMF 为实时电源模式、OSF 为粘性停振标志，快照反映上电时刻状态
 */
uint8_t SD3078_ReadPMF(void)//电源模式标志（0=VDD主电源，1=VBAT后备电池供电）
{
    return SD3078_Status.ctr1 & SD3078_CTR1_PMF;
}
uint8_t SD3078_ReadRTCF(void)//上电标志（1=曾发生全部电源失效后再上电）
{
    return SD3078_Status.ctr1 & SD3078_CTR1_RTCF;
}
uint8_t SD3078_ReadOSF(void)//停振标志（1=内部振荡器曾停振）
{
    return SD3078_Status.ctr1 & SD3078_CTR1_OSF;
}

/* 读取芯片 UID（唯一身份识别码，8 字节）。
 * 数据来自初始化时一次性读取的镜像（0x72~0x79），不在此发起 I2C。
 * idx: 0~7 取单字节（idx=0 为寄存器 0x72 的最高字节），越界返回 0。 */
uint8_t SD3078_ReadID(uint8_t idx)
{
    if (idx >= sizeof(SD3078_Status.id)) return 0U;
    return SD3078_Status.id[idx];
}

/*******************************初始化区***************************************/
/* SD3078统一初始化。
 * ① 充电寄存器恢复为默认关闭，具体 Off/On/Auto 由 sd3078_algo 接管；
 * ② 禁止32K/报警/频率/倒计时INT输出，VBAT模式不输出INT，INT脚保持高阻；
 * ③ 写入默认高低温报警阈值，为后续需要时启用温度报警准备好寄存器。
 * 不配置RTC时间/闹钟/倒计时值；失败由上层根据 i2c_status_type 重试。 */
SD3078_RET SD3078_Init(SD3078_NOARG)
{
    i2c_status_type st;
    i2c_status_type lock_st;

    /* 首次写之前快照 CTR1，保留 RTCF/PMF/OSF 上电状态。
     * ByteRead 是驱动内部底层原语，本身不加锁；初始化入口显式保护这笔事务。 */
    SD3078_MUTEX_TAKE;
    st = SD3078_ByteRead(SD3078_CTRG_CTR1, &SD3078_Status.ctr1);
    SD3078_MUTEX_GIVE;
    if (st != I2C_OK) return st;
    if (SD3078_Status.ctr1 == 0xFFU) return I2C_ERR_ADDR;

    st = SD3078_IDLoad();
    if (st != I2C_OK) return st;

    st = SD3078_Unlock();
    if (st != I2C_OK) return st;

    st = SD3078_ChargeSet(SD3078_CHARGE_ENABLE, SD3078_CHARGE_RES_SEL);

    /* ByteModify 是底层原语，本身不拿 mutex；初始化里的连续 RMW 显式保护。 */
    if (st == I2C_OK) {
        SD3078_MUTEX_TAKE;
        st = SD3078_ByteModify(SD3078_CTRG_CTR3,
                               SD3078_CTR3_F32K, SD3078_CTR3_F32K);
        if (st == I2C_OK) {
            st = SD3078_ByteModify(SD3078_CTRG_CTR2,
                                   SD3078_CTR2_INTFE | SD3078_CTR2_INTAE |
                                   SD3078_CTR2_INTDE | SD3078_CTR2_FOBAT, 0x00U);
        }
        if (st == I2C_OK) {
            st = SD3078_ByteModify(SD3078_CTRG_CTR4,
                                   SD3078_CTR4_INTS_E2 | SD3078_CTR4_INTS_E1 |
                                   SD3078_CTR4_INTS_E0, 0x00U);
        }
        SD3078_MUTEX_GIVE;
    }

    if (st == I2C_OK) {
        st = SD3078_TempAlarmSet(SD3078_TEMP_ALARM_LOW, SD3078_TEMP_ALARM_HIGH);
    }

    lock_st = SD3078_Lock();
    return (st != I2C_OK) ? st : lock_st;
}


