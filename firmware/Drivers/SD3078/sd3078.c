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
SD3078_RET SD3078_Unlock(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR2,
                      SD3078_CTR2_WRTC1, SD3078_CTR2_WRTC1);
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR1,
                      SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2,
                      SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2);
    if (__sd_status == I2C_OK) {
        SD3078_Status.unlocked = 1U;
    }
    SD3078_MUTEX_GIVE;
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
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR2, SD3078_CTR2_WRTC1, SD3078_CTR2_WRTC1);
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR1, SD3078_CTR1_WRTC3|SD3078_CTR1_WRTC2, SD3078_CTR1_WRTC3|SD3078_CTR1_WRTC2);
    SD3078_Status.unlocked = 1;
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/*SD3078写保护上锁
/上锁顺序：先置WRTC2/3=0（0x0F），后置WRTC1=0（0x10）
/使用读-改-写以保留OSF/INTAF/INTDF/BLF等标志位
SD3078_RET SD3078_Lock(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR1,
                      SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2, 0x00U);
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR2,
                      SD3078_CTR2_WRTC1, 0x00U);
    if (__sd_status == I2C_OK) {
        SD3078_Status.unlocked = 0U;
    }
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/* ==================== RTC 时间格式收发 ====================
 * RTC 0x00~0x06 固定按 24 小时制处理；驱动不兼容 12 小时制。
 * Alarm 的 hour/week 编码与 RTC 不完全相同，单独处理。
 * 历史温度发生时间为 6 字节（分/时/周/日/月/年），也不复用 RTC 7 字节格式。 */

/* 读一组时间到 t（BCD 原始值）；week 非 BCD 直接取低 3 位，hour 屏蔽 12_/24 位。
static i2c_status_type sd3078_time_read(uint8_t base, sd3078_time_t *t)
{
    uint8_t b[SD3078_TIME_FIELDS];
    i2c_status_type st;

    st = SD3078_I2C_Receive(SD3078_I2C_ADDR, base, b, SD3078_TIME_FIELDS,
                            (uint8_t*)&SD3078_Status.flag);
    if (st != I2C_OK) {
        return st;
    }

    /* 本项目只支持 24h。若芯片保留了 12h 模式数据，直接报格式错误，
     * 不做 AM/PM 兼容转换，避免把 bit5 当成 BCD 十位。 */
    if ((b[2] & SD3078_HOUR_1224) == 0U) {
        return I2C_ERR_STEP_2;
    }

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

