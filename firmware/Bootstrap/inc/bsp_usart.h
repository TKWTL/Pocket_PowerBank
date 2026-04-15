#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f423_wk_config.h"
#include "wk_spi.h"
#include "wk_dma.h"
#include "wk_system.h"
#include "freertos_app.h"

#define GSPI    SPI3
#define GSPI    SPI3
#define GSPI_DMACH  DMA1_CHANNEL1
#define GSPI_FDT_FLAG   DMA1_FDT1_FLAG
#define GSPI    SPI3
#define GSPI    SPI3
#define GSPI    SPI3
    
void MX_SPI1_Init(void);

//发送函数
void GSPI_Transmit(uint8_t* p_buf, size_t len);


#ifdef __cplusplus
}
#endif

#endif