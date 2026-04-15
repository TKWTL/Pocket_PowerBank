/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f423_wk_config.c
  * @brief    work bench config program
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

#include "at32f423_wk_config.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */

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

/**
  * @brief  system clock config program
  * @note   the system clock is configured as follow:
  *         system clock (sclk)   = (hick / 6 * pll_ns)/(pll_ms * pll_fr)
  *         system clock source   = HICK_VALUE
  *         - lick                = on
  *         - lext                = off
  *         - hick                = on
  *         - hext                = off
  *         - sclk                = 150000000
  *         - ahbdiv              = 1
  *         - ahbclk              = 150000000
  *         - apb1div             = 2
  *         - apb1clk             = 75000000
  *         - apb2div             = 2
  *         - apb2clk             = 75000000
  *         - pll_ns              = 75
  *         - pll_ms              = 1
  *         - pll_fr              = 2
  *         - flash_wtcyc         = 4 cycle
  * @param  none
  * @retval none
  */
void wk_system_clock_config(void)
{
  /* reset crm */
  crm_reset();

  /* config flash psr register */
  flash_psr_set(FLASH_WAIT_CYCLE_4);

  /* enable pwc periph clock */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
  
  /* config ldo voltage */
  pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);

  /* enable lick */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_LICK, TRUE);

  /* wait till lick is ready */
  while(crm_flag_get(CRM_LICK_STABLE_FLAG) != SET)
  {
  }

  /* enable hick */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);

  /* wait till hick is ready */
  while(crm_flag_get(CRM_HICK_STABLE_FLAG) != SET)
  {
  }

  /* config pll clock resource
  common frequency config list: pll source selected  hick or hext(8mhz)
  _____________________________________________________________________________
  |        |         |         |         |         |         |        |        |
  | sysclk |   150   |   144   |   120   |   108   |   96    |   72   |   36   |
  |________|_________|_________|_________|_________|_________|_________________|
  |        |         |         |         |         |         |        |        |
  |pll_ns  |   75    |   72    |   120   |   108   |   96    |   72   |   72   |
  |        |         |         |         |         |         |        |        |
  |pll_ms  |   1     |   1     |   1     |   1     |   1     |   1    |   1    |
  |        |         |         |         |         |         |        |        |
  |pll_fr  |   FR_2  |   FR_2  |   FR_4  |   FR_4  |   FR_4  |   FR_4 |   FR_8 |
  |________|_________|_________|_________|_________|_________|________|________|

  if pll clock source selects hext with other frequency values, or configure pll to other
  frequency values, please use the at32 new clock  configuration tool for configuration. */
  crm_pll_config(CRM_PLL_SOURCE_HICK, 75, 1, CRM_PLL_FR_2);

  /* enable pll */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);

  /* wait till pll is ready */
  while(crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
  {
  }

  /* config ahbclk */
  crm_ahb_div_set(CRM_AHB_DIV_1);

  /* config apb2clk, the maximum frequency of APB2 clock is 150 MHz  */
  crm_apb2_div_set(CRM_APB2_DIV_2);

  /* config apb1clk, the maximum frequency of APB1 clock is 120 MHz  */
  crm_apb1_div_set(CRM_APB1_DIV_2);

  /* enable auto step mode */
  crm_auto_step_mode_enable(TRUE);

  /* select pll as system clock source */
  crm_sysclk_switch(CRM_SCLK_PLL);

  /* wait till pll is used as system clock source */
  while(crm_sysclk_switch_status_get() != CRM_SCLK_PLL)
  {
  }

  /* disable auto step mode */
  crm_auto_step_mode_enable(FALSE);

  /* update system_core_clock global variable */
  system_core_clock_update();
}

/**
  * @brief  config periph clock
  * @param  none
  * @retval none
  */
void wk_periph_clock_config(void)
{
  /* enable gpioa periph clock */
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

  /* enable gpiob periph clock */
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  /* enable gpiof periph clock */
  crm_periph_clock_enable(CRM_GPIOF_PERIPH_CLOCK, TRUE);

  /* enable crc periph clock */
  crm_periph_clock_enable(CRM_CRC_PERIPH_CLOCK, TRUE);

  /* enable dma1 periph clock */
  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

  /* enable spi3 periph clock */
  crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);

  /* enable i2c1 periph clock */
  crm_periph_clock_enable(CRM_I2C1_PERIPH_CLOCK, TRUE);

  /* enable tmr1 periph clock */
  crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);

  /* enable usart1 periph clock */
  crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);

  /* enable adc1 periph clock */
  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);

  /* enable spi1 periph clock */
  crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);

  /* enable scfg periph clock */
  crm_periph_clock_enable(CRM_SCFG_PERIPH_CLOCK, TRUE);
}

/**
  * @brief  nvic config
  * @param  none
  * @retval none
  */
void wk_nvic_config(void)
{
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  NVIC_SetPriority(MemoryManagement_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(BusFault_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(UsageFault_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(SVCall_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(DebugMonitor_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(PendSV_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));
  NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));
  nvic_irq_enable(EXINT0_IRQn, 5, 0);
  nvic_irq_enable(EXINT1_IRQn, 5, 0);
  nvic_irq_enable(EXINT2_IRQn, 5, 0);
  nvic_irq_enable(DMA1_Channel1_IRQn, 5, 0);
  nvic_irq_enable(EXINT9_5_IRQn, 5, 0);
  nvic_irq_enable(I2C1_EVT_IRQn, 5, 0);
  nvic_irq_enable(I2C1_ERR_IRQn, 5, 0);
}

/**
  * @brief  init gpio_input/gpio_output/gpio_analog/eventout function.
  * @param  none
  * @retval none
  */
void wk_gpio_config(void)
{
  /* add user code begin gpio_config 0 */

  /* add user code end gpio_config 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin gpio_config 1 */

  /* add user code end gpio_config 1 */

  /* gpio output config */
  gpio_bits_reset(GPIOA, WP_PIN | DC_PIN);
  gpio_bits_set(SRST_GPIO_PORT, SRST_PIN);
  gpio_bits_reset(GPIOB, GPIO_PINS_4);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = WP_PIN | SRST_PIN | DC_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* gpio analog config */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOF, &gpio_init_struct);

  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_1 | GPIO_PINS_2;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* add user code begin gpio_config 2 */

  /* add user code end gpio_config 2 */
}

/**
  * @brief  init exint function.
  * @param  none
  * @retval none
  */
void wk_exint_config(void)
{
  /* add user code begin exint_config 0 */

  /* add user code end exint_config 0 */

  gpio_init_type gpio_init_struct;
  exint_init_type exint_init_struct;

  /* add user code begin exint_config 1 */

  /* add user code end exint_config 1 */

  /* configure the EXINT0 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = NEXT_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(NEXT_GPIO_PORT, &gpio_init_struct);

  scfg_exint_line_config(SCFG_PORT_SOURCE_GPIOA, SCFG_PINS_SOURCE0);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_0;
  exint_init_struct.line_polarity = EXINT_TRIGGER_RISING_EDGE;
  exint_init(&exint_init_struct);

  /* configure the EXINT1 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = CONF_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init(CONF_GPIO_PORT, &gpio_init_struct);

  scfg_exint_line_config(SCFG_PORT_SOURCE_GPIOB, SCFG_PINS_SOURCE1);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_1;
  exint_init_struct.line_polarity = EXINT_TRIGGER_FALLING_EDGE;
  exint_init(&exint_init_struct);

  /* configure the EXINT2 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = MENU_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init(MENU_GPIO_PORT, &gpio_init_struct);

  scfg_exint_line_config(SCFG_PORT_SOURCE_GPIOB, SCFG_PINS_SOURCE2);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_2;
  exint_init_struct.line_polarity = EXINT_TRIGGER_FALLING_EDGE;
  exint_init(&exint_init_struct);

  /* configure the EXINT8 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = INT_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init(INT_GPIO_PORT, &gpio_init_struct);

  scfg_exint_line_config(SCFG_PORT_SOURCE_GPIOB, SCFG_PINS_SOURCE8);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_8;
  exint_init_struct.line_polarity = EXINT_TRIGGER_FALLING_EDGE;
  exint_init(&exint_init_struct);

  /* add user code begin exint_config 2 */

  /* add user code end exint_config 2 */
}

/**
  * @brief  init usart1 function
  * @param  none
  * @retval none
  */
void wk_usart1_init(void)
{
  /* add user code begin usart1_init 0 */

  /* add user code end usart1_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin usart1_init 1 */

  /* add user code end usart1_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_7);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE10, GPIO_MUX_7);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_10;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* config usart1 clock source */
  crm_usart_clock_select(CRM_USART1, CRM_USART_CLOCK_SOURCE_PCLK);

  /* configure param */
  usart_init(USART1, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART1, TRUE);
  usart_receiver_enable(USART1, TRUE);
  usart_parity_selection_config(USART1, USART_PARITY_NONE);

  usart_dma_transmitter_enable(USART1, TRUE);

  usart_dma_receiver_enable(USART1, TRUE);

  usart_hardware_flow_control_set(USART1, USART_HARDWARE_FLOW_NONE);

  /* add user code begin usart1_init 2 */

  /* add user code end usart1_init 2 */

  usart_enable(USART1, TRUE);

  /* add user code begin usart1_init 3 */

  /* add user code end usart1_init 3 */
}

/**
  * @brief  init i2c1 function.
  * @param  none
  * @retval none
  */
void wk_i2c1_init(void)
{
  /* add user code begin i2c1_init 0 */

  /* add user code end i2c1_init 0 */

  gpio_init_type gpio_init_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin i2c1_init 1 */

  /* add user code end i2c1_init 1 */

  /* configure the SCL pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE6, GPIO_MUX_4);
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the SDA pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE7, GPIO_MUX_4);
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_UP;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_pins = GPIO_PINS_7;
  gpio_init(GPIOB, &gpio_init_struct);

  /* config i2c1 clock source */
  crm_i2c_clock_select(CRM_I2C1, CRM_I2C_CLOCK_SOURCE_PCLK);
  
  i2c_init(I2C1, 10, 0x20B00F1B);
  i2c_own_address1_set(I2C1, I2C_ADDRESS_MODE_7BIT, 0x0);
  i2c_ack_enable(I2C1, TRUE);
  i2c_clock_stretch_enable(I2C1, TRUE);
  i2c_general_call_enable(I2C1, FALSE);
  
  i2c_analog_filter_enable(I2C1, TRUE);

  /* add user code begin i2c1_init 2 */

  /* add user code end i2c1_init 2 */

  i2c_enable(I2C1, TRUE);

  /* add user code begin i2c1_init 3 */

  /* add user code end i2c1_init 3 */
}

/**
  * @brief  init adc1 function.
  * @param  none
  * @retval none
  */
void wk_adc1_init(void)
{
  /* add user code begin adc1_init 0 */

  /* add user code end adc1_init 0 */

  adc_common_config_type adc_common_struct;  
  adc_base_config_type adc_base_struct;

  /* add user code begin adc1_init 1 */

  /* add user code end adc1_init 1 */

  adc_reset();
  /* adc_common_settings--------------------------------------------------------------*/
  /* config adc clock source */
  crm_adc_clock_select(CRM_ADC_CLOCK_SOURCE_HCLK);

  adc_common_default_para_init(&adc_common_struct);  
  /* config adc clock division */
  adc_common_struct.div = ADC_HCLK_DIV_15;
  /* config inner temperature sensor and vintrv */
  adc_common_struct.tempervintrv_state = TRUE;
  adc_common_config(&adc_common_struct);  
  
  /* adc_settings------------------------------------------------------------------- */
  adc_base_default_para_init(&adc_base_struct);
  adc_base_struct.sequence_mode = FALSE;
  adc_base_struct.repeat_mode = FALSE;
  adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
  adc_base_struct.ordinary_channel_length = 1;
  adc_base_config(ADC1, &adc_base_struct);

  adc_resolution_set(ADC1, ADC_RESOLUTION_12B);

  /* adc_ordinary_conversionmode---------------------------------------------------- */
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_17, 1, ADC_SAMPLETIME_6_5);

  /* When "ADC_ORDINARY_TRIG_SOFTWARE" is selected, user can only use software trigger. \
  The software trigger function is adc_ordinary_software_trigger_enable(ADCx, TRUE); */
  adc_ordinary_conversion_trigger_set(ADC1, ADC_ORDINARY_TRIG_SOFTWARE, ADC_ORDINARY_TRIG_EDGE_NONE);

  /* add user code begin adc1_init 2 */

  /* add user code end adc1_init 2 */

  adc_enable(ADC1, TRUE);
  while(adc_flag_get(ADC1, ADC_RDY_FLAG) == RESET);

  /* adc calibration---------------------------------------------------------------- */
  adc_calibration_init(ADC1);
  while(adc_calibration_init_status_get(ADC1));
  adc_calibration_start(ADC1);
  while(adc_calibration_status_get(ADC1));

  /* add user code begin adc1_init 3 */

  /* add user code end adc1_init 3 */
}