/* 写一组时间（t 为十进制；内部转 BCD 并按 SD3078_24HOUR 置 12_/24 位）。
static i2c_status_type sd3078_time_write(uint8_t base, const sd3078_time_t *t)
{
    uint8_t b[SD3078_TIME_FIELDS];

    b[0] = SD3078_DecToBcd((uint8_t)SD3078_SEC(t));
    b[1] = SD3078_DecToBcd((uint8_t)SD3078_MIN(t));
    b[2] = (uint8_t)(SD3078_DecToBcd((uint8_t)SD3078_HOUR(t)) | SD3078_HOUR_1224);
    b[3] = (uint8_t)SD3078_WEEK(t);
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
/t 为十进制值（0~99/0~12/1~31/0~6/0~23/0~59/0~59），内部转 BCD 并按 SD3078_24HOUR 置 12_/24 位
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

SD3078_RET SD3078_RequestTimeSet(
    SD3078_ARGS(uint8_t year, uint8_t month, uint8_t day,
                uint8_t hour, uint8_t min, uint8_t sec))
{
    if (year > 99U || month < 1U || month > 12U || day < 1U || day > 31U ||
        hour > 23U || min > 59U || sec > 59U) {
        return I2C_ERR_STEP_1;
    }

    taskENTER_CRITICAL();
    SD3078_YEAR(&SD3078_Status.set_time)  = year;
    SD3078_MONTH(&SD3078_Status.set_time) = month;
    SD3078_DAY(&SD3078_Status.set_time)   = day;
    SD3078_HOUR(&SD3078_Status.set_time)  = hour;
    SD3078_MIN(&SD3078_Status.set_time)   = min;
    SD3078_SEC(&SD3078_Status.set_time)   = sec;
    SD3078_Status.time_set_mask |= 0x3FU;
    taskEXIT_CRITICAL();
    return I2C_OK;
}

SD3078_RET SD3078_RequestTimeFieldSet(
    SD3078_ARGS(sd3078_time_field_t field, uint8_t value))
{
    uint8_t bit;

    switch (field) {
    case SD3078_TIME_FIELD_SEC:
        if (value > 59U) return I2C_ERR_STEP_1;
        bit = 0U;
        break;
    case SD3078_TIME_FIELD_MIN:
        if (value > 59U) return I2C_ERR_STEP_1;
        bit = 1U;
        break;
    case SD3078_TIME_FIELD_HOUR:
        if (value > 23U) return I2C_ERR_STEP_1;
        bit = 2U;
        break;
    case SD3078_TIME_FIELD_DAY:
        if (value < 1U || value > 31U) return I2C_ERR_STEP_1;
        bit = 3U;
        break;
    case SD3078_TIME_FIELD_MONTH:
        if (value < 1U || value > 12U) return I2C_ERR_STEP_1;
        bit = 4U;
        break;
    case SD3078_TIME_FIELD_YEAR:
        if (value > 99U) return I2C_ERR_STEP_1;
        bit = 5U;
        break;
    default:
        return I2C_ERR_STEP_1;
    }

    taskENTER_CRITICAL();
    switch (field) {
    case SD3078_TIME_FIELD_SEC:   SD3078_SEC(&SD3078_Status.set_time) = value; break;
    case SD3078_TIME_FIELD_MIN:   SD3078_MIN(&SD3078_Status.set_time) = value; break;
    case SD3078_TIME_FIELD_HOUR:  SD3078_HOUR(&SD3078_Status.set_time) = value; break;
    case SD3078_TIME_FIELD_DAY:   SD3078_DAY(&SD3078_Status.set_time) = value; break;
    case SD3078_TIME_FIELD_MONTH: SD3078_MONTH(&SD3078_Status.set_time) = value; break;
    case SD3078_TIME_FIELD_YEAR:  SD3078_YEAR(&SD3078_Status.set_time) = value; break;
    default: break;
    }
    SD3078_Status.time_set_mask |= (uint8_t)(1U << bit);
    taskEXIT_CRITICAL();
    return I2C_OK;
}

/* 检测并提交时间设置请求（load_task 0.5s 周期调用；星期保留句柄镜像当前值）
SD3078_RET SD3078_TimeSetProcess(SD3078_NOARG)
{
    sd3078_time_t req;
    sd3078_time_t t;
    uint8_t mask;
    i2c_status_type st;
    i2c_status_type lock_st;

    /* 原子取走本批请求。UI 若在 I2C 提交期间再次修改同一字段，会重新置位，
     * 不会被本批成功后的清除动作吞掉。 */
    taskENTER_CRITICAL();
    mask = SD3078_Status.time_set_mask;
    req = SD3078_Status.set_time;
    SD3078_Status.time_set_mask &= (uint8_t)~mask;
    taskEXIT_CRITICAL();

    if (mask == 0U) {
        return I2C_OK;
    }

    /* 关键修复：提交前实时读 RTC，而不是使用“进入 Time 页时”的旧快照。
     * 只覆盖用户本次修改的字段，其他字段（尤其秒）保持此刻真实值。 */
    st = SD3078_TimeLoad();
    if (st != I2C_OK) {
        taskENTER_CRITICAL();
        SD3078_Status.time_set_mask |= mask;
        taskEXIT_CRITICAL();
        return st;
    }

    t = SD3078_Status.time_dec;
    if (mask & (1U << 0)) SD3078_SEC(&t)   = SD3078_SEC(&req);
    if (mask & (1U << 1)) SD3078_MIN(&t)   = SD3078_MIN(&req);
    if (mask & (1U << 2)) SD3078_HOUR(&t)  = SD3078_HOUR(&req);
    if (mask & (1U << 3)) SD3078_DAY(&t)   = SD3078_DAY(&req);
    if (mask & (1U << 4)) SD3078_MONTH(&t) = SD3078_MONTH(&req);
    if (mask & (1U << 5)) SD3078_YEAR(&t)  = SD3078_YEAR(&req);

    st = SD3078_Unlock();
    if (st == I2C_OK) {
        st = SD3078_TimeSetDec(&t);   /* 仍一次性写满 0x00~0x06 */
        lock_st = SD3078_Lock();      /* 无论写是否成功都尝试重新上锁 */
        if (st == I2C_OK) {
            st = lock_st;
        }
    }

    if (st != I2C_OK) {
        taskENTER_CRITICAL();
        SD3078_Status.time_set_mask |= mask;
        taskEXIT_CRITICAL();
        return st;
    }

    /* 立即更新十进制镜像，下一次 TimeLoad 会再用硬件值校正。 */
    SD3078_Status.time_dec = t;
    return I2C_OK;
}

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
        /* Alarm week 是 7 位星期掩码，不是 RTC 的 0~6 编码。 */
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
SD3078_RET SD3078_AlarmSetDec(SD3078_ARGS(const sd3078_time_t *t, uint8_t en))
{
    uint8_t b[SD3078_TIME_FIELDS + 1U];
    i2c_status_type st;

    b[0] = SD3078_DecToBcd((uint8_t)SD3078_SEC(t));
    b[1] = SD3078_DecToBcd((uint8_t)SD3078_MIN(t));
    /* Alarm hour 没有 RTC hour 的 bit7 12/24 标志。 */
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

SD3078_RET SD3078_TempLoad(SD3078_NOARG)
{
    uint8_t t = 0U;
    i2c_status_type st;

    SD3078_MUTEX_TAKE;
    st = SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_TEMP,
                            &t, 1U, (uint8_t*)&SD3078_Status.flag);
    if (st == I2C_OK) {
        SD3078_Status.temp = (int8_t)t;
    }
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

/*设置充电功能与限流电阻
/enable：1=使能充电，0=禁止
/res_sel：0=10kΩ，1=5kΩ，2=2kΩ，3=断开
/注意：使用充电电池时，每次上电必须重置18H寄存器为82H（2kΩ+使能）以确保充电功能打开；
/      非充电电池务必禁止充电，否则会损坏电池
*/
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

SD3078_RET SD3078_SramWrite(
    SD3078_ARGS(uint8_t offset, const uint8_t *pdata, uint16_t len))
{
    i2c_status_type st = I2C_OK;
    i2c_status_type tmp;

    if ((len != 0U && pdata == NULL) ||
        ((uint32_t)offset + (uint32_t)len > SD3078_RAM_LEN)) {
        return I2C_ERR_STEP_1;
    }
    if (len == 0U) {
        return I2C_OK;
    }

    /* 用户 SRAM 同样受 WRTC1/2/3 写保护。整个 unlock -> SRAM write -> lock
     * 放在同一个 I2C mutex 临界区内，避免其它设备操作插入解锁窗口。 */
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

    /* 无论 payload 写入是否成功都尝试重新上锁；优先返回第一个错误。 */
    tmp = SD3078_ByteModify(SD3078_CTRG_CTR1,
                            SD3078_CTR1_WRTC3 | SD3078_CTR1_WRTC2, 0x00U);
    if (st == I2C_OK && tmp != I2C_OK) st = tmp;
    tmp = SD3078_ByteModify(SD3078_CTRG_CTR2, SD3078_CTR2_WRTC1, 0x00U);
    if (st == I2C_OK && tmp != I2C_OK) st = tmp;
    if (tmp == I2C_OK) {
        SD3078_Status.unlocked = 0U;
    }

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
    if (len == 0U) {
        return I2C_OK;
    }

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
/*SD3078正式初始化（默认不充电 + 低功耗配置）
 * ① 充电：每次上电重置充电寄存器为「不充电」（默认）；需要充电时由上层/菜单
 *    手动或自动调用 SD3078_ChargeSet(1, res) 使能（备用电池充电开关）。
 * ② 低功耗：禁止 32K 输出（F32K=1）；禁止报警/频率/倒计时中断输出（INTFE/INTAE/INTDE=0）；
 *    VBAT 模式下禁止 INT 输出（FOBAT=0）；INT 脚高阻（INTS=00 + CTR4.INTS_E=000）。
 *    注：充电功能开启后 VDD 电流会增加约 80uA，属正常现象。
SD3078_RET SD3078_FullInit(SD3078_NOARG)
{
    i2c_status_type st;
    i2c_status_type lock_st;

    st = SD3078_ByteRead(SD3078_CTRG_CTR1, &SD3078_Status.ctr1);
    if (st != I2C_OK) return st;
    if (SD3078_Status.ctr1 == 0xFFU) return I2C_ERR_ADDR;

    st = SD3078_IDLoad();
    if (st != I2C_OK) return st;

    st = SD3078_Unlock();
    if (st != I2C_OK) return st;

    st = SD3078_ChargeSet(SD3078_CHARGE_ENABLE, SD3078_CHARGE_RES_SEL);
    if (st == I2C_OK) {
        st = SD3078_TempAlarmSet(SD3078_TEMP_ALARM_LOW, SD3078_TEMP_ALARM_HIGH);
    }

    lock_st = SD3078_Lock();
    return (st != I2C_OK) ? st : lock_st;
}

/*SD3078完整初始化（在正式SD3078_Init基础上追加温度报警阈值等完整配置）
 * 默认不充电；需要时由上层/菜单调用 SD3078_ChargeSet(1, res) 使能。 */
SD3078_RET SD3078_FullInit(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    //上电即读CTR1校验在线，并快照 PMF/RTCF/OSF（RTCF 会在首次有效写后被芯片清零，必须先读）
    SD3078_SPAWN_ARGS(SD3078_ByteRead, SD3078_CTRG_CTR1, &SD3078_Status.ctr1);
    if(SD3078_Status.ctr1 != 0xFFU)//读到0xFF视为无设备（总线浮空）→ 不置 initialized，下轮重试
    {
        //读取芯片ID（一次性快照）
        SD3078_SPAWN_NOARG(SD3078_IDLoad);
        //解锁寄存器写入（WRTC1/2/3=1）
        SD3078_SPAWN_NOARG(SD3078_Unlock);
        //每次上电重置充电寄存器：默认不充电（由菜单/上层手动控制充电）
        SD3078_SPAWN_ARGS(SD3078_ChargeSet, SD3078_CHARGE_ENABLE, SD3078_CHARGE_RES_SEL);
        //设置温度报警阈值
        SD3078_SPAWN_ARGS(SD3078_TempAlarmSet, SD3078_TEMP_ALARM_LOW, SD3078_TEMP_ALARM_HIGH);
        //写完成后上锁，避免误写
        SD3078_SPAWN_NOARG(SD3078_Lock);
        SD3078_Status.initialized = 1;
    }
    SD3078_FUNC_END;
}


