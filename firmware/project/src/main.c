/* add user code begin Header */
/**
  **************************************************************************
  * @file     main.c
  * @brief    main program
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

/* Includes ------------------------------------------------------------------*/
#include "at32f423_wk_config.h"
#include "wk_system.h"
#include "freertos_app.h"
#include "i2c_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "bsp_usart.h"
#include "at32f423_int.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */
/* 固件/硬件信息（启动 banner 用；固件版本与 UI 菜单 Version 显示保持一致） */
#define FW_VERSION_STR      "1.2.0"
#define PROJECT_NAME_STR    "Pocket PowerBank"
#define HW_VERSION_STR      "Rev.A"
/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */
/* 启动时捕获的复位原因位图：bit0=POR,1=NRST,2=SW,3=WDT,4=WWDT,5=LOWPOWER
 * .noinit：掉电复位清零、系统复位保留（与 g_hardfault 崩溃现场一致，便于诊断） */
static uint8_t s_boot_reset_flags __attribute__((section(".noinit")));
/* 原始 RSTS 寄存器值（(CRM->ctrlsts>>24)&0xFF，与手册直接对应）：
 * bit2=NRST bit3=POR/LVR bit4=SW bit5=WDT bit6=WWDT bit7=LPW */
static uint8_t s_boot_rsts_raw __attribute__((section(".noinit")));
/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */
static void boot_capture_reset_flags(void);
static void boot_print_banner(void);
/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */
/* 最早捕获复位原因（RSTS sticky 标志），随后初始化流程可能清除这些标志 */
static void boot_capture_reset_flags(void)
{
    /* 清除前保存原始 RSTS（bit24~31）：bit2=NRST bit3=POR/LVR bit4=SW bit5=WDT bit6=WWDT bit7=LPW */
    s_boot_rsts_raw = (uint8_t)((CRM->ctrlsts >> 24) & 0xFFU);
    s_boot_reset_flags = 0;
    if(crm_flag_get(CRM_POR_RESET_FLAG)      == SET) s_boot_reset_flags |= (1u << 0);
    if(crm_flag_get(CRM_NRST_RESET_FLAG)     == SET) s_boot_reset_flags |= (1u << 1);
    if(crm_flag_get(CRM_SW_RESET_FLAG)       == SET) s_boot_reset_flags |= (1u << 2);
    if(crm_flag_get(CRM_WDT_RESET_FLAG)      == SET) s_boot_reset_flags |= (1u << 3);
    if(crm_flag_get(CRM_WWDT_RESET_FLAG)     == SET) s_boot_reset_flags |= (1u << 4);
    if(crm_flag_get(CRM_LOWPOWER_RESET_FLAG) == SET) s_boot_reset_flags |= (1u << 5);
    crm_flag_clear(CRM_ALL_RESET_FLAG);
}

/* 上电打印：项目/固件/硬件信息 + 复位原因 + 上次崩溃现场（类 Linux 启动 banner）
 * 注意：USART_Printf 单行输出须 <64B，勿超长 */
static void boot_print_banner(void)
{
    USART_Printf("\r\n");
    USART_Printf("========================================\r\n");
    USART_Printf("  " PROJECT_NAME_STR " Firmware\r\n");
    USART_Printf("  FW  : v" FW_VERSION_STR " (" __DATE__ " " __TIME__ ")\r\n");
    USART_Printf("  HW  : " HW_VERSION_STR " | 2S1P 30Q\r\n");
    USART_Printf("  MCU : AT32F423KCU7 @ %luMHz\r\n", (unsigned long)(SystemCoreClock / 1000000UL));
    USART_Printf("========================================\r\n");

    /* 复位原因（崩溃优先：异常自动复位有完整现场，不再打印原始标志值） */
    if(g_hardfault.magic == 0xFA17CA11UL)
    {
        USART_Printf("[BOOT] Reset: CRASH (auto-reset, see dump)\r\n");
        USART_Printf("[BOOT] HFSR=0x%08X CFSR=0x%08X\r\n", (unsigned int)g_hardfault.hfsr, (unsigned int)g_hardfault.cfsr);
        USART_Printf("[BOOT] PC=0x%08X LR=0x%08X PSR=0x%08X\r\n", (unsigned int)g_hardfault.pc, (unsigned int)g_hardfault.lr, (unsigned int)g_hardfault.psr);
        g_hardfault.magic = 0;   /* 打印一次后清除，避免每次复位重复报警 */
    }
    else
    {
        /* 复位原因（优先级：SW>WDT>WWDT>LowPower>NRST>POR） */
        if(s_boot_reset_flags & (1u << 2))      USART_Printf("[BOOT] Reset: SW (NVIC_SystemReset)\r\n");
        else if(s_boot_reset_flags & (1u << 3)) USART_Printf("[BOOT] Reset: Watchdog (WDT)\r\n");
        else if(s_boot_reset_flags & (1u << 4)) USART_Printf("[BOOT] Reset: Window WDT\r\n");
        else if(s_boot_reset_flags & (1u << 5)) USART_Printf("[BOOT] Reset: LowPower\r\n");
        else if(s_boot_reset_flags & (1u << 1)) USART_Printf("[BOOT] Reset: NRST pin\r\n");
        else if(s_boot_reset_flags & (1u << 0)) USART_Printf("[BOOT] Reset: Power-On (POR)\r\n");
        else                                    USART_Printf("[BOOT] Reset: unknown\r\n");
        /* 无崩溃现场时打印复位标志：原始 RSTS（bit24~31，对照手册）+ 压缩位图。
         * 注：AT32F423 的 NRST 是双向复位引脚，软件复位(SYSRESETREQ)时 pad 会
         * 伴随反映到 NRSTF——因此 RSTF=0x06(NRST+SW) 是软件复位的正常表现；
         * 且 POR(bit0)=0 排除 LVR/POR（无需怀疑电源）。 */
        USART_Printf("[BOOT] RSTS=0x%02X RSTF=0x%02X\r\n", s_boot_rsts_raw, s_boot_reset_flags);
    }
}
/* add user code end 0 */