/**
  * @brief  init tmr1 function.
  * @param  none
  * @retval none
  */
void wk_tmr1_init(void)
{
  /* add user code begin tmr1_init 0 */

  /* add user code end tmr1_init 0 */

  gpio_init_type gpio_init_struct;
  tmr_output_config_type tmr_output_struct;
  tmr_brkdt_config_type tmr_brkdt_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin tmr1_init 1 */

  /* add user code end tmr1_init 1 */

  /* configure the tmr1 CH2C pin */
  gpio_pin_mux_config(LED_GPIO_PORT, GPIO_PINS_SOURCE0, GPIO_MUX_1);
  gpio_init_struct.gpio_pins = LED_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(LED_GPIO_PORT, &gpio_init_struct);

  /* configure the tmr1 CH4 pin */
  gpio_pin_mux_config(BLK_GPIO_PORT, GPIO_PINS_SOURCE11, GPIO_MUX_1);
  gpio_init_struct.gpio_pins = BLK_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(BLK_GPIO_PORT, &gpio_init_struct);

  /* configure counter settings */
  tmr_cnt_dir_set(TMR1, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR1, TMR_CLOCK_DIV1);
  tmr_repetition_counter_set(TMR1, 0);
  tmr_period_buffer_enable(TMR1, FALSE);
  tmr_base_init(TMR1, 1023, 0);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR1, FALSE);
  tmr_primary_mode_select(TMR1, TMR_PRIMARY_SEL_RESET);

  /* configure channel 2 output settings */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  tmr_output_struct.oc_output_state = FALSE;
  tmr_output_struct.occ_output_state = TRUE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;
  tmr_output_struct.occ_idle_state = FALSE;
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_2, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_2, 0);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_2, TRUE);

  tmr_output_channel_immediately_set(TMR1, TMR_SELECT_CHANNEL_2, FALSE);

  /* configure channel 4 output settings */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = FALSE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;
  tmr_output_struct.occ_idle_state = FALSE;
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_4, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_4, 512);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_4, TRUE);

  tmr_output_channel_immediately_set(TMR1, TMR_SELECT_CHANNEL_4, FALSE);

  /* configure break and dead-time settings */
  tmr_brkdt_struct.brk_enable = FALSE;
  tmr_brkdt_struct.auto_output_enable = FALSE;
  tmr_brkdt_struct.brk_polarity = TMR_BRK_INPUT_ACTIVE_LOW;
  tmr_brkdt_struct.fcsoen_state = FALSE;
  tmr_brkdt_struct.fcsodis_state = FALSE;
  tmr_brkdt_struct.wp_level = TMR_WP_OFF;
  tmr_brkdt_struct.deadtime = 0;
  tmr_brkdt_config(TMR1, &tmr_brkdt_struct);
  
  tmr_brk_filter_value_set(TMR1, 0);


  tmr_output_enable(TMR1, TRUE);

  tmr_counter_enable(TMR1, TRUE);

  /* add user code begin tmr1_init 2 */

  /* add user code end tmr1_init 2 */
}

