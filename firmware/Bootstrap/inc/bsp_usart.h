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
#define DUART_DMARX_CH  DMA1_CHANNEL7
#define DUART_RX_FDT    DMA1_FDT7_FLAG

#define BSP_USART_RX_BUF_SIZE   16U

void USART_SendByte(uint8_t data);
void USART_SendString(const char *str);
void USART_Printf(const char *format, ...);
void USART_TxIRQHandler(void);

/* 临时交互输入：进入页面时启用16B循环DMA，离开页面即关闭。 */
void USART_RxBegin(void);
void USART_RxEnd(void);
uint8_t USART_RxReadByte(uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif