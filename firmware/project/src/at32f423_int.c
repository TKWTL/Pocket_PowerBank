/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f423_int.c
  * @brief    main interrupt service routines.
  **************************************************************************
  *                       Copyright notice & Disclaimer
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */
/* add user code end Header */

/* includes ------------------------------------------------------------------*/
#include "at32f423_int.h"
#include "wk_system.h"
#include "freertos_app.h"

#include "i2c_application.h"

extern i2c_handle_type hi2c1;
/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "bsp_i2c.h"
#include "bsp_spi.h"

#include "lv_tick.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/* external variables ---------------------------------------------------------*/
/* add user code begin external variables */

/* add user code end external variables */

/**
  * @brief  this function handles nmi exception.
  * @param  none
  * @retval none
  */
void NMI_Handler(void)
{
  /* add user code begin NonMaskableInt_IRQ 0 */

  /* add user code end NonMaskableInt_IRQ 0 */

  /* add user code begin NonMaskableInt_IRQ 1 */

  /* add user code end NonMaskableInt_IRQ 1 */
}

/**
  * @brief  this function handles hard fault exception.
  * @param  none
  * @retval none
  */
void HardFault_Handler(void)
{
  /* add user code begin HardFault_IRQ 0 */
    //直接复位，避免死机
    NVIC_SystemReset();
  /* add user code end HardFault_IRQ 0 */
  /* go to infinite loop when hard fault exception occurs */
  while (1)
  {
    /* add user code begin W1_HardFault_IRQ 0 */

    /* add user code end W1_HardFault_IRQ 0 */
  }
}

/**
  * @brief  this function handles memory manage exception.
  * @param  none
  * @retval none
  */
void MemManage_Handler(void)
{
  /* add user code begin MemoryManagement_IRQ 0 */

  /* add user code end MemoryManagement_IRQ 0 */
  /* go to infinite loop when memory manage exception occurs */
  while (1)
  {
    /* add user code begin W1_MemoryManagement_IRQ 0 */

    /* add user code end W1_MemoryManagement_IRQ 0 */
  }
}

/**
  * @brief  this function handles bus fault exception.
  * @param  none
  * @retval none
  */
void BusFault_Handler(void)
{
  /* add user code begin BusFault_IRQ 0 */
    //直接复位，避免死机
    NVIC_SystemReset();
  /* add user code end BusFault_IRQ 0 */
  /* go to infinite loop when bus fault exception occurs */
  while (1)
  {
    /* add user code begin W1_BusFault_IRQ 0 */

    /* add user code end W1_BusFault_IRQ 0 */
  }
}

/**
  * @brief  this function handles usage fault exception.
  * @param  none
  * @retval none
  */
void UsageFault_Handler(void)
{
  /* add user code begin UsageFault_IRQ 0 */
    //直接复位，避免死机
    NVIC_SystemReset();
  /* add user code end UsageFault_IRQ 0 */
  /* go to infinite loop when usage fault exception occurs */
  while (1)
  {
    /* add user code begin W1_UsageFault_IRQ 0 */

    /* add user code end W1_UsageFault_IRQ 0 */
  }
}


/**
  * @brief  this function handles debug monitor exception.
  * @param  none
  * @retval none
  */
void DebugMon_Handler(void)
{
  /* add user code begin DebugMonitor_IRQ 0 */

  /* add user code end DebugMonitor_IRQ 0 */
  /* add user code begin DebugMonitor_IRQ 1 */

  /* add user code end DebugMonitor_IRQ 1 */
}

extern void xPortSysTickHandler(void);

/**
  * @brief  this function handles systick handler.
  * @param  none
  * @retval none
  */
void SysTick_Handler(void)
{
  /* add user code begin SysTick_IRQ 0 */
    lv_tick_inc(1);
  /* add user code end SysTick_IRQ 0 */

  wk_timebase_handler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
#endif /* INCLUDE_xTaskGetSchedulerState */
  xPortSysTickHandler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  }
#endif /* INCLUDE_xTaskGetSchedulerState */

  /* add user code begin SysTick_IRQ 1 */

  /* add user code end SysTick_IRQ 1 */
}

/**
  * @brief  this function handles EXINT Line 0 handler.
  * @param  none
  * @retval none
  */