/**
  * @brief  init spi1 function
  * @param  none
  * @retval none
  */
void wk_spi1_init(void)
{
  /* add user code begin spi1_init 0 */

  /* add user code end spi1_init 0 */

  gpio_init_type gpio_init_struct;
  spi_init_type spi_init_struct;

  gpio_default_para_init(&gpio_init_struct);
  spi_default_para_init(&spi_init_struct);

  /* add user code begin spi1_init 1 */

  /* add user code end spi1_init 1 */

  /* configure the SCK pin */
  gpio_pin_mux_config(FSCK_GPIO_PORT, GPIO_PINS_SOURCE5, GPIO_MUX_5);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = FSCK_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(FSCK_GPIO_PORT, &gpio_init_struct);

  /* configure the MISO pin */
  gpio_pin_mux_config(FMISO_GPIO_PORT, GPIO_PINS_SOURCE6, GPIO_MUX_5);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = FMISO_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(FMISO_GPIO_PORT, &gpio_init_struct);

  /* configure the MOSI pin */
  gpio_pin_mux_config(FMOSI_GPIO_PORT, GPIO_PINS_SOURCE7, GPIO_MUX_5);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = FMOSI_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(FMOSI_GPIO_PORT, &gpio_init_struct);

  /* configure the CS pin */
  gpio_pin_mux_config(FCS_GPIO_PORT, GPIO_PINS_SOURCE4, GPIO_MUX_5);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = FCS_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(FCS_GPIO_PORT, &gpio_init_struct);

  /* configure param */
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.mclk_freq_division = SPI_MCLK_DIV_3;
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_HARDWARE_MODE;
  spi_init(SPI1, &spi_init_struct);

  /* configure the cs pin output */
  spi_hardware_cs_output_enable(SPI1, TRUE);

  spi_i2s_dma_transmitter_enable(SPI1, TRUE);

  spi_i2s_dma_receiver_enable(SPI1, TRUE);

  /* add user code begin spi1_init 2 */

  /* add user code end spi1_init 2 */

  spi_enable(SPI1, TRUE);

  /* add user code begin spi1_init 3 */

  /* add user code end spi1_init 3 */
}

