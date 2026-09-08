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

/******************************实时时钟操作区**********************************/
/*读取实时时钟
/当芯片收到读实时时钟数据命令时，所有实时时钟数据被锁存（走时不受影响），可避免错读
/因此一次性读取00H~06H共7字节
*/
SD3078_RET SD3078_TimeLoad(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_BytesRead, SD3078_STRG_SEC, &SD3078_Status.sec, 7);
    /* 24小时制下屏蔽小时寄存器12_/24位（作用于BCD镜像） */
    SD3078_Status.hour &= ~SD3078_HOUR_1224;
    /* 保留原始BCD镜像的同时转换为十进制，SD3078_Read*()直接返回十进制 */
    SD3078_Status.sec_dec   = SD3078_BcdToDec(SD3078_Status.sec);
    SD3078_Status.min_dec   = SD3078_BcdToDec(SD3078_Status.min);
    SD3078_Status.hour_dec  = SD3078_BcdToDec(SD3078_Status.hour);
    SD3078_Status.week_dec  = (uint8_t)(SD3078_Status.week & SD3078_WEEK_MSK); //星期非BCD，直接取低3位
    SD3078_Status.day_dec   = SD3078_BcdToDec(SD3078_Status.day);
    SD3078_Status.month_dec = SD3078_BcdToDec(SD3078_Status.month);
    SD3078_Status.year_dec  = SD3078_BcdToDec(SD3078_Status.year);
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}
uint8_t SD3078_ReadSec(void)//读取秒（十进制，TimeLoad后有效）
{
    return SD3078_Status.sec_dec;
}
uint8_t SD3078_ReadMin(void)//读取分钟（十进制）
{
    return SD3078_Status.min_dec;
}
uint8_t SD3078_ReadHour(void)//读取小时（十进制）
{
    return SD3078_Status.hour_dec;
}
uint8_t SD3078_ReadWeek(void)//读取星期（十进制，0~6，0=星期日）
{
    return SD3078_Status.week_dec;
}
uint8_t SD3078_ReadDay(void)//读取日（十进制）
{
    return SD3078_Status.day_dec;
}
uint8_t SD3078_ReadMonth(void)//读取月（十进制）
{
    return SD3078_Status.month_dec;
}
uint8_t SD3078_ReadYear(void)//读取年（十进制，0~99）
{
    return SD3078_Status.year_dec;
}
/* BCD后缀API：直接提取原始BCD镜像（保留寄存器原值） */
uint8_t SD3078_ReadSecBCD(void)//读取秒（原始BCD镜像）
{
    return SD3078_Status.sec;
}
uint8_t SD3078_ReadMinBCD(void)//读取分钟（原始BCD镜像）
{
    return SD3078_Status.min;
}
uint8_t SD3078_ReadHourBCD(void)//读取小时（原始BCD镜像）
{
    return SD3078_Status.hour;
}
uint8_t SD3078_ReadWeekBCD(void)//读取星期（原始镜像）
{
    return SD3078_Status.week;
}
uint8_t SD3078_ReadDayBCD(void)//读取日（原始BCD镜像）
{
    return SD3078_Status.day;
}
uint8_t SD3078_ReadMonthBCD(void)//读取月（原始BCD镜像）
{
    return SD3078_Status.month;
}
uint8_t SD3078_ReadYearBCD(void)//读取年（原始BCD镜像）
{
    return SD3078_Status.year;
}
uint8_t SD3078_BcdToDec(uint8_t bcd)//BCD → 十进制（时间/日期寄存器为 BCD 码）
{
    return (uint8_t)(((bcd >> 4) & 0x0FU) * 10U + (bcd & 0x0FU));
}
uint8_t SD3078_DecToBcd(uint8_t dec)//十进制 → BCD（时间/日期寄存器为 BCD 码）
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

/*设置实时时钟（BCD 输入）
/写实时时间数据时不可以单独写其中某一位，必须一次性写入全部7个实时时钟数据（00H~06H），
/否则可能引起时间数据错误进位
/hour为BCD码，24小时制下需要置12_/24位（本库根据SD3078_24HOUR宏自动处理）
*/
SD3078_RET SD3078_TimeSetBCD(SD3078_ARGS(uint8_t year, uint8_t month, uint8_t day, uint8_t week, uint8_t hour, uint8_t min, uint8_t sec))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
#if SD3078_24HOUR
    hour |= SD3078_HOUR_1224;
#endif
    SD3078_Status.sendbuf[0] = sec;
    SD3078_Status.sendbuf[1] = min;
    SD3078_Status.sendbuf[2] = hour;
    SD3078_Status.sendbuf[3] = week;
    SD3078_Status.sendbuf[4] = day;
    SD3078_Status.sendbuf[5] = month;
    SD3078_Status.sendbuf[6] = year;
    SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_STRG_SEC, SD3078_Status.sendbuf, 7, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    //写秒寄存器时会对秒以下内部计数器清零，实现时间同步
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/*设置实时时钟（二进制/十进制输入，内部转 BCD）
/写实时时间数据时不可以单独写其中某一位，必须一次性写入全部7个实时时钟数据（00H~06H），
/否则可能引起时间数据错误进位
/hour为十进制（0~23），24小时制下需要置12_/24位（本库根据SD3078_24HOUR宏自动处理）
*/
SD3078_RET SD3078_TimeSetDec(SD3078_ARGS(uint8_t year, uint8_t month, uint8_t day, uint8_t week, uint8_t hour, uint8_t min, uint8_t sec))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_Status.sendbuf[0] = SD3078_DecToBcd(sec);
    SD3078_Status.sendbuf[1] = SD3078_DecToBcd(min);
    SD3078_Status.sendbuf[2] = SD3078_DecToBcd(hour);
    SD3078_Status.sendbuf[3] = week;
    SD3078_Status.sendbuf[4] = SD3078_DecToBcd(day);
    SD3078_Status.sendbuf[5] = SD3078_DecToBcd(month);
    SD3078_Status.sendbuf[6] = SD3078_DecToBcd(year);
