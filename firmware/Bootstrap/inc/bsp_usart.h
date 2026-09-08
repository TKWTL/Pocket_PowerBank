#ifndef __BSP_USART_H__
#define __BSP_USART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f423_wk_config.h"
#include <stdarg.h>

/* 环形发送缓冲区大小（必须是 2 的幂） */
#ifndef BSP_USART_TX_BUF_SIZE
#define BSP_USART_TX_BUF_SIZE   128
#endif

#define DUART           USART1
#define DUART_DMATX_CH  DMA1_CHANNEL6
#define DUART_TX_FDT    DMA1_FDT6_FLAG

void USART_SendByte(uint8_t data);
void USART_SendString(const char *str);
void USART_Printf(const char *format, ...);
void USART_TxIRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif