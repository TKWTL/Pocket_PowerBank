/*SC7A20操作库 V0.2（基础+高级检测）
/TKWTL 2026/08/17
参考SW6306库的风格，依据《SC7A20 驱动库开发提示文档》扩展
自由跌落/运动静止/振动检测。单一 .c/.h，不拆文件。
*/
#ifndef __SC7A20_H__
#define __SC7A20_H__

#ifdef __cplusplus
extern C {
#endif

#include "stdint.h"
    
/******************************用户设置区开始**********************************/
// 定义 SC7A20_USE_PROTOTHREAD 以启用协作式挂起（protothread + coroOS）。
//  - 未定义：阻塞式 API（返回类型为 void）。
//  - 已定义：协作式 API（返回类型为 char，并额外带 struct pt *pt 参数）。
//#define SC7A20_USE_PROTOTHREAD

/* I²C 通信失败 → 清 initialized（不再维护 online 字段）：
 *  - initialized=0 使 SC7A20_IsInitialized() 返回 0，load_task 下一轮就会重新
 *    SC7A20_Init()（WHO_AM_I 校验通过后重新置 1），即"通信失败 → 自动重初始化"；
 *  - 驱动内原有的 online 守卫改为 initialized 守卫，语义由"是否通信过"统一为"是否可用"。 */
#ifndef SC7A20_MARK_OFFLINE_ON_I2C_FAIL
#define SC7A20_MARK_OFFLINE_ON_I2C_FAIL() \
    do { SC7A20_Status.initialized = 0; SC7A20_Status.last_error = SC7A20_ERR_NOT_ONLINE; } while(0)
#endif

/*包含自己的I2C驱动库*/
#include "bsp_i2c.h"
        
//外部库给出的I2C读写函数
#ifdef SC7A20_USE_PROTOTHREAD   //允许挂起    
    #define SC7A20_I2C_Transmit(addr,reg,pdata,len,pflag)   ASYNC_I2C_Transmit(addr,reg,pdata,len,0,pflag)
    #define SC7A20_I2C_Receive(addr,reg,pdata,len,pflag)    ASYNC_I2C_Receive(addr,reg,pdata,len,0,pflag)    
#else                           //不允许挂起
    #define SC7A20_I2C_Transmit(addr,reg,pdata,len,pflag)   I2C_RegWrite(addr, reg, pdata, len)
    #define SC7A20_I2C_Receive(addr,reg,pdata,len,pflag)    I2C_RegRead(addr, reg, pdata, len)
#endif

//加速度计默认配置（可在初始化后按需用SC7A20_SetODR/SetFullScale等修改）
#define SC7A20_DEFAULT_ODR            SC7A20_ODR_1HZ     //默认输出数据率（大于0的最低速率：1Hz，兼顾可用与低功耗）
#define SC7A20_DEFAULT_FULLSCALE      SC7A20_FS_2G       //默认量程
#define SC7A20_DEFAULT_HIGHRES        1                  //1=12bit高精度输出（HR），0=10bit
#define SC7A20_DEFAULT_BDU            1                  //1=块数据更新（读LSB+MSB期间不更新）
#define SC7A20_DEFAULT_AXIS_EN        (SC7A20_AXIS_X|SC7A20_AXIS_Y|SC7A20_AXIS_Z)//默认使能三轴

/******************************用户设置区结束**********************************/
//操作语法宏，方便添加freeRTOS之类的支持
#ifdef SC7A20_USE_PROTOTHREAD
    #define SC7A20_RET          char
    #define SC7A20_NOARG        struct pt *pt
    #define SC7A20_ARGS(...)    struct pt *pt, __VA_ARGS__
    #define SC7A20_EXEC(cond)   if(cond == 0) THRD_YIELD                        //反复执行某函数直到返回1
    #define SC7A20_UNTIL(cond)  THRD_UNTIL(cond)                                //条件不满足时出让CPU
    #define SC7A20_SPAWN_NOARG(func)\
                                THRD_SPAWN_NOARG(func)
    #define SC7A20_SPAWN_ARGS(func,...)\
                                THRD_SPAWN_ARGS(func, __VA_ARGS__)              //调用子线程/函数语句
    #define SC7A20_FUNC_BEGIN   THRD_BEGIN
    #define SC7A20_FUNC_END     THRD_END
    #define SC7A20_MUTEX_TAKE   PT_SEM_WAIT(pt, &i2c_mutex)
    #define SC7A20_MUTEX_GIVE   PT_SEM_SIGNAL(pt, &i2c_mutex)   
#else
    #define SC7A20_RET          void
    #define SC7A20_NOARG        void
    #define SC7A20_ARGS(...)    __VA_ARGS__
    #define SC7A20_EXEC(cond)   cond
    #define SC7A20_UNTIL(cond)  {}
    #define SC7A20_SPAWN_NOARG(func)\
                                func()
    #define SC7A20_SPAWN_ARGS(func,...)\
                                func(__VA_ARGS__)
    #define SC7A20_FUNC_BEGIN   {}
    #define SC7A20_FUNC_END     {}
    #define SC7A20_MUTEX_TAKE   xSemaphoreTake(mutex_i2c_handle, portMAX_DELAY)
    #define SC7A20_MUTEX_GIVE   xSemaphoreGive(mutex_i2c_handle)
#endif    

/******************************对外数据类型*************************************/
//错误码（SC7A20_GetLastError 返回）
typedef enum {
    SC7A20_ERR_NONE = 0,        //无错误
    SC7A20_ERR_NOT_ONLINE,      //芯片不在线（WHO_AM_I校验失败）
    SC7A20_ERR_BAD_PARAM,       //参数非法
    SC7A20_ERR_INT_BUSY,        //目标INT线已被其他功能占用（INT资源冲突）
} sc7a20_err_t;

//中断资源占用者（用于防止静默覆盖其他功能的中断配置）
typedef enum {
    SC7A20_INT_OWNER_NONE = 0,
    SC7A20_INT_OWNER_FREEFALL,
    SC7A20_INT_OWNER_MOTION,
    SC7A20_INT_OWNER_VIBRATION,
} sc7a20_int_owner_t;

//运动状态（物理含义：物体静止/运动；与系统休眠无关，业务层自行映射）
typedef enum {
    SC7A20_MOTION_STATE_UNKNOWN = 0,
    SC7A20_MOTION_STATE_STILL,
    SC7A20_MOTION_STATE_MOVING,
} sc7a20_motion_state_t;

//自由跌落配置（物理量接口，驱动内部换算寄存器值）
typedef struct {
    uint16_t threshold_mg;   //跌落阈值（mg）：三轴同时低于此值视为自由跌落
    uint16_t duration_ms;    //持续时长（ms）：达到阈值后需维持的时长
    uint8_t  latch;          //1=中断锁存（读INT_SRC清除），0=不锁存
    uint8_t  int_line;       //事件输出线：1=INT1，2=INT2
} sc7a20_freefall_config_t;

//运动/静止检测配置
//（硬件路径ACT_THS/ACT_DUR为实验性：COMPAT_INFERRED + TODO_HW_VERIFY）
typedef struct {
    uint16_t motion_threshold_mg; //运动阈值（mg）：总幅值偏离1g超过此值视为运动
    uint32_t stillness_time_ms;   //静止判定时长（ms）：低于阈值持续该时长后判定静止
    uint8_t  route_to_int2;       //1=ACT状态路由到INT2（实验性），0=仅软件查询
} sc7a20_motion_config_t;

//硬件振动事件配置（HPF + AOI阈值中断）
typedef struct {
    uint16_t threshold_mg;   //振动阈值（mg，HPF后的动态幅值）
    uint16_t duration_ms;    //持续时间（ms）
    uint8_t  axis_mask;      //参与检测的轴（SC7A20_AXIS_X|Y|Z）
    uint8_t  latch;          //1=锁存中断，0=不锁存
    uint8_t  int_line;       //事件输出线：1=INT1，2=INT2
} sc7a20_vibration_event_config_t;

//软件振动强度结果
typedef struct {
    float rms_mg;            //窗口RMS振动强度（mg）
    float peak_mg;           //窗口峰值动态幅值（mg）
} sc7a20_vibration_result_t;

struct SC7A20_StatusTypedef
{
    uint8_t initialized;            //SC7A20已初始化（WHO_AM_I 校验通过并完成配置）；通信失败时清 0 触发重新初始化
    uint8_t flag;                                                               //标识传输完成与传输状态用变量
    uint8_t sendbuf[8];                                                         //传输缓冲用变量
    
/***************************寄存器内存镜像声明*********************************/
    //测量数据区（原始数据，需要经过对应的转换函数才有意义）
    int16_t x;                      //X轴原始加速度数据（2的补码）
    int16_t y;                      //Y轴原始加速度数据
    int16_t z;                      //Z轴原始加速度数据
    int16_t temp;                   //温度原始数据（12位补码）
    uint8_t status;                 //0x27 状态寄存器

    //中断源镜像
    uint8_t int1_source;            //0x31 中断1状态
    uint8_t int2_source;            //0x35 中断2状态

    //设置寄存器存档
    uint8_t ctrl1;                  //0x20 控制寄存器1（ODR/低功耗/轴使能）
    uint8_t ctrl2;                  //0x21 控制寄存器2（高通滤波）
    uint8_t ctrl3;                  //0x22 控制寄存器3（INT1中断路由）
    uint8_t ctrl4;                  //0x23 控制寄存器4（BDU/量程/高精度/自测试）
    uint8_t ctrl5;                  //0x24 控制寄存器5（FIFO/锁存/4D）
    uint8_t ctrl6;                  //0x25 控制寄存器6（INT2中断路由）
    uint8_t int1_cfg;               //0x30 中断1配置
    uint8_t int2_cfg;               //0x34 中断2配置
    uint8_t fifo_ctrl;              //0x2E FIFO控制

    //高级功能状态
    uint8_t last_error;             //最近一次配置/使能操作错误码（sc7a20_err_t）
    uint8_t int1_owner;             //INT1当前占用者（sc7a20_int_owner_t）
    uint8_t int2_owner;             //INT2当前占用者
    uint8_t motion_state;           //运动状态（sc7a20_motion_state_t，软件估算）
    uint16_t motion_tick;           //静止计时节拍（内部）
    uint8_t ff_enabled;             //自由跌落已使能
    uint8_t motion_enabled;         //运动/静止检测已使能
    uint8_t vib_evt_enabled;        //硬件振动事件已使能
    uint16_t vib_window_samples;    //软件振动RMS窗口样本数（内部）
    float grav_x;                   //低频/DC分量估计（EMA，内部）
    float grav_y;                   //低频/DC分量估计
    float grav_z;                   //低频/DC分量估计
    float vib_sum_sq;               //窗口动态幅值平方和（内部）
    float vib_peak;                 //窗口峰值动态幅值（mg，内部）
    uint16_t vib_sample_count;      //窗口已累计样本数（内部）
    sc7a20_vibration_result_t vibration_rms;   //软件振动结果（RMS/峰值）
    sc7a20_freefall_config_t freefall_cfg;     //自由跌落配置存档
    sc7a20_motion_config_t motion_cfg;         //运动/静止检测配置存档
    sc7a20_vibration_event_config_t vibration_cfg;//硬件振动事件配置存档
};

//SC7A20 I2C 地址（7位0x18或0x19，此处为左移一位后的8位写地址）
//SDO接逻辑高/悬空：0x19 → 写地址0x32；SDO接逻辑低：0x18 → 写地址0x30
#ifndef SC7A20_I2C_ADDR
#define SC7A20_I2C_ADDR                 0x32U
#endif

//WHO_AM_I 期望值
#define SC7A20_WHO_AM_I_VALUE            0x11U

/**************************SC7A20 寄存器地址定义*******************************/
//命名规则：固定前缀(SC7A20)_状态(ST)/控制(CT)+寄存器(RG)_(功能描述)
#define SC7A20_STRG_OUT_TEMP_L       0x0CU//温度输出低8位
#define SC7A20_STRG_OUT_TEMP_H       0x0DU//温度输出高4位（12位补码）
#define SC7A20_STRG_WHO_AM_I         0x0FU//芯片标识（应读0x11）
#define SC7A20_CTRG_NVM_WR           0x1EU//NVM写（内部校准数据）
#define SC7A20_CTRG_TEMP_CFG         0x1FU//温度配置（内部温度ADC使能）

#define SC7A20_CTRG_CTRL1            0x20U//控制寄存器1（ODR/低功耗/轴使能）
#define SC7A20_CTRG_CTRL2            0x21U//控制寄存器2（高通滤波配置）
#define SC7A20_CTRG_CTRL3            0x22U//控制寄存器3（INT1中断路由）
#define SC7A20_CTRG_CTRL4            0x23U//控制寄存器4（BDU/量程/高精度/自测试）
#define SC7A20_CTRG_CTRL5            0x24U//控制寄存器5（BOOT/FIFO/中断锁存/4D）
#define SC7A20_CTRG_CTRL6            0x25U//控制寄存器6（INT2中断路由）
#define SC7A20_CTRG_REFERENCE        0x26U//参考值（高通滤波参考）
#define SC7A20_STRG_STATUS           0x27U//状态寄存器

#define SC7A20_STRG_OUT_X_L          0x28U//X轴加速度低8位
#define SC7A20_STRG_OUT_X_H          0x29U//X轴加速度高8位
#define SC7A20_STRG_OUT_Y_L          0x2AU//Y轴加速度低8位
#define SC7A20_STRG_OUT_Y_H          0x2BU//Y轴加速度高8位
#define SC7A20_STRG_OUT_Z_L          0x2CU//Z轴加速度低8位
#define SC7A20_STRG_OUT_Z_H          0x2DU//Z轴加速度高8位

/*连续读起始地址（MSB=1 触发芯片地址自动递增）：从0x28起连读X/Y/Z共6字节
 * ⚠️ 若用 0x28（MSB=0），连续读时地址不自增，每字节都返回同一寄存器 → 三轴数据被复制*/
#define SC7A20_STRG_OUT_AUTO         (SC7A20_STRG_OUT_X_L | 0x80U)//0xA8

#define SC7A20_CTRG_FIFO_CTRL        0x2EU//FIFO控制
#define SC7A20_STRG_FIFO_SRC         0x2FU//FIFO状态

#define SC7A20_CTRG_INT1_CFG         0x30U//中断1配置
#define SC7A20_STRG_INT1_SOURCE      0x31U//中断1状态
#define SC7A20_CTRG_INT1_THS         0x32U//中断1阈值
#define SC7A20_CTRG_INT1_DURATION    0x33U//中断1持续时间
#define SC7A20_CTRG_INT2_CFG         0x34U//中断2配置
#define SC7A20_STRG_INT2_SOURCE      0x35U//中断2状态
#define SC7A20_CTRG_INT2_THS         0x36U//中断2阈值
#define SC7A20_CTRG_INT2_DURATION    0x37U//中断2持续时间

#define SC7A20_CTRG_CLICK_CFG        0x38U//单击/双击配置
#define SC7A20_STRG_CLICK_SRC        0x39U//单击/双击状态
#define SC7A20_CTRG_CLICK_THS        0x3AU//单击/双击阈值
#define SC7A20_CTRG_TIME_LIMIT       0x3BU//单击时间限制
#define SC7A20_CTRG_TIME_LATENCY     0x3CU//双击时间间隔
#define SC7A20_CTRG_TIME_WINDOW      0x3DU//双击时间窗口
#define SC7A20_CTRG_ACT_THS          0x3EU//激活/睡眠阈值
#define SC7A20_CTRG_ACT_DURATION     0x3FU//激活/睡眠持续时间

/******************************寄存器位定义************************************/

//0x20  SC7A20_CTRG_CTRL1            控制寄存器1
#define SC7A20_CTRL1_ODR_MSK         0xF0U//输出数据率有效位
#define SC7A20_CTRL1_LPEN            0x08U//低功耗模式使能
#define SC7A20_CTRL1_ZEN             0x04U//Z轴使能
#define SC7A20_CTRL1_YEN             0x02U//Y轴使能
#define SC7A20_CTRL1_XEN             0x01U//X轴使能

//输出数据率选择（ODR3~ODR0）
#define SC7A20_ODR_POWERDOWN         0x00U//电源关断
#define SC7A20_ODR_1HZ               0x10U//1Hz
#define SC7A20_ODR_10HZ              0x20U//10Hz
#define SC7A20_ODR_25HZ              0x30U//25Hz
#define SC7A20_ODR_50HZ              0x40U//50Hz
#define SC7A20_ODR_100HZ             0x50U//100Hz
#define SC7A20_ODR_200HZ             0x60U//200Hz
#define SC7A20_ODR_400HZ             0x70U//400Hz
#define SC7A20_ODR_1K6HZ_LP          0x80U//1.6kHz（仅低功耗）
#define SC7A20_ODR_1K25HZ            0x90U//1.25kHz（正常）/5kHz（低功耗）

//轴使能
#define SC7A20_AXIS_X                SC7A20_CTRL1_XEN//X轴
#define SC7A20_AXIS_Y                SC7A20_CTRL1_YEN//Y轴
#define SC7A20_AXIS_Z                SC7A20_CTRL1_ZEN//Z轴

//0x21  SC7A20_CTRG_CTRL2            控制寄存器2（高通滤波）
#define SC7A20_CTRL2_HPM_MSK         0xC0U//高通模式选择有效位
#define SC7A20_CTRL2_HPCF_MSK        0x30U//高通截止频率选择有效位
#define SC7A20_CTRL2_FDS             0x08U//数据滤波选择（1=内部滤波后输出）
#define SC7A20_CTRL2_HPCLICK         0x04U//CLICK高通滤波使能
#define SC7A20_CTRL2_HPIS2           0x02U//中断2 AOI高通滤波使能
#define SC7A20_CTRL2_HPIS1           0x01U//中断1 AOI高通滤波使能

//高通模式选择（HPM1/HPM0）
#define SC7A20_HPM_NORMAL_RST        0x00U//正常模式（读高通滤波自动复位）
#define SC7A20_HPM_REFERENCE         0x40U//滤波参考信号
#define SC7A20_HPM_NORMAL            0x80U//正常模式
#define SC7A20_HPM_AUTORST           0xC0U//中断事件自动复位

//0x22  SC7A20_CTRG_CTRL3            控制寄存器3（INT1中断路由）
#define SC7A20_CTRL3_I1_CLICK        0x80U//CLICK中断到INT1
#define SC7A20_CTRL3_I1_AOI1         0x40U//AOI1中断到INT1
#define SC7A20_CTRL3_I1_AOI2         0x20U//AOI2中断到INT1
#define SC7A20_CTRL3_I1_DRDY1        0x10U//DRDY1中断到INT1
#define SC7A20_CTRL3_I1_DRDY2        0x08U//DRDY2中断到INT1
#define SC7A20_CTRL3_I1_WTM          0x04U//FIFO水印中断到INT1
#define SC7A20_CTRL3_I1_OVERRUN      0x02U//FIFO溢出中断到INT1

//0x23  SC7A20_CTRG_CTRL4            控制寄存器4
#define SC7A20_CTRL4_BDU             0x80U//块数据更新（读LSB+MSB期间不更新数据）
#define SC7A20_CTRL4_BLE             0x40U//大端/小端选择（0=低字节在低地址）
#define SC7A20_CTRL4_FS_MSK          0x30U//全量程选择有效位
#define SC7A20_CTRL4_HR              0x08U//高精度输出模式（1=12bit）
#define SC7A20_CTRL4_ST_MSK          0x06U//自测试使能有效位
#define SC7A20_CTRL4_SIM             0x01U//SPI接口模式（0=4线，1=3线）

//全量程选择（FS1/FS0）
#define SC7A20_FS_2G                 0x00U//±2G（灵敏度1mg/digit @HR）
#define SC7A20_FS_4G                 0x10U//±4G（灵敏度2mg/digit @HR）
#define SC7A20_FS_8G                 0x20U//±8G（灵敏度4mg/digit @HR）
#define SC7A20_FS_16G                0x30U//±16G（灵敏度8mg/digit @HR）

//自测试模式（ST1/ST0）
#define SC7A20_ST_NORMAL             0x00U//正常模式
#define SC7A20_ST_TEST0              0x02U//自测试0
#define SC7A20_ST_TEST1              0x04U//自测试1

//0x24  SC7A20_CTRG_CTRL5            控制寄存器5
#define SC7A20_CTRL5_BOOT            0x80U//重载修调值（校准补偿）
#define SC7A20_CTRL5_FIFO_EN         0x40U//FIFO使能
#define SC7A20_CTRL5_LIR_INT1        0x08U//锁存中断1（读INT1_SOURCE清除）
#define SC7A20_CTRL5_D4D_INT1        0x04U//4D检测使能（INT1）
#define SC7A20_CTRL5_LIR_INT2        0x02U//锁存中断2（读INT2_SOURCE清除）
#define SC7A20_CTRL5_D4D_INT2        0x01U//4D检测使能（INT2）

//0x25  SC7A20_CTRG_CTRL6            控制寄存器6（INT2中断路由）
#define SC7A20_CTRL6_I2_CLICK        0x80U//CLICK中断到INT2
/* COMPAT_INFERRED: SC7A20手册文字疑似有误；按LIS2DH/LIS2DH12语义 I2_INT1=0x40 表示 AOI1 中断路由到 INT2 */
#define SC7A20_CTRL6_I2_INT1         0x40U//AOI1中断到INT2（COMPAT_INFERRED）
#define SC7A20_CTRL6_I2_INT2         0x20U//AOI2中断到INT2
#define SC7A20_CTRL6_BOOT_I2         0x10U//BOOT状态到INT2
#define SC7A20_CTRL6_H_LACTIVE       0x02U//中断高电平/低电平触发（0=高，1=低）

//0x27  SC7A20_STRG_STATUS           状态寄存器
#define SC7A20_STATUS_ZYXOR          0x80U//三轴至少一轴数据覆盖
#define SC7A20_STATUS_ZOR            0x40U//Z轴数据覆盖
#define SC7A20_STATUS_YOR            0x20U//Y轴数据覆盖
#define SC7A20_STATUS_XOR            0x10U//X轴数据覆盖
#define SC7A20_STATUS_ZYXDA          0x08U//三轴新数据全部转换完成
#define SC7A20_STATUS_ZDA            0x04U//Z轴新数据到来
#define SC7A20_STATUS_YDA            0x02U//Y轴新数据到来
#define SC7A20_STATUS_XDA            0x01U//X轴新数据到来

//0x30  SC7A20_CTRG_INT1_CFG         中断1配置
#define SC7A20_INT_CFG_AOI           0x80U//与/或中断事件
#define SC7A20_INT_CFG_6D            0x40U//6方向检测使能
#define SC7A20_INT_CFG_ZHIE          0x20U//Z轴高事件/方向检测中断使能
#define SC7A20_INT_CFG_ZLIE          0x10U//Z轴低事件/方向检测中断使能
#define SC7A20_INT_CFG_YHIE          0x08U//Y轴高事件/方向检测中断使能
#define SC7A20_INT_CFG_YLIE          0x04U//Y轴低事件/方向检测中断使能
#define SC7A20_INT_CFG_XHIE          0x02U//X轴高事件/方向检测中断使能
#define SC7A20_INT_CFG_XLIE          0x01U//X轴低事件/方向检测中断使能

//中断模式（AOI/6D）
#define SC7A20_INTMODE_OR            0x00U//或中断事件
#define SC7A20_INTMODE_6D_MOTION     0x40U//6方向运动识别
#define SC7A20_INTMODE_AND           0x80U//与中断事件
#define SC7A20_INTMODE_6D_POSITION   0xC0U//6方向位置检测

//0x31  SC7A20_STRG_INT1_SOURCE      中断1状态
#define SC7A20_INT_SRC_IA            0x40U//中断激活
#define SC7A20_INT_SRC_ZH            0x20U//Z轴高事件
#define SC7A20_INT_SRC_ZL            0x10U//Z轴低事件
#define SC7A20_INT_SRC_YH            0x08U//Y轴高事件
#define SC7A20_INT_SRC_YL            0x04U//Y轴低事件
#define SC7A20_INT_SRC_XH            0x02U//X轴高事件
#define SC7A20_INT_SRC_XL            0x01U//X轴低事件

//0x32  SC7A20_CTRG_INT1_THS         中断1阈值（1LSB=16mg@2G，32mg@4G，64mg@8G，128mg@16G）
#define SC7A20_INT_THS_MSK           0x7FU//阈值有效位

//0x33  SC7A20_CTRG_INT1_DURATION    中断1持续时间（以ODR为时钟）
#define SC7A20_INT_DUR_MSK           0x7FU//持续时间有效位

//0x3E  SC7A20_CTRG_ACT_THS          运动/静止激活阈值
//COMPAT_INFERRED: SC7A20资料未说明，按LIS2DH12推断与INT_THS同LSB（16/32/64/128mg按量程），待实物验证
#define SC7A20_ACT_THS_MSK           0x7FU//激活阈值有效位

//0x3F  SC7A20_CTRG_ACT_DURATION     运动/静止判定时长
//COMPAT_INFERRED: 单位按LIS2DH12推断为1/ODR时钟，待实物验证
#define SC7A20_ACT_DUR_MSK           0x7FU//判定时长有效位

/*****************************函数声明区***************************************/
//基本操作
SC7A20_RET SC7A20_ByteWrite(SC7A20_ARGS(uint8_t reg, uint8_t data));     //写单字节
SC7A20_RET SC7A20_ByteRead(SC7A20_ARGS(uint8_t reg, volatile uint8_t *data));//读单字节
SC7A20_RET SC7A20_BytesRead(SC7A20_ARGS(uint8_t reg, uint8_t *pdata, uint16_t len));//连续读
SC7A20_RET SC7A20_ByteModify(SC7A20_ARGS(uint8_t reg, uint8_t mask, uint8_t data));//读-改-写

//加速度数据操作
SC7A20_RET SC7A20_AccelLoad(SC7A20_NOARG);    //读取三轴原始数据镜像（0x28~0x2D共6字节）
int16_t SC7A20_ReadX(void);                    //读取X轴原始数据
int16_t SC7A20_ReadY(void);                    //读取Y轴原始数据
int16_t SC7A20_ReadZ(void);                    //读取Z轴原始数据
float SC7A20_ReadX_mg(void);                   //读取X轴加速度（单位：mg）
float SC7A20_ReadY_mg(void);                   //读取Y轴加速度（单位：mg）
float SC7A20_ReadZ_mg(void);                   //读取Z轴加速度（单位：mg）

//温度操作
SC7A20_RET SC7A20_TempLoad(SC7A20_NOARG);      //读取温度原始数据镜像（0x0C/0x0D）
float SC7A20_ReadTemp(void);                   //读取温度（单位：°C）

//状态操作
SC7A20_RET SC7A20_StatusLoad(SC7A20_NOARG);    //读取状态寄存器镜像（0x27）
uint8_t SC7A20_IsDataReady(void);              //三轴新数据全部就绪（ZYXDA）
uint8_t SC7A20_HasOverrun(void);               //有数据覆盖（ZYXOR）
uint8_t SC7A20_ReadStatus(void);               //读取状态寄存器原始值

//配置操作
SC7A20_RET SC7A20_SetODR(SC7A20_ARGS(uint8_t odr));         //设置输出数据率（SC7A20_ODR_*）
SC7A20_RET SC7A20_SetFullScale(SC7A20_ARGS(uint8_t fs));    //设置量程（SC7A20_FS_*）
SC7A20_RET SC7A20_SetPowerMode(SC7A20_ARGS(uint8_t lpen));  //设置低功耗模式（1=低功耗，0=正常）
SC7A20_RET SC7A20_EnableAxis(SC7A20_ARGS(uint8_t axis));    //设置轴使能（SC7A20_AXIS_X|Y|Z）
SC7A20_RET SC7A20_BDUSet(SC7A20_ARGS(uint8_t enable));      //块数据更新使能
SC7A20_RET SC7A20_HighResSet(SC7A20_ARGS(uint8_t enable));  //高精度输出使能（1=12bit）

//中断操作
SC7A20_RET SC7A20_Int1Config(SC7A20_ARGS(uint8_t cfg));     //配置中断1（SC7A20_INT_CFG_*）
SC7A20_RET SC7A20_Int1ThresholdSet(SC7A20_ARGS(uint8_t ths));//设置中断1阈值
SC7A20_RET SC7A20_Int1DurationSet(SC7A20_ARGS(uint8_t dur));//设置中断1持续时间
SC7A20_RET SC7A20_ReadInt1Source(SC7A20_NOARG);             //读取中断1状态镜像（读后清除锁存）
uint8_t SC7A20_IsInt1Active(void);                          //中断1激活（IA）
SC7A20_RET SC7A20_Int2Config(SC7A20_ARGS(uint8_t cfg));     //配置中断2（SC7A20_INT_CFG_*）
SC7A20_RET SC7A20_Int2ThresholdSet(SC7A20_ARGS(uint8_t ths));//设置中断2阈值
SC7A20_RET SC7A20_Int2DurationSet(SC7A20_ARGS(uint8_t dur));//设置中断2持续时间
SC7A20_RET SC7A20_ReadInt2Source(SC7A20_NOARG);             //读取中断2状态镜像
uint8_t SC7A20_IsInt2Active(void);                          //中断2激活（IA）
SC7A20_RET SC7A20_IntRouteSet(SC7A20_ARGS(uint8_t ctrl3, uint8_t ctrl6));//设置INT1/INT2中断路由

//自测试操作
SC7A20_RET SC7A20_SelfTest(SC7A20_ARGS(uint8_t st));        //设置自测试模式（SC7A20_ST_*）

//低功耗设置（修正INT极性 + 进入Power-down；休眠策略进 DeepSleep 前调用，恢复时再 Init/SetODR）
SC7A20_RET SC7A20_LowPowerSet(SC7A20_NOARG);   //仅修正INT极性并置于Power-down；不改变initialized状态

//初始化
SC7A20_RET SC7A20_Init(SC7A20_NOARG);          //初始化，最好系统上电后立刻执行
uint8_t SC7A20_IsInitialized(void);             //检测SC7A20是否已初始化过（通信失败时会返回 0）

//错误查询
uint8_t SC7A20_GetLastError(void);              //读取最近一次配置/使能操作错误码（sc7a20_err_t）

//自由跌落检测（AOI: AND + 三轴低事件）
SC7A20_RET SC7A20_FreefallConfig(SC7A20_ARGS(const sc7a20_freefall_config_t *cfg));//配置（阈值/时长/锁存/输出线）
SC7A20_RET SC7A20_FreefallEnable(SC7A20_NOARG); //使能自由跌落检测
SC7A20_RET SC7A20_FreefallDisable(SC7A20_NOARG);//禁止自由跌落检测
SC7A20_RET SC7A20_FreefallGetStatus(SC7A20_ARGS(uint8_t *active));//读取跌落事件状态（锁存模式读后清除）

//运动/静止检测（软件判定可用；ACT硬件路径为实验性）
SC7A20_RET SC7A20_MotionConfig(SC7A20_ARGS(const sc7a20_motion_config_t *cfg));//配置（阈值/静止时长/INT2路由）
SC7A20_RET SC7A20_MotionEnable(SC7A20_NOARG);   //使能运动/静止检测（写ACT_THS/ACT_DUR + 可选INT2路由）
SC7A20_RET SC7A20_MotionDisable(SC7A20_NOARG);  //禁止运动/静止检测
SC7A20_RET SC7A20_MotionUpdate(SC7A20_NOARG);   //用最近一次加速度样本更新运动状态（软件判定，按ODR周期调用）
uint8_t SC7A20_MotionGetState(void);            //查询运动状态（sc7a20_motion_state_t）

//硬件振动事件（HPF + AOI阈值中断）
SC7A20_RET SC7A20_VibrationEventConfig(SC7A20_ARGS(const sc7a20_vibration_event_config_t *cfg));//配置
SC7A20_RET SC7A20_VibrationEventEnable(SC7A20_NOARG);//使能
SC7A20_RET SC7A20_VibrationEventDisable(SC7A20_NOARG);//禁止
SC7A20_RET SC7A20_VibrationEventGetStatus(SC7A20_ARGS(uint8_t *active));//读取振动事件状态（锁存模式读后清除）

//软件振动强度（窗口RMS/峰值，SC7A20_VERIFIED算法）
SC7A20_RET SC7A20_VibrationAnalyze(SC7A20_NOARG);//用最近一次加速度样本累计窗口（DC去除+平方累加，按ODR周期调用）
SC7A20_RET SC7A20_VibrationReset(SC7A20_NOARG); //清空窗口累计并按当前ODR重算窗口长度
const sc7a20_vibration_result_t *SC7A20_VibrationGetResult(void);//获取窗口RMS/峰值

//寄存器Dump（调试）
SC7A20_RET SC7A20_RegisterDumpRead(SC7A20_ARGS(uint8_t *out, uint16_t len));//读0x0F+0x20~0x3F共33字节到out（out[0]=WHO_AM_I）
SC7A20_RET SC7A20_RegisterDump(SC7A20_NOARG);   //读取并通过USART_Printf打印

#ifdef __cplusplus
}
#endif

#endif