#if SD3078_24HOUR
    SD3078_Status.sendbuf[2] |= SD3078_HOUR_1224;
#endif
    SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_STRG_SEC, SD3078_Status.sendbuf, 7, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
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

/* 检测并提交时间设置请求（load_task 0.5s 周期调用；星期保留句柄镜像当前值 week_dec）
 * 内部依次调用 Unlock/TimeSetDec/Lock（各含互斥），自身不持锁，避免嵌套死锁 */
SD3078_RET SD3078_TimeSetProcess(SD3078_NOARG)
{
    SD3078_FUNC_BEGIN;
    if (SD3078_Status.time_set_pending) {
        SD3078_SPAWN_ARGS(SD3078_Unlock);
        SD3078_SPAWN_ARGS(SD3078_TimeSetDec, SD3078_Status.set_year, SD3078_Status.set_month,
                          SD3078_Status.set_day, SD3078_Status.week_dec,
                          SD3078_Status.set_hour, SD3078_Status.set_min, SD3078_Status.set_sec);
        SD3078_SPAWN_ARGS(SD3078_Lock);
        SD3078_Status.time_set_pending = 0;
    }
    SD3078_FUNC_END;
}

/******************************时间报警操作区**********************************/
SD3078_RET SD3078_AlarmLoad(SD3078_NOARG)//读取报警镜像（07H~0EH共8字节）
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_BytesRead, SD3078_STRG_ALARM_SEC, &SD3078_Status.alarm_sec, 8);
    SD3078_Status.alarm_hour &= ~SD3078_HOUR_1224;
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

/*设置时间报警
/en为报警允许位（SD3078_ALARMEN_*的按位或），如：
/  SD3078_ALARMEN_EAS|SD3078_ALARMEN_EAMN|SD3078_ALARMEN_EAH  每天固定时分秒报警
/  SD3078_ALARMEN_EAD|SD3078_ALARMEN_EAMO|SD3078_ALARMEN_EAY  每年固定日期报警
/注意：日报警与星期报警同时允许时只有日报警有效
*/
SD3078_RET SD3078_AlarmSetBCD(SD3078_ARGS(uint8_t year, uint8_t month, uint8_t day, uint8_t week, uint8_t hour, uint8_t min, uint8_t sec, uint8_t en))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
#if SD3078_24HOUR
    hour |= SD3078_HOUR_1224;
#endif
    SD3078_Status.sendbuf[0] = sec;
    SD3078_Status.sendbuf[1] = min;
    SD3078_Status.sendbuf[2] = hour;
    SD3078_Status.sendbuf[3] = week;
    SD3078_Status.sendbuf[4] = day;
    SD3078_Status.sendbuf[5] = month;
    SD3078_Status.sendbuf[6] = year;
    SD3078_Status.sendbuf[7] = en;
    SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_STRG_ALARM_SEC, SD3078_Status.sendbuf, 8, (uint8_t*)&SD3078_Status.flag));
    SD3078_UNTIL(SD3078_Status.flag);
    //每一次对时间报警允许寄存器的写入都会清INTAF为"0"
    SD3078_MUTEX_GIVE;
    SD3078_FUNC_END;
}

SD3078_RET SD3078_AlarmSetDec(SD3078_ARGS(uint8_t year, uint8_t month, uint8_t day, uint8_t week, uint8_t hour, uint8_t min, uint8_t sec, uint8_t en))
{
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_Status.sendbuf[0] = SD3078_DecToBcd(sec);
    SD3078_Status.sendbuf[1] = SD3078_DecToBcd(min);
    SD3078_Status.sendbuf[2] = SD3078_DecToBcd(hour);
    SD3078_Status.sendbuf[3] = week;
    SD3078_Status.sendbuf[4] = SD3078_DecToBcd(day);
    SD3078_Status.sendbuf[5] = SD3078_DecToBcd(month);
    SD3078_Status.sendbuf[6] = SD3078_DecToBcd(year);
    SD3078_Status.sendbuf[7] = en;
#if SD3078_24HOUR
    SD3078_Status.sendbuf[2] |= SD3078_HOUR_1224;
#endif
    SD3078_EXEC(SD3078_I2C_Transmit(SD3078_I2C_ADDR, SD3078_STRG_ALARM_SEC, SD3078_Status.sendbuf, 8, (uint8_t*)&SD3078_Status.flag));
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
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteRead, SD3078_STRG_TEMP, (volatile uint8_t*)&SD3078_Status.temp);
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
    SD3078_FUNC_BEGIN;
    SD3078_MUTEX_TAKE;
    SD3078_SPAWN_ARGS(SD3078_ByteRead, SD3078_STRG_CTR5, &SD3078_Status.ctr5);
    SD3078_SPAWN_ARGS(SD3078_ByteRead, SD3078_STRG_BAT_VAL, SD3078_Status.sendbuf);
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
        SD3078_Status.online = (SD3078_Status.ctr1 == 0xFFU) ? 0 : 1;
        if(SD3078_Status.online)
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
    SD3078_Status.online = (SD3078_Status.ctr1 == 0xFFU) ? 0 : 1;//读到0xFF视为无设备（总线浮空）
    if(SD3078_Status.online)
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
