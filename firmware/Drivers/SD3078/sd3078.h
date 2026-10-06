/*SD3078操作库 V0.1（胚胎）
/TKWTL 2026/08/09
参考SW6306库的风格搭建，用于后续功能完善
*/
#ifndef __SD3078_H__
#define __SD3078_H__

#ifdef __cplusplus
extern C {
#endif

#include "stdint.h"
    
/******************************用户设置区开始**********************************/
// 定义 SD3078_USE_PROTOTHREAD 以启用协作式挂起（protothread + coroOS）。
//  - 未定义：阻塞式 API（返回类型为 void）。
//  - 已定义：协作式 API（返回类型为 char，并额外带 struct pt *pt 参数）。
//#define SD3078_USE_PROTOTHREAD

/* I²C 通信失败 → 清 initialized（不再维护 online 字段）：
 *  - initialized=0 使 SD3078_IsInitialized() 返回 0，load_task 下一轮就会重新
 *    SD3078_Init()（CTR1 校验通过后重新置 1），即"通信失败 → 自动重初始化"。 */
#ifndef SD3078_MARK_OFFLINE_ON_I2C_FAIL
#define SD3078_MARK_OFFLINE_ON_I2C_FAIL()   do { SD3078_Status.initialized = 0; } while(0)
#endif

/*包含自己的I2C驱动库*/
#include "bsp_i2c.h"
        
//外部库给出的I2C读写函数
#ifdef SD3078_USE_PROTOTHREAD   //允许挂起    
    #define SD3078_I2C_Transmit(addr,reg,pdata,len,pflag)   ASYNC_I2C_Transmit(addr,reg,pdata,len,0,pflag)
    #define SD3078_I2C_Receive(addr,reg,pdata,len,pflag)    ASYNC_I2C_Receive(addr,reg,pdata,len,0,pflag)    
#else                           //不允许挂起
    #define SD3078_I2C_Transmit(addr,reg,pdata,len,pflag)   I2C_RegWrite(addr, reg, pdata, len)
    #define SD3078_I2C_Receive(addr,reg,pdata,len,pflag)    I2C_RegRead(addr, reg, pdata, len)
#endif

//充电功能设置（SD3078 内置 VBAT 充电电路）
//默认不充电：上电初始化写「禁止充电」；需要充电时由上层/菜单手动或自动调用 SD3078_ChargeSet() 使能
//  - 5kΩ限流+使能 = 0x81H；2kΩ限流+使能 = 0x82H（手册推荐值）
#define SD3078_CHARGE_ENABLE        0           //1=使能充电，0=禁止充电（默认不充电，由上层手动/自动控制）
#define SD3078_CHARGE_RES_SEL       1           //充电限流电阻选择：0=10kΩ，1=5kΩ，2=2kΩ，3=断开（使能充电时生效）

//温度报警阈值（单位：°C，带符号，仅使能温度报警时有效）
#define SD3078_TEMP_ALARM_LOW       (-10)       //低温报警值（小于等于此值报警）
#define SD3078_TEMP_ALARM_HIGH      60          //高温报警值（大于等于此值报警）

//时间制式（1=24小时制，0=12小时制）
#define SD3078_24HOUR               1

/******************************用户设置区结束**********************************/
//操作语法宏，方便添加freeRTOS之类的支持
#ifdef SD3078_USE_PROTOTHREAD
    #define SD3078_RET          char
    #define SD3078_NOARG        struct pt *pt
    #define SD3078_ARGS(...)    struct pt *pt, __VA_ARGS__
    #define SD3078_EXEC(cond)   if(cond == 0) THRD_YIELD                        //反复执行某函数直到返回1
    #define SD3078_UNTIL(cond)  THRD_UNTIL(cond)                                //条件不满足时出让CPU
    #define SD3078_SPAWN_NOARG(func)\
                                THRD_SPAWN_NOARG(func)
    #define SD3078_SPAWN_ARGS(func,...)\
                                THRD_SPAWN_ARGS(func, __VA_ARGS__)              //调用子线程/函数语句
    #define SD3078_FUNC_BEGIN   THRD_BEGIN
    #define SD3078_FUNC_END     THRD_END
    #define SD3078_MUTEX_TAKE   PT_SEM_WAIT(pt, &i2c_mutex)
    #define SD3078_MUTEX_GIVE   PT_SEM_SIGNAL(pt, &i2c_mutex)   
#else
    #define SD3078_RET          void
    #define SD3078_NOARG        void
    #define SD3078_ARGS(...)    __VA_ARGS__
    #define SD3078_EXEC(cond)   cond
    #define SD3078_UNTIL(cond)  {}
    #define SD3078_SPAWN_NOARG(func)\
                                func()
    #define SD3078_SPAWN_ARGS(func,...)\
                                func(__VA_ARGS__)
    #define SD3078_FUNC_BEGIN   {}
    #define SD3078_FUNC_END     {}
    #define SD3078_MUTEX_TAKE   xSemaphoreTake(mutex_i2c_handle, portMAX_DELAY)
    #define SD3078_MUTEX_GIVE   xSemaphoreGive(mutex_i2c_handle)
#endif    

/* ==================== 统一时间结构体 ====================
 * 参考 NUEDC framework 的 time.c/.h（统一时间结构体，避免各驱动各写一套 set/get）：
 *  - 字段顺序统一为「秒 分 时 周 日 月 年」，正好等于 SD3078 四组时间寄存器
 *    的物理顺序，因此同一套收发函数可服务全部四组：
 *      0x00~0x06  RTC 时间
 *      0x07~0x0D  报警时间（0x0E 报警允许另放，不在本结构体内）
 *      0x20~0x26  历史最低温发生时间
 *      0x27~0x2D  历史最高温发生时间
 *  - 收发走字节池 b[]，用下面的 SEC/MIN/... 宏逐字节取用（不依赖编译器字节序、
 *    不用匿名 union 的类型双关），字段顺序仍只在这一处定义。 */
#define SD3078_TIME_FIELDS  7

/* 全部字段都是 uint8_t 且按寄存器顺序排列，因此结构体恰好 7 字节、无填充、
 * 对齐为 1，既可 memcpy，也可用下面的宏按固定下标当 b[] 用。 */
typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t week;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} sd3078_time_t;

