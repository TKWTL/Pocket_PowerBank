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
*/
SD3078_RET SD3078_Lock(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR1, SD3078_CTR1_WRTC3|SD3078_CTR1_WRTC2, 0x00U);
    SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR2, SD3078_CTR2_WRTC1, 0x00U);
    SD3078_Status.unlocked = 0;
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/* ==================== 时间格式统一收发 ====================
 * SD3078 有四组同格式（秒分时周日月年）的 7 字节时间寄存器：
 *   0x00~0x06 RTC 时间 / 0x07~0x0D 报警时间 / 0x20~0x26 历史最低温时间 / 0x27~0x2D 历史最高温时间。
 * 这里各用一份函数服务全部四组，调用方只传组基地址（见 sd3078.h 的 sd3078_time_t）。
 * BCD 与 12/24 制式处理都收在这一处，不再逐处分写。 */

/* 读一组时间到 t（BCD 原始值）；week 非 BCD 直接取低 3 位，hour 屏蔽 12_/24 位。
 * 返回非 I2C_OK 表示通信失败（调用方决定是否清 initialized）。 */
static i2c_status_type sd3078_time_read(uint8_t base, sd3078_time_t *t)
{
    uint8_t b[SD3078_TIME_FIELDS];
    if(SD3078_I2C_Receive(SD3078_I2C_ADDR, base, b, SD3078_TIME_FIELDS, (uint8_t*)&SD3078_Status.flag) != I2C_OK)
    {
        return I2C_ERR_INTERRUPT;
    }
    /* 字节序固定为 秒 分 时 周 日 月 年，与 b[] 下标一致 */
    SD3078_SEC(t)   = b[0];
    SD3078_MIN(t)   = b[1];
    SD3078_HOUR(t)  = (uint8_t)(b[2] & ~SD3078_HOUR_1224);
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
 * 一次性写 7 字节：单独写某一位会引起时间数据错误进位。 */
static i2c_status_type sd3078_time_write(uint8_t base, const sd3078_time_t *t)
{
    uint8_t b[SD3078_TIME_FIELDS];
    b[0] = SD3078_DecToBcd((uint8_t)SD3078_SEC(t));
    b[1] = SD3078_DecToBcd((uint8_t)SD3078_MIN(t));
    b[2] = SD3078_DecToBcd((uint8_t)SD3078_HOUR(t));
#if SD3078_24HOUR
    b[2] |= SD3078_HOUR_1224;
#endif
    b[3] = (uint8_t)SD3078_WEEK(t);          //星期非 BCD
    b[4] = SD3078_DecToBcd((uint8_t)SD3078_DAY(t));
    b[5] = SD3078_DecToBcd((uint8_t)SD3078_MONTH(t));
    b[6] = SD3078_DecToBcd((uint8_t)SD3078_YEAR(t));
    return SD3078_I2C_Transmit(SD3078_I2C_ADDR, base, b, SD3078_TIME_FIELDS, (uint8_t*)&SD3078_Status.flag);
}

/******************************实时时钟操作区**********************************/
/*读取实时时钟
/当芯片收到读实时时钟数据命令时，所有实时时钟数据被锁存（走时不受影响），可避免错读
/因此一次性读取00H~06H共7字节
*/
SD3078_RET SD3078_TimeLoad(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    if(sd3078_time_read(SD3078_STRG_SEC, &SD3078_Status.time) != I2C_OK)
    {
        SD3078_MARK_OFFLINE_ON_I2C_FAIL();   /* 通信失败：清 initialized → 下轮 load_task 重新初始化 */
        SD3078_MUTEX_GIVE;
        SD3078_FUNC_END;
    }
    sd3078_time_to_dec(&SD3078_Status.time, &SD3078_Status.time_dec);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
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

/* 请求设置时间（UI 调用，十进制输入；写入句柄待 load_task 提交，UI 不直接访问 I2C） */
SD3078_RET SD3078_RequestTimeSet(SD3078_ARGS(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec))
{
    SD3078_FUNC_BEGIN;
    SD3078_Status.set_year  = year;
    SD3078_Status.set_month = month;
    SD3078_Status.set_day   = day;
    SD3078_Status.set_hour  = hour;
    SD3078_Status.set_min   = min;
    SD3078_Status.set_sec   = sec;
    SD3078_Status.time_set_pending = 1;
    SD3078_FUNC_END;
}

/* 检测并提交时间设置请求（load_task 0.5s 周期调用；星期保留句柄镜像当前值）
 * 内部依次调用 Unlock/TimeSetDec/Lock（各含互斥），自身不持锁，避免嵌套死锁 */
SD3078_RET SD3078_TimeSetProcess(SD3078_NOARG)
{
    sd3078_time_t t;
    SD3078_FUNC_BEGIN;
    if (SD3078_Status.time_set_pending) {
        SD3078_SEC(&t)   = SD3078_Status.set_sec;
        SD3078_MIN(&t)   = SD3078_Status.set_min;
        SD3078_HOUR(&t)  = SD3078_Status.set_hour;
        SD3078_WEEK(&t)  = SD3078_WEEK(&SD3078_Status.time_dec);   //星期不在菜单中设置，沿用当前值
        SD3078_DAY(&t)   = SD3078_Status.set_day;
        SD3078_MONTH(&t) = SD3078_Status.set_month;
        SD3078_YEAR(&t)  = SD3078_Status.set_year;
        SD3078_SPAWN_NOARG(SD3078_Unlock);
        SD3078_SPAWN_ARGS(SD3078_TimeSetDec, &t);
        SD3078_SPAWN_NOARG(SD3078_Lock);
        SD3078_Status.time_set_pending = 0;
    }
    SD3078_FUNC_END;
}

/******************************时间报警操作区**********************************/
SD3078_RET SD3078_AlarmLoad(SD3078_NOARG)//读取报警镜像（07H~0EH：时间7字节 + 报警允许）
{
    uint8_t en;
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    /* 时间部分复用统一时间读取（0x07~0x0D），报警允许位 0x0E 单独读 */
    if(sd3078_time_read(SD3078_STRG_ALARM_SEC, &SD3078_Status.alarm_time) != I2C_OK
       || SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_CTRG_ALARM_EN, &en, 1, (uint8_t*)&SD3078_Status.flag) != I2C_OK)
    {
        SD3078_MARK_OFFLINE_ON_I2C_FAIL();   /* 通信失败：清 initialized → 下轮重新初始化 */
        SD3078_MUTEX_GIVE;
        SD3078_FUNC_END;
    }
    SD3078_Status.alarm_en = en;
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
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
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    /* 报警组 = 0x07~0x0D 的 7 字节同格式时间 + 0x0E 报警允许；
     * 时间部分复用同一套格式逻辑，只有"多了 en 这一字节写"是报警特有的。 */
    b[0] = SD3078_DecToBcd((uint8_t)SD3078_SEC(t));
    b[1] = SD3078_DecToBcd((uint8_t)SD3078_MIN(t));
    b[2] = SD3078_DecToBcd((uint8_t)SD3078_HOUR(t));
#if SD3078_24HOUR
    b[2] |= SD3078_HOUR_1224;
#endif
    b[3] = (uint8_t)SD3078_WEEK(t);          //星期非 BCD
    b[4] = SD3078_DecToBcd((uint8_t)SD3078_DAY(t));
    b[5] = SD3078_DecToBcd((uint8_t)SD3078_MONTH(t));
    b[6] = SD3078_DecToBcd((uint8_t)SD3078_YEAR(t));
    b[7] = en;
    SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_STRG_ALARM_SEC, b, SD3078_TIME_FIELDS + 1U, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    //每一次对时间报警允许寄存器的写入都会清INTAF为"0"
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
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
SD3078_RET SD3078_TempLoad(SD3078_NOARG)//读取温度镜像（0x16）
{
    volatile uint8_t t = 0U;         /* 读缓冲：失败时不改动镜像 */
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    if(SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_TEMP, (uint8_t*)&t, 1, (uint8_t*)&SD3078_Status.flag) != I2C_OK)
    {
        SD3078_MARK_OFFLINE_ON_I2C_FAIL();   /* 通信失败：清 initialized → 下轮 load_task 重新初始化 */
        SD3078_MUTEX_GIVE;
        SD3078_FUNC_END;
    }
    SD3078_Status.temp = (int8_t)t;
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
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
SD3078_RET SD3078_BattLoad(SD3078_NOARG)//读取电池电压镜像（1AH/1BH合成9位）
{
    volatile uint8_t ctr5 = 0U, bval = 0U;   /* 读缓冲：失败时不改动镜像 */
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    if(SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_CTR5, (uint8_t*)&ctr5, 1, (uint8_t*)&SD3078_Status.flag) != I2C_OK
       || SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_STRG_BAT_VAL, (uint8_t*)&bval, 1, (uint8_t*)&SD3078_Status.flag) != I2C_OK)
    {
        SD3078_MARK_OFFLINE_ON_I2C_FAIL();   /* 通信失败：清 initialized → 下轮 load_task 重新初始化 */
        SD3078_MUTEX_GIVE;
        SD3078_FUNC_END;
    }
    SD3078_Status.ctr5 = ctr5;
    SD3078_Status.sendbuf[0] = bval;
    //9位数据：1AH[7](BAT8_VAL)<<8 | 1BH(VBAT_VAL)，如130H=304=3.04V
    SD3078_Status.batt = ((uint16_t)(SD3078_Status.ctr5 & SD3078_CTR5_BAT8_VAL) << 1) | SD3078_Status.sendbuf[0];
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
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

/******************************用户RAM与ID操作区*******************************/
SD3078_RET SD3078_SramWrite(SD3078_ARGS(uint8_t offset, uint8_t *pdata, uint16_t len))//写用户RAM
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    if(offset + len <= SD3078_RAM_LEN)
        SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_RAM_START + offset, pdata, len, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

SD3078_RET SD3078_SramRead(SD3078_ARGS(uint8_t offset, uint8_t *pdata, uint16_t len))//读用户RAM
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    if(offset + len <= SD3078_RAM_LEN)
        SD3078_EXEC(SD3078_I2C_Receive(SD3078_I2C_ADDR, SD3078_RAM_START + offset, pdata, len, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
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
 * 注意：本函数为正式初始化（上电即调用），不配置时间/报警/倒计时等；
 *       需要温度报警等完整配置时，再调用SD3078_FullInit()。
 *       函数自带initialized判断，在线且成功后只执行一次。
 */
SD3078_RET SD3078_Init(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    if(SD3078_Status.initialized == 0)
    {
        //读取CTR1校验芯片在线（读到0xFF视为无设备/总线浮空）
        //注意：必须在任何写操作之前读——RTCF 会在上电后第一次有效写时被芯片清零，
        //      PMF/RTCF/OSF 在此一次性快照，供 SD3078_ReadPMF/RTCF/OSF 查询
        SD3078_SPAWN_ARGS(SD3078_ByteRead, SD3078_CTRG_CTR1, &SD3078_Status.ctr1);
        if(SD3078_Status.ctr1 != 0xFFU)   //读到0xFF视为无设备/总线浮空 → 不置 initialized，下轮重试
        {
            //读取芯片ID（一次性快照）
            SD3078_SPAWN_NOARG(SD3078_IDLoad);
            //解锁寄存器写入（WRTC1/2/3=1）
            SD3078_SPAWN_NOARG(SD3078_Unlock);
            //每次上电重置充电寄存器：默认不充电（由菜单/上层手动控制充电）
            SD3078_SPAWN_ARGS(SD3078_ChargeSet, SD3078_CHARGE_ENABLE, SD3078_CHARGE_RES_SEL);
            //低功耗：禁止32K输出（F32K=1）
            SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR3, SD3078_CTR3_F32K, SD3078_CTR3_F32K);
            //低功耗：禁止报警/频率/倒计时中断输出，VBAT模式下禁止INT输出（FOBAT=0）
            SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR2, SD3078_CTR2_INTFE|SD3078_CTR2_INTAE|SD3078_CTR2_INTDE|SD3078_CTR2_FOBAT, 0x00U);
            //低功耗：INT脚高阻禁止输出（INTS=00时由CTR4的INTS_E=000控制）
            SD3078_SPAWN_ARGS(SD3078_ByteModify, SD3078_CTRG_CTR4, SD3078_CTR4_INTS_E2|SD3078_CTR4_INTS_E1|SD3078_CTR4_INTS_E0, 0x00U);
            //写完成后上锁，避免误写
            SD3078_SPAWN_NOARG(SD3078_Lock);
            SD3078_Status.initialized = 1;
        }
    }
    SD3078_FUNC_END;
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

uint8_t SD3078_IsInitialized(void)//检测SD3078是否已初始化过
{
    return SD3078_Status.initialized;
}