void EXINT0_IRQHandler(void)
{
  /* add user code begin EXINT0_IRQ 0 */

  /* add user code end EXINT0_IRQ 0 */

  if(exint_interrupt_flag_get(EXINT_LINE_0) != RESET)
  {
    /* add user code begin EXINT_LINE_0 */
    /* clear flag */
    exint_flag_clear(EXINT_LINE_0);
    /* add user code end EXINT_LINE_0 */ 
  }

  /* add user code begin EXINT0_IRQ 1 */

  /* add user code end EXINT0_IRQ 1 */
}

/**
  * @brief  this function handles EXINT Line 1 handler.
  * @param  none
  * @retval none
  */
void EXINT1_IRQHandler(void)
{
  /* add user code begin EXINT1_IRQ 0 */

  /* add user code end EXINT1_IRQ 0 */

  if(exint_interrupt_flag_get(EXINT_LINE_1) != RESET)
  {
    /* add user code begin EXINT_LINE_1 */
    /* clear flag */
    exint_flag_clear(EXINT_LINE_1);
    /* add user code end EXINT_LINE_1 */ 
  }

  /* add user code begin EXINT1_IRQ 1 */

  /* add user code end EXINT1_IRQ 1 */
}

/**
  * @brief  this function handles EXINT Line 2 handler.
  * @param  none
  * @retval none
  */
void EXINT2_IRQHandler(void)
{
  /* add user code begin EXINT2_IRQ 0 */

  /* add user code end EXINT2_IRQ 0 */

  if(exint_interrupt_flag_get(EXINT_LINE_2) != RESET)
  {
    /* add user code begin EXINT_LINE_2 */
    /* clear flag */
    exint_flag_clear(EXINT_LINE_2);
    /* add user code end EXINT_LINE_2 */ 
  }

  /* add user code begin EXINT2_IRQ 1 */

  /* add user code end EXINT2_IRQ 1 */
}

/**
  * @brief  this function handles DMA1 Channel 1 handler.
  * @param  none
  * @retval none
  */
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

/**
  * @brief  this function handles EXINT Line [9:5] handler.
  * @param  none
  * @retval none
  */
void EXINT9_5_IRQHandler(void)
{
  /* add user code begin EXINT9_5_IRQ 0 */

  /* add user code end EXINT9_5_IRQ 0 */

  if(exint_interrupt_flag_get(EXINT_LINE_8) != RESET)
  {
    /* add user code begin EXINT_LINE_8 */
    /* clear flag */
    exint_flag_clear(EXINT_LINE_8);
    /* add user code end EXINT_LINE_8 */ 
  }

  /* add user code begin EXINT9_5_IRQ 1 */

  /* add user code end EXINT9_5_IRQ 1 */
}

/**
  * @brief  this function handles I2C1 Event handler.
  * @param  none
  * @retval none
  */
void I2C1_EVT_IRQHandler(void)
{
  /* add user code begin I2C1_EVT_IRQ 0 */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  /* add user code end I2C1_EVT_IRQ 0 */

  i2c_evt_irq_handler(&hi2c1);

  /* add user code begin I2C1_EVT_IRQ 1 */
    if(hi2c1.status == 1){//define I2C_END 1
        if (i2c_wait_task != NULL) {
            vTaskNotifyGiveFromISR(
                i2c_wait_task,
                &xHigherPriorityTaskWoken
            );
        }
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  /* add user code end I2C1_EVT_IRQ 1 */
}

/**
  * @brief  this function handles I2C1 Error handler.
  * @param  none
  * @retval none
  */
void I2C1_ERR_IRQHandler(void)
{
  /* add user code begin I2C1_ERR_IRQ 0 */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  /* add user code end I2C1_ERR_IRQ 0 */

  i2c_err_irq_handler(&hi2c1);

  /* add user code begin I2C1_ERR_IRQ 1 */
    if (i2c_wait_task != NULL) {
        vTaskNotifyGiveFromISR(
                i2c_wait_task,
                &xHigherPriorityTaskWoken
            );
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  /* add user code end I2C1_ERR_IRQ 1 */
}

/* add user code begin 1 */

/* add user code end 1 */