/* 按寄存器顺序取字段（0=秒 … 6=年）：b[] 视图。
 * 统一转成 uint8_t* 再按固定下标取，故调用点传值（SD3078_Status.time_dec）
 * 和传指针（函数内的 sd3078_time_t *t、&t）都是同一写法。 */
#define SD3078_TIME_B(t)      ((uint8_t *)(t))
#define SD3078_SEC(t)         (SD3078_TIME_B(t)[0])
#define SD3078_MIN(t)         (SD3078_TIME_B(t)[1])
#define SD3078_HOUR(t)        (SD3078_TIME_B(t)[2])
#define SD3078_WEEK(t)        (SD3078_TIME_B(t)[3])
#define SD3078_DAY(t)         (SD3078_TIME_B(t)[4])
#define SD3078_MONTH(t)       (SD3078_TIME_B(t)[5])
#define SD3078_YEAR(t)        (SD3078_TIME_B(t)[6])

struct SD3078_StatusTypedef
{
    uint8_t initialized;                                                        //SD3078已初始化（CTR1 校验通过）；通信失败时清 0 触发重新初始化
    uint8_t unlocked;                                                           //SD3078已解锁（WRTC1/2/3=1），此时寄存器可写
    uint8_t flag;                                                               //标识传输完成与传输状态用变量
    uint8_t sendbuf[8];                                                         //传输缓冲用变量
    
/***************************寄存器内存镜像声明*********************************/
    /* 时间：统一结构体，BCD/十进制两套镜像由 sd3078_time_read/write 统一维护 */
    sd3078_time_t time;             //0x00~0x06 实时时钟（BCD 原始值）
    sd3078_time_t time_dec;         //0x00~0x06 实时时钟（十进制镜像，供 SD3078_Read*() 返回）

    //时间报警镜像（0x07~0x0D 用统一时间结构体，0x0E 报警允许单独放）
    sd3078_time_t alarm_time;       //0x07~0x0D 报警时间（BCD 原始值）
    uint8_t alarm_en;               //0x0E 报警允许

