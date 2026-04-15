#include "bsp_spi.h"

TaskHandle_t gspi_wait_task = NULL;   // 当前等待SPI完成的任务句柄

//屏幕发送函数
void GSPI_Transmit(uint8_t* p_buf, size_t len){
    if (p_buf == NULL || len == 0) return;
    
    xSemaphoreTake(mutex_gspi_handle, portMAX_DELAY);
    
    gspi_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    
    dma_channel_enable(GSPI_DMACH, FALSE);/* 1) 关闭 DMA 通道，避免传输过程中改参数 */
    dma_flag_clear(GSPI_FDT_FLAG);/* 2) 清 DMA 完成标志（按你通道对应的 flag 宏改） */
    GSPI_DMACH->paddr = (uint32_t)&(GSPI->dt);/* 3) 设置外设地址：GSPI 数据寄存器 */
    GSPI_DMACH->maddr = (uint32_t)p_buf;/* 4) 设置内存地址：发送缓冲区首地址 */
    dma_data_number_set(GSPI_DMACH, len);/* 5) 设置传输长度 */
    dma_channel_enable(GSPI_DMACH, TRUE);/* 6) 使能 DMA 通道，等待 SPI3_TX DMA 请求触发搬运 */

    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    gspi_wait_task = NULL;
    
    xSemaphoreGive(mutex_gspi_handle);
}

//DMA中断函数示例，实际位置在_int.c文件中
#if 0
void DMA1_Channel1_IRQHandler(void)
{
  /* add user code begin DMA1_Channel1_IRQ 0 */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  /* add user code end DMA1_Channel1_IRQ 0 */

  if(dma_interrupt_flag_get(DMA1_FDT1_FLAG) != RESET)
  {   
    /* add user code begin DMA1_FDT1_FLAG */
    /* handle full data transfer and clear flag */
    dma_flag_clear(DMA1_FDT1_FLAG);
    if (gspi_wait_task != NULL) {
        vTaskNotifyGiveFromISR(
            gspi_wait_task,
            &xHigherPriorityTaskWoken
        );
    }
    /* add user code end DMA1_FDT1_FLAG */ 
  }

  /* add user code begin DMA1_Channel1_IRQ 1 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  /* add user code end DMA1_Channel1_IRQ 1 */
}
#endif