/**
  * @brief  take some delay for waiting power stable, delay is about 60ms with frequency 8MHz.
  * @param  none
  * @retval none
  */
static void wk_wait_for_power_stable(void)
{
  volatile uint32_t delay = 0;
  for(delay = 0; delay < 50000; delay++);
}

/**
  * @brief main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  /* add user code begin 1 */
    //配置错误重定向
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;
    //尽早捕获复位原因（RSTS sticky 标志），随后初始化可能清除它
    boot_capture_reset_flags();
  /* add user code end 1 */

  /* add a necessary delay to ensure that Vdd is higher than the operating
     voltage of battery powered domain (2.57V) when the battery powered 
     domain is powered on for the first time and being operated. */
  wk_wait_for_power_stable();
  
  /* system clock config. */
  wk_system_clock_config();

  /* config periph clock. */
  wk_periph_clock_config();

  /* nvic config. */
  wk_nvic_config();

  /* timebase config for
     void wk_delay_ms(uint32_t delay); */
  wk_timebase_init();

  /* init gpio function. */
  wk_gpio_config();

  /* init adc1 function. */
  wk_adc1_init();

  /* init dma1 channel1 */
  wk_dma1_channel1_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR 
     and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL1, 
                        (uint32_t)&SPI3->dt, 
                        DMA1_CHANNEL1_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL1_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL1, TRUE);

  /* init dma1 channel4 */
  wk_dma1_channel4_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR 
     and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL4, 
                        (uint32_t)&SPI1->dt, 
                        DMA1_CHANNEL4_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL4_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL4, TRUE);

  /* init dma1 channel5 */
  wk_dma1_channel5_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR 
     and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL5, 
                        (uint32_t)&SPI1->dt, 
                        DMA1_CHANNEL5_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL5_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL5, TRUE);

  /* init dma1 channel6 */
  wk_dma1_channel6_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR 
     and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL6, 
                        (uint32_t)&USART1->dt, 
                        DMA1_CHANNEL6_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL6_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL6, TRUE);

  /* init dma1 channel7 */
  wk_dma1_channel7_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR 
     and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL7, 
                        (uint32_t)&USART1->dt, 
                        DMA1_CHANNEL7_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL7_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL7, TRUE);

  /* init usart1 function. */
  wk_usart1_init();

  /* init spi1 function. */
  wk_spi1_init();

  /* init spi3 function. */
  wk_spi3_init();

  /* init i2c1 function. */
  wk_i2c1_init();

  /* init crc function. */
  wk_crc_init();

  /* init exint function. */
  wk_exint_config();

  /* init tmr1 function. */
  wk_tmr1_init();

  /* init i2c app function. */
  wk_i2c_app_init();

  /* add user code begin 2 */
    //上电 banner：复位原因 + 崩溃现场 + 项目/固件/硬件信息（USART1 已初始化）
    boot_print_banner();
  /* add user code end 2 */

  /* init freertos function. */
  wk_freertos_init();

  while(1)
  {
    /* add user code begin 3 */
    
    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */
