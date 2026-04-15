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
    
extern TaskHandle_t i2c_wait_task;
    
void I2C_Transmit(uint8_t addr, uint8_t* p_buf, uint16_t len);  //发送函数
void I2C_RegWrite(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len);//带寄存器设置的发送函数
void I2C_Receive(uint8_t addr, uint8_t* p_buf, uint16_t len);   //读取函数
void I2C_RegRead(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len);//带寄存器设置的读取函数
uint8_t I2C_IsReady(uint8_t addr);  //确认地址存在函数

#ifdef __cplusplus
}
#endif

#endif