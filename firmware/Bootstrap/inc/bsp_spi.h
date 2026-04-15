#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f423_wk_config.h"

#define GSPI    SPI3
#define GSPI_DMACH  DMA1_CHANNEL1
#define GSPI_FDT_FLAG   DMA1_FDT1_FLAG
    
#define DSPI    SPI1
#define DSPI_DMATX_CH  DMA1_CHANNEL1
#define DSPI_TX_FDT_FLAG   DMA1_FDT1_FLAG
#define DSPI_DMARX_CH  DMA1_CHANNEL1
#define DSPI_RX_FDT_FLAG   DMA1_FDT1_FLAG
    
extern TaskHandle_t gspi_wait_task;
    
//屏幕SPI发送函数
void GSPI_Transmit(uint8_t* p_buf, size_t len);


#ifdef __cplusplus
}
#endif

#endif