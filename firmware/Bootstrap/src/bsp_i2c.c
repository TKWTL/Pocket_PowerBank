#include "bsp_i2c.h"

extern i2c_handle_type hi2c1;

TaskHandle_t i2c_wait_task = NULL;   // 当前等待I2C完成的任务句柄

//发送函数
void I2C_Transmit(uint8_t addr, uint8_t* p_buf, uint16_t len){
    //xSemaphoreTake(mutex_i2c_handle, portMAX_DELAY);
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    i2c_master_transmit_int(&hi2c1, addr, p_buf, len, 0xFFFFFFFF);
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    i2c_wait_task = NULL;
    //xSemaphoreGive(mutex_i2c_handle);
}

//带寄存器设置的发送函数
void I2C_RegWrite(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len){
    //xSemaphoreTake(mutex_i2c_handle, portMAX_DELAY);
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    i2c_memory_write_int(&hi2c1, I2C_MEM_ADDR_WIDIH_8, addr, reg, p_buf, len, 0xFFFFFFFF);
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    i2c_wait_task = NULL;
    //xSemaphoreGive(mutex_i2c_handle);
}

//读取函数
void I2C_Receive(uint8_t addr, uint8_t* p_buf, uint16_t len){
    //xSemaphoreTake(mutex_i2c_handle, portMAX_DELAY);
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    i2c_master_receive_int(&hi2c1, addr, p_buf, len, 0xFFFFFFFF);
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    i2c_wait_task = NULL;
    //xSemaphoreGive(mutex_i2c_handle);
}

//带寄存器设置的读取函数
void I2C_RegRead(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len){
    //xSemaphoreTake(mutex_i2c_handle, portMAX_DELAY);
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    i2c_memory_read_int(&hi2c1, I2C_MEM_ADDR_WIDIH_8, addr, reg, p_buf, len, 0xFFFFFFFF);
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    i2c_wait_task = NULL;
    //xSemaphoreGive(mutex_i2c_handle);
}


//确认地址存在函数
uint8_t I2C_IsReady(uint8_t addr){
    uint8_t ready, i2cbuf;
    xSemaphoreTake(mutex_i2c_handle, portMAX_DELAY);
    if(i2c_master_transmit(&hi2c1, addr, &i2cbuf, 1, 0x249F0) == I2C_OK) ready = 1;
    else ready = 0;
    xSemaphoreGive(mutex_i2c_handle);
    return ready;
}

//I2C中断函数示例，实际位置在_int.c文件中
#if 0
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
            );x
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  /* add user code end I2C1_ERR_IRQ 1 */
}
#endif