    //控制寄存器存档
    uint8_t ctr1;                   //0x0F 控制寄存器1（含WRTC3/2、OSF、INTAF、INTDF、BLF、PMF、RTCF）
    uint8_t ctr2;                   //0x10 控制寄存器2（含WRTC1、IM、INTS、FOBAT、INTDE/AE/FE）
    uint8_t ctr3;                   //0x11 控制寄存器3（含ARST、F32K、TDS、FS）
    uint8_t ctr4;                   //0x19 扩展控制寄存器
    uint8_t ctr5;                   //0x1A 扩展状态寄存器（BAT8_VAL、BHF、BLF）
    uint8_t charge;                 //0x18 充电寄存器

    //测量数据区
    int8_t temp;                    //0x16 温度（补码，单位°C）
    uint16_t batt;                  //0x1A/0x1B 电池电压（9位，单位0.01V，如130H=3.04V）

    //芯片ID（只读）
    uint8_t id[8];                  //0x72~0x79 芯片唯一身份识别码

    //时间设置请求（UI 经 SD3078_RequestTimeSet 写入，load_task 经 SD3078_TimeSetProcess 提交）
    uint8_t time_set_pending;        //1=有待提交的时间设置
    uint8_t set_sec;                 //目标秒（十进制）
    uint8_t set_min;                 //目标分（十进制）
    uint8_t set_hour;                //目标时（十进制）
    uint8_t set_day;                 //目标日（十进制）
    uint8_t set_month;               //目标月（十进制）
    uint8_t set_year;                //目标年（十进制，0~99）
};

//SD3078 I2C 地址，器件代码为7位"0110010"(0x32)，此处为左移一位后的8位写地址
#ifndef SD3078_I2C_ADDR
#define SD3078_I2C_ADDR                 0x64U
#endif

/**************************SD3078 寄存器地址定义*******************************/
//命名规则：固定前缀(SD3078)_状态(ST)/控制(CT)+寄存器(RG)_(功能描述)
//实时时钟寄存器（00H~06H，BCD码，写时必须一次性写满7字节）
#define SD3078_STRG_SEC                 0x00U//秒
#define SD3078_STRG_MIN                 0x01U//分钟
#define SD3078_STRG_HOUR                0x02U//小时（bit7=12_/24制式选择）
#define SD3078_STRG_WEEK                0x03U//星期（0~6）
#define SD3078_STRG_DAY                 0x04U//日
#define SD3078_STRG_MONTH               0x05U//月
#define SD3078_STRG_YEAR                0x06U//年（00~99）

//时间报警寄存器（07H~0EH）
#define SD3078_STRG_ALARM_SEC           0x07U//秒报警
#define SD3078_STRG_ALARM_MIN           0x08U//分钟报警
#define SD3078_STRG_ALARM_HOUR          0x09U//小时报警
#define SD3078_STRG_ALARM_WEEK          0x0AU//星期报警
#define SD3078_STRG_ALARM_DAY           0x0BU//日报警
#define SD3078_STRG_ALARM_MONTH         0x0CU//月报警
#define SD3078_STRG_ALARM_YEAR          0x0DU//年报警
#define SD3078_CTRG_ALARM_EN            0x0EU//报警允许

//控制寄存器（0FH~1AH）
#define SD3078_CTRG_CTR1                0x0FU//控制寄存器1
#define SD3078_CTRG_CTR2                0x10U//控制寄存器2
#define SD3078_CTRG_CTR3                0x11U//控制寄存器3
#define SD3078_STRG_25C_TTF             0x12U//25℃温补值（只读）
#define SD3078_CTRG_TD0                 0x13U//倒计时定时器低8位
#define SD3078_CTRG_TD1                 0x14U//倒计时定时器中8位
#define SD3078_CTRG_TD2                 0x15U//倒计时定时器高8位
#define SD3078_STRG_TEMP                0x16U//温度（补码）
#define SD3078_CTRG_IIC_CTL             0x17U//IIC控制寄存器（AGTC、BATIIC）
#define SD3078_CTRG_CHARGE              0x18U//充电寄存器
#define SD3078_CTRG_CTR4                0x19U//扩展控制寄存器
#define SD3078_STRG_CTR5                0x1AU//扩展状态寄存器
#define SD3078_STRG_BAT_VAL             0x1BU//电池电量低8位（与1AH的bit7合成9位）

//温度报警与历史温度
#define SD3078_CTRG_TEMP_AL             0x1CU//低温报警值
#define SD3078_CTRG_TEMP_AH             0x1DU//高温报警值
#define SD3078_STRG_TEMP_HIS_L          0x1EU//历史最低温度（bit7为符号位）
#define SD3078_STRG_TEMP_HIS_H          0x1FU//历史最高温度（bit7为符号位）

//历史温度发生时间（20H~25H为历史最低温，26H~2BH为历史最高温，均为分钟/时/星期/日/月/年）
#define SD3078_STRG_TL_MIN              0x20U//历史最低温-分钟
#define SD3078_STRG_TL_HOUR             0x21U//历史最低温-小时
#define SD3078_STRG_TL_WEEK             0x22U//历史最低温-星期
#define SD3078_STRG_TL_DAY              0x23U//历史最低温-日
#define SD3078_STRG_TL_MONTH            0x24U//历史最低温-月
#define SD3078_STRG_TL_YEAR             0x25U//历史最低温-年
#define SD3078_STRG_TH_MIN              0x26U//历史最高温-分钟
#define SD3078_STRG_TH_HOUR             0x27U//历史最高温-小时
#define SD3078_STRG_TH_WEEK             0x28U//历史最高温-星期
#define SD3078_STRG_TH_DAY              0x29U//历史最高温-日
#define SD3078_STRG_TH_MONTH            0x2AU//历史最高温-月
#define SD3078_STRG_TH_YEAR             0x2BU//历史最高温-年

//用户RAM与ID
#define SD3078_RAM_START                0x2CU//用户RAM起始地址（70字节）
#define SD3078_RAM_END                  0x71U//用户RAM结束地址
#define SD3078_RAM_LEN                  70U  //用户RAM长度
#define SD3078_STRG_ID                  0x72U//芯片ID起始地址（8字节，只读）

/******************************寄存器位定义************************************/

//0x0F  SD3078_CTRG_CTR1               控制寄存器1
#define SD3078_CTR1_WRTC3               0x80U//写保护位3（1=允许写）
#define SD3078_CTR1_OSF                 0x40U//停振标志位（1=曾经停振）
#define SD3078_CTR1_INTAF               0x20U//报警中断标志（写0清除或读自动清除）
#define SD3078_CTR1_INTDF               0x10U//倒计时中断标志
#define SD3078_CTR1_BLF                 0x08U//电池电压欠压标志（低于2.2V）
#define SD3078_CTR1_WRTC2               0x04U//写保护位2（1=允许写）
#define SD3078_CTR1_PMF                 0x02U//电源模式标志（0=VDD，1=VBAT）
#define SD3078_CTR1_RTCF                0x01U//上电标志（全部电源失效后再上电置1）

//0x10  SD3078_CTRG_CTR2               控制寄存器2
#define SD3078_CTR2_WRTC1               0x80U//写保护位1（1=允许写）
#define SD3078_CTR2_IM                  0x40U//报警中断模式（0=单事件，1=周期性250ms脉冲）
#define SD3078_CTR2_INTS1               0x20U//INT脚中断选择位1
#define SD3078_CTR2_INTS0               0x10U//INT脚中断选择位0
#define SD3078_CTR2_FOBAT               0x08U//VBAT模式下INT输出允许
#define SD3078_CTR2_INTDE               0x04U//倒计时中断使能
#define SD3078_CTR2_INTAE               0x02U//报警中断使能
#define SD3078_CTR2_INTFE               0x01U//频率中断使能

//INT脚中断输出选通表（INTS1/INTS0）
#define SD3078_CTR2_INTS_BATT           0x00U//电量报警输出
#define SD3078_CTR2_INTS_ALARM          0x10U//报警中断输出
#define SD3078_CTR2_INTS_FREQ           0x20U//频率中断输出
#define SD3078_CTR2_INTS_COUNT          0x30U//倒计时中断输出