/**
  * @brief  init spi3 function
  * @param  none
  * @retval none
  */
void wk_spi3_init(void)
{
  /* add user code begin spi3_init 0 */

  /* add user code end spi3_init 0 */

  gpio_init_type gpio_init_struct;
  spi_init_type spi_init_struct;

  gpio_default_para_init(&gpio_init_struct);
  spi_default_para_init(&spi_init_struct);

  /* add user code begin spi3_init 1 */

  /* add user code end spi3_init 1 */

  /* configure the SCK pin */
  gpio_pin_mux_config(SCK_GPIO_PORT, GPIO_PINS_SOURCE3, GPIO_MUX_6);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = SCK_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(SCK_GPIO_PORT, &gpio_init_struct);

  /* configure the MOSI pin */
  gpio_pin_mux_config(MOSI_GPIO_PORT, GPIO_PINS_SOURCE5, GPIO_MUX_6);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = MOSI_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(MOSI_GPIO_PORT, &gpio_init_struct);

  /* configure the CS pin */
  gpio_pin_mux_config(CS_GPIO_PORT, GPIO_PINS_SOURCE15, GPIO_MUX_6);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = CS_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(CS_GPIO_PORT, &gpio_init_struct);

  /* configure param */
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.mclk_freq_division = SPI_MCLK_DIV_3;
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_HARDWARE_MODE;
  spi_init(SPI3, &spi_init_struct);

  /* configure the cs pin output */
  spi_hardware_cs_output_enable(SPI3, TRUE);

  spi_i2s_dma_transmitter_enable(SPI3, TRUE);

  /* add user code begin spi3_init 2 */

  /* add user code end spi3_init 2 */

  spi_enable(SPI3, TRUE);

  /* add user code begin spi3_init 3 */

  /* add user code end spi3_init 3 */
}

