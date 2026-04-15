/* add user code begin Header */
/**
  **************************************************************************
  * @file     wk_i2c_app.c
  * @brief    i2c device app
  **************************************************************************
  * Copyright (c) 2025, Artery Technology, All rights reserved.
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

#include "i2c_app.h"

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

#define I2C1_MASTER_TRANSMIT_ADDRESS                         0x78
#define I2C1_MASTER_TRANSMIT_NUM                             1
#define I2C1_MASTER_TRANSMIT_TIMEOUT                         0xFFFFFFFF

#define I2C1_MASTER_RECEIVE_ADDRESS                          0x78
#define I2C1_MASTER_RECEIVE_NUM                              1
#define I2C1_MASTER_RECEIVE_TIMEOUT                          0xFFFFFFFF

uint8_t i2c1_master_transmit_buf[I2C1_MASTER_TRANSMIT_NUM] = {0};
uint8_t i2c1_master_receive_buf[I2C1_MASTER_RECEIVE_NUM] = {0};

i2c_handle_type hi2c1;

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/**
  * @brief  i2c application initialization
  * @param  none
  * @retval none
  */
void wk_i2c_app_init(void)
{
  /* add user code begin i2c_app_init 0 */

  /* add user code end i2c_app_init 0 */

  hi2c1.i2cx = I2C1;

  /* add user code begin i2c_app_init 1 */

  /* add user code end i2c_app_init 1 */

  /* add user code begin i2c_app_init 2 */

  /* add user code end i2c_app_init 2 */
}

/**
  * @brief  i2c application task
  * @param  none
  * @retval none
  */
void wk_i2c_app_task(void)
{
  /* add user code begin i2c_app_task 0 */

  /* add user code end i2c_app_task 0 */

  /* start i2c1 master transmit process */
  if(i2c_master_transmit_int(&hi2c1, I2C1_MASTER_TRANSMIT_ADDRESS, i2c1_master_transmit_buf, I2C1_MASTER_TRANSMIT_NUM, I2C1_MASTER_TRANSMIT_TIMEOUT) != I2C_OK)
  {
    /* add user code begin i2c1_master_transmit error process */

    /* add user code end i2c1_master_transmit error process */
  }

  /* wait for the communication to end */
  if(i2c_wait_end(&hi2c1, I2C1_MASTER_TRANSMIT_TIMEOUT) != I2C_OK)
  {
    /* add user code begin i2c1_master_transmit wait_end error process */

    /* add user code end i2c1_master_transmit wait_end error process */
  }

  /* start i2c1 master receive process */
  if(i2c_master_receive_int(&hi2c1, I2C1_MASTER_RECEIVE_ADDRESS, i2c1_master_receive_buf, I2C1_MASTER_RECEIVE_NUM, I2C1_MASTER_RECEIVE_TIMEOUT) != I2C_OK)
  {
    /* add user code begin i2c1_master_receive error process */

    /* add user code end i2c1_master_receive error process */
  }

  /* wait for the communication to end */
  if(i2c_wait_end(&hi2c1, I2C1_MASTER_RECEIVE_TIMEOUT) != I2C_OK)
  {
    /* add user code begin i2c1_master_receive wait_end error process */

    /* add user code end i2c1_master_receive wait_end error process */
  }

  /* add user code begin i2c_app_task 1 */

  /* add user code end i2c_app_task 1 */

  /* add user code begin i2c_app_task 2 */

  /* add user code end i2c_app_task 2 */

  /* add user code begin i2c_app_task 3 */

  /* add user code end i2c_app_task 3 */
}

/* add user code begin 1 */

/* add user code end 1 */