//0x11  SD3078_CTRG_CTR3               控制寄存器3
#define SD3078_CTR3_ARST                0x80U//自动复位使能（读CTR1时自动清INTAF/INTDF）
#define SD3078_CTR3_F32K                0x40U//32K输出控制（0=允许输出，1=禁止输出）
#define SD3078_CTR3_TDS1                0x20U//倒计时时钟源选择1
#define SD3078_CTR3_TDS0                0x10U//倒计时时钟源选择0
#define SD3078_CTR3_FS_MSK              0x0FU//频率中断选择有效位

//倒计时时钟源选择（TDS1/TDS0）
#define SD3078_CTR3_TDS_4096HZ          0x00U//4096Hz
#define SD3078_CTR3_TDS_1024HZ          0x10U//1024Hz
#define SD3078_CTR3_TDS_1S              0x20U//1秒
#define SD3078_CTR3_TDS_1MIN            0x30U//1分钟

//频率中断输出选择（FS3~FS0）
#define SD3078_CTR3_FS_OFF              0x00U//关闭
#define SD3078_CTR3_FS_4096HZ           0x02U//4096Hz
#define SD3078_CTR3_FS_1024HZ           0x03U//1024Hz
#define SD3078_CTR3_FS_64HZ             0x04U//64Hz
#define SD3078_CTR3_FS_32HZ             0x05U//32Hz
#define SD3078_CTR3_FS_16HZ             0x06U//16Hz
#define SD3078_CTR3_FS_8HZ              0x07U//8Hz
#define SD3078_CTR3_FS_4HZ              0x08U//4Hz
#define SD3078_CTR3_FS_2HZ              0x09U//2Hz
#define SD3078_CTR3_FS_1HZ              0x0AU//1Hz
#define SD3078_CTR3_FS_HALF_HZ          0x0BU//1/2Hz
#define SD3078_CTR3_FS_QUARTER_HZ       0x0CU//1/4Hz
#define SD3078_CTR3_FS_1_8_HZ           0x0DU//1/8Hz
#define SD3078_CTR3_FS_1_16_HZ          0x0EU//1/16Hz
#define SD3078_CTR3_FS_1SEC             0x0FU//1秒（500ms低/500ms高）

//0x0E  SD3078_CTRG_ALARM_EN           报警允许
#define SD3078_ALARMEN_MSK              0x7FU//报警允许寄存器有效位
#define SD3078_ALARMEN_EAY              0x40U//年报警允许
#define SD3078_ALARMEN_EAMO             0x20U//月报警允许
#define SD3078_ALARMEN_EAD              0x10U//日报警允许
#define SD3078_ALARMEN_EAW              0x08U//星期报警允许
#define SD3078_ALARMEN_EAH              0x04U//小时报警允许
#define SD3078_ALARMEN_EAMN             0x02U//分钟报警允许
#define SD3078_ALARMEN_EAS              0x01U//秒报警允许

//0x02  SD3078_STRG_HOUR               小时寄存器位定义
#define SD3078_HOUR_1224                0x80U//12_/24制式选择（1=24小时制）
#define SD3078_HOUR_AMP                 0x20U//12小时制AM/PM指示（0=AM，1=PM）
#define SD3078_HOUR_MSK                 0x3FU//小时数据有效位（24小时制屏蔽bit7/bit6）

//0x03  SD3078_STRG_WEEK               星期寄存器位定义
#define SD3078_WEEK_MSK                 0x07U//星期数据有效位（0~6，0=星期日）

//0x17  SD3078_CTRG_IIC_CTL            IIC控制寄存器
#define SD3078_IIC_AGTC                 0x80U//IIC通信自动复位功能使能
#define SD3078_IIC_BATIIC               0x40U//VBAT模式下允许IIC通信（上电默认0=禁止）

//0x18  SD3078_CTRG_CHARGE             充电寄存器
#define SD3078_CHARGE_ENCH              0x80U//充电功能使能
#define SD3078_CHARGE_RES_MSK           0x03U//充电限流电阻选择有效位
#define SD3078_CHARGE_RES_10K           0x00U//10kΩ
#define SD3078_CHARGE_RES_5K            0x01U//5kΩ
#define SD3078_CHARGE_RES_2K            0x02U//2kΩ
#define SD3078_CHARGE_RES_OFF           0x03U//断开（不充电）