/**
  * @brief  init crc function.
  * @param  none
  * @retval none
  */
void wk_crc_init(void)
{
  /* add user code begin crc_init 0 */

  /* add user code end crc_init 0 */

  crc_init_data_set(0xFFFFFFFF);
  crc_poly_size_set(CRC_POLY_SIZE_32B);
  crc_poly_value_set(0x04C11DB7);
  crc_reverse_input_data_set(CRC_REVERSE_INPUT_NO_AFFECTE);
  crc_reverse_output_data_set(CRC_REVERSE_OUTPUT_NO_AFFECTE);
  crc_data_reset();

  /* add user code begin crc_init 1 */

  /* add user code end crc_init 1 */
}

/**
  * @brief  init dma1 channel1 for "spi3_tx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel1_init(void)
{
  /* add user code begin dma1_channel1 0 */

  /* add user code end dma1_channel1 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL1);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL1, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL1, DMAMUX_DMAREQ_ID_SPI3_TX);

  /* enable dma1 channel1 full data transfer interrupt */
  dma_interrupt_enable(DMA1_CHANNEL1, DMA_FDT_INT, TRUE);

  /* add user code begin dma1_channel1 1 */

  /* add user code end dma1_channel1 1 */
}

/**
  * @brief  init dma1 channel4 for "spi1_tx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel4_init(void)
{
  /* add user code begin dma1_channel4 0 */

  /* add user code end dma1_channel4 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL4);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL4, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL4, DMAMUX_DMAREQ_ID_SPI1_TX);

  /* add user code begin dma1_channel4 1 */

  /* add user code end dma1_channel4 1 */
}

/**
  * @brief  init dma1 channel5 for "spi1_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel5_init(void)
{
  /* add user code begin dma1_channel5 0 */

  /* add user code end dma1_channel5 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL5);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL5, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL5, DMAMUX_DMAREQ_ID_SPI1_RX);

  /* add user code begin dma1_channel5 1 */

  /* add user code end dma1_channel5 1 */
}

/**
  * @brief  init dma1 channel6 for "usart1_tx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel6_init(void)
{
  /* add user code begin dma1_channel6 0 */

  /* add user code end dma1_channel6 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL6);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL6, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL6, DMAMUX_DMAREQ_ID_USART1_TX);

  /* add user code begin dma1_channel6 1 */

  /* add user code end dma1_channel6 1 */
}

/**
  * @brief  init dma1 channel7 for "usart1_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel7_init(void)
{
  /* add user code begin dma1_channel7 0 */

  /* add user code end dma1_channel7 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL7);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL7, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL7, DMAMUX_DMAREQ_ID_USART1_RX);

  /* add user code begin dma1_channel7 1 */

  /* add user code end dma1_channel7 1 */
}

/**
  * @brief  config dma channel transfer parameter
  * @param  dmax_channely: DMAx_CHANNELy
  * @param  peripheral_base_addr: peripheral address.
  * @param  memory_base_addr: memory address.
  * @param  buffer_size: buffer size.
  * @retval none
  */
void wk_dma_channel_config(dma_channel_type* dmax_channely, uint32_t peripheral_base_addr, uint32_t memory_base_addr, uint16_t buffer_size)
{
  /* add user code begin dma_channel_config 0 */

  /* add user code end dma_channel_config 0 */

  dmax_channely->dtcnt = buffer_size;
  dmax_channely->paddr = peripheral_base_addr;
  dmax_channely->maddr = memory_base_addr;

  /* add user code begin dma_channel_config 1 */

  /* add user code end dma_channel_config 1 */
}

/* add user code begin 1 */

/* add user code end 1 */
