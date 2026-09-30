#ifndef __BSP_I2C_H__
#define __BSP_I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f423_wk_config.h"
#include "i2c_application.h"
    
//启动前等待 BUSY 释放的“循环计数”上限（不是毫秒）
#ifndef BSP_I2C_START_WAIT_CNT
#define BSP_I2C_START_WAIT_CNT   200000U
#endif

//一次 I2C 会话允许的最大时间（毫秒）
#ifndef BSP_I2C_SESSION_TIMEOUT_MS
#define BSP_I2C_SESSION_TIMEOUT_MS  30U
#endif

/* hi2c.status 的“完成”值（对应 i2c_application.c 内部宏 I2C_END：0=I2C_START 1=I2C_END） */
#define BSP_I2C_STATUS_END   1U
    
extern TaskHandle_t i2c_wait_task;

/* I2C 故障现场（sticky）：在错误中断入口抓取，早于官方 i2c_err_irq_handler()
 * 清标志，因此能在调试器 Watch 中看到“最初的错误现场”。复位后自动清零。
 *  - sts   : I2C1->sts（BUSERR 0x0100 / ARLOST 0x0200 / OUF 0x0400 / TMOUT 0x1000 …）
 *  - ctrl1 : 中断使能/外设使能状态；ctrl2: master/slave 与停止模式
 *  - txdt  : 出错瞬间的数据字节（常可定位“从机地址/寄存器地址”阶段）
 * 判读：BUSERR → SDA/SCL 毛刺或从机拉死；ARLOST → 单主机系统中的异常 START；
 *       OUF → 软件/ISR 响应不及（优先级或长临界区）。 */
extern volatile uint32_t i2c_fault_sts;
extern volatile uint32_t i2c_fault_ctrl1;
extern volatile uint32_t i2c_fault_ctrl2;
extern volatile uint32_t i2c_fault_txdt;
extern volatile uint32_t i2c_fault_count;

/* 最近一次事务的错误码（i2c_status_type，在总线恢复时保存）：
 * 即使错误发生在不进错误中断的路径（如库内 TDC/TCRLD 异常），也能在这里看到。 */
extern volatile uint32_t i2c_fault_code;

/* 总线恢复统计（Watch 查看）：
 *  - i2c_recover_count ：i2c_bus_recover() 执行次数（含无错误码的启动失败/超时恢复）
 *  - i2c_recover_reason：最近一次恢复原因，编码 = (原因码 << 16) | (细节 & 0xFFFF)
 *      原因码见下方 BSP_I2C_RC_* ；细节 = 库错误码或 STEP 码 */
extern volatile uint32_t i2c_recover_count;
extern volatile uint32_t i2c_recover_reason;

#define BSP_I2C_RC_START_FAIL      1U   /* 启动失败：细节为 I2C_ERR_STEP_x（卡在 BUSYF/TDIS/TDC/STOPF…） */
#define BSP_I2C_RC_NOTIFY_TIMEOUT  2U   /* 等完成通知超时：细节为 0 */
#define BSP_I2C_RC_TRANS_ERROR     3U   /* 通知到达但事务状态异常：细节为库错误码（INTERRUPT/TDC/ACKFAIL…） */

void i2c_fault_capture(void);   //I2C 错误中断入口调用：抓取原始故障现场（粘滞）

/* 事务入口：整笔事务失败后自动恢复总线并重试，最多 BSP_I2C_ATTEMPTS 次。
 * 返回 I2C_OK=成功；否则为最后一次的库错误码（i2c_status_type，HAL 风格）。
 * 调用方应按返回值决定是否把对应器件置为离线，而不是假定成功。 */
i2c_status_type I2C_Transmit(uint8_t addr, uint8_t* p_buf, uint16_t len);  //发送函数
i2c_status_type I2C_RegWrite(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len);//带寄存器设置的发送函数
i2c_status_type I2C_Receive(uint8_t addr, uint8_t* p_buf, uint16_t len);   //读取函数
i2c_status_type I2C_RegRead(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len);//带寄存器设置的读取函数
uint8_t I2C_IsReady(uint8_t addr);  //确认地址存在函数

#ifdef __cplusplus
}
#endif

#endif