//0x19  SD3078_CTRG_CTR4               扩展控制寄存器
#define SD3078_CTR4_INTS_E2             0x80U//扩展中断选择位2
#define SD3078_CTR4_INTS_E1             0x40U//扩展中断选择位1
#define SD3078_CTR4_INTS_E0             0x20U//扩展中断选择位0
#define SD3078_CTR4_INTTHE              0x08U//高温报警使能（>=TEMP_AH报警）
#define SD3078_CTR4_INTTLE              0x04U//低温报警使能（<=TEMP_AL报警）
#define SD3078_CTR4_INTBHE              0x02U//电池高压报警使能（>=3.3V置BHF）
#define SD3078_CTR4_INTBLE              0x01U//电池低压报警使能（<=2.2V置BLF）

//0x1A  SD3078_STRG_CTR5               扩展状态寄存器
#define SD3078_CTR5_BAT8_VAL            0x80U//电池测量结果最高位（与1BH合成9位）
#define SD3078_CTR5_BHF                 0x02U//电池电压高压标志位
#define SD3078_CTR5_BLF                 0x01U//电池电压欠压标志位

/*****************************函数声明区***************************************/
//基本操作
SD3078_RET SD3078_ByteWrite(SD3078_ARGS(uint8_t reg, uint8_t data));     //写单字节
SD3078_RET SD3078_ByteRead(SD3078_ARGS(uint8_t reg, volatile uint8_t *data));//读单字节
SD3078_RET SD3078_BytesRead(SD3078_ARGS(uint8_t reg, uint8_t *pdata, uint16_t len));//连续读
SD3078_RET SD3078_ByteModify(SD3078_ARGS(uint8_t reg, uint8_t mask, uint8_t data));//读-改-写

//写保护操作（SD3078 必须三写保护位全1才能写寄存器）
SD3078_RET SD3078_Unlock(SD3078_NOARG);       //解锁（WRTC1=1 → WRTC2/3=1），写寄存器前调用
SD3078_RET SD3078_Lock(SD3078_NOARG);         //上锁（WRTC2/3=0 → WRTC1=0），写完寄存器后调用

//实时时钟操作
SD3078_RET SD3078_TimeLoad(SD3078_NOARG);     //读取时间镜像（一次性读00H~06H共7字节，硬件锁存防错读；同时保留BCD镜像并转换为十进制）
uint8_t SD3078_ReadSec(void);                  //读取秒（十进制，TimeLoad后有效）
uint8_t SD3078_ReadMin(void);                  //读取分钟（十进制）
uint8_t SD3078_ReadHour(void);                 //读取小时（十进制）
uint8_t SD3078_ReadWeek(void);                 //读取星期（十进制，0~6，0=星期日）
uint8_t SD3078_ReadDay(void);                  //读取日（十进制）
uint8_t SD3078_ReadMonth(void);                //读取月（十进制）
uint8_t SD3078_ReadYear(void);                 //读取年（十进制，0~99）
//BCD后缀API：直接提取原始BCD镜像（保留寄存器原值）
uint8_t SD3078_ReadSecBCD(void);               //读取秒（原始BCD镜像）
uint8_t SD3078_ReadMinBCD(void);               //读取分钟（原始BCD镜像）
uint8_t SD3078_ReadHourBCD(void);              //读取小时（原始BCD镜像）
uint8_t SD3078_ReadWeekBCD(void);              //读取星期（原始镜像）
uint8_t SD3078_ReadDayBCD(void);               //读取日（原始BCD镜像）
uint8_t SD3078_ReadMonthBCD(void);             //读取月（原始BCD镜像）
uint8_t SD3078_ReadYearBCD(void);              //读取年（原始BCD镜像）
uint8_t SD3078_BcdToDec(uint8_t bcd);          //BCD → 十进制（时间/日期寄存器为 BCD 码）
uint8_t SD3078_DecToBcd(uint8_t dec);          //十进制 → BCD（时间/日期寄存器为 BCD 码）
SD3078_RET SD3078_TimeSetDec(SD3078_ARGS(const sd3078_time_t *t));//一次性写7字节时间（t 传十进制值，内部转 BCD 并处理 12_/24 位）
SD3078_RET SD3078_RequestTimeSet(SD3078_ARGS(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec));//请求设置时间（十进制输入，写句柄待 load_task 提交）
SD3078_RET SD3078_TimeSetProcess(SD3078_NOARG);    //检测并提交时间设置请求（load_task 0.5s 周期调用）

//时间报警操作
SD3078_RET SD3078_AlarmLoad(SD3078_NOARG);     //读取报警镜像（0x07~0x0D 时间 + 0x0E 报警允许）
SD3078_RET SD3078_AlarmSetDec(SD3078_ARGS(const sd3078_time_t *t, uint8_t en));//设置报警（t 传十进制值，en 为报警允许位）
SD3078_RET SD3078_AlarmClear(SD3078_NOARG);    //清除报警中断标志（INTAF写0）
uint8_t SD3078_HasAlarm(void);                 //查询报警中断标志（INTAF）

//温度操作
SD3078_RET SD3078_TempLoad(SD3078_NOARG);      //读取温度镜像（0x16）
int8_t SD3078_ReadTemp(void);                  //读取温度（°C，补码）
SD3078_RET SD3078_TempAlarmSet(SD3078_ARGS(int8_t low, int8_t high));//设置高低温报警阈值
SD3078_RET SD3078_TempHistoryLoad(SD3078_NOARG);//读取历史高低温值（0x1E/0x1F）

//电池电压与充电操作
SD3078_RET SD3078_BattLoad(SD3078_NOARG);      //读取电池电压镜像（1AH/1BH合成9位）
uint16_t SD3078_ReadBatt(void);                //读取电池电压（单位：mV，如3040）
uint8_t SD3078_IsBattLow(void);                //电池欠压标志（BLF）
uint8_t SD3078_IsBattHigh(void);               //电池高压标志（BHF）
SD3078_RET SD3078_ChargeSet(SD3078_ARGS(uint8_t enable, uint8_t res_sel));//设置充电功能与限流电阻
//上电状态标志（初始化时一次性快照，不随状态页刷新）
uint8_t SD3078_ReadPMF(void);                  //电源模式标志（0=VDD主电源，1=VBAT后备电池供电）
uint8_t SD3078_ReadRTCF(void);                 //上电标志（1=曾发生全部电源失效后再上电；首次有效写后芯片清零）
uint8_t SD3078_ReadOSF(void);                  //停振标志（1=内部振荡器曾停振）

//倒计时定时器操作
SD3078_RET SD3078_CountdownSet(SD3078_ARGS(uint32_t count, uint8_t src));//设置24位倒计数值与时钟源（须先禁止后使能INTDE生效）
SD3078_RET SD3078_CountdownEnable(SD3078_ARGS(uint8_t enable));//使能/禁止倒计时中断
uint8_t SD3078_HasCountdown(void);             //倒计时中断标志（INTDF）

//频率/32K输出操作
SD3078_RET SD3078_FreqOutSet(SD3078_ARGS(uint8_t fs));//设置INT脚频率中断输出频率（SD3078_CTR3_FS_*）
SD3078_RET SD3078_F32KSet(SD3078_ARGS(uint8_t enable));//32K输出控制（1=允许输出）

//用户RAM与ID操作
SD3078_RET SD3078_SramWrite(SD3078_ARGS(uint8_t offset, uint8_t *pdata, uint16_t len));//写用户RAM（offset 0~69）
SD3078_RET SD3078_SramRead(SD3078_ARGS(uint8_t offset, uint8_t *pdata, uint16_t len));//读用户RAM
SD3078_RET SD3078_IDLoad(SD3078_NOARG);        //读取芯片ID（72H~79H共8字节）
uint8_t SD3078_ReadID(uint8_t idx);            //读取芯片UID单字节（idx 0~7，取初始化时读到的镜像，不发 I2C）

//初始化
SD3078_RET SD3078_Init(SD3078_NOARG);          //正式初始化（上电即调用一次：在线校验+ID/PMF/RTCF/OSF快照+默认不充电+低功耗），自带initialized判断
SD3078_RET SD3078_FullInit(SD3078_NOARG);      //完整初始化（在SD3078_Init基础上追加温度报警阈值等完整配置）
uint8_t SD3078_IsInitialized(void);            //检测SD3078是否已初始化过

#ifdef __cplusplus
}
#endif

#endif
