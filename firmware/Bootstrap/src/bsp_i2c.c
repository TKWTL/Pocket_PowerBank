#include "bsp_i2c.h"

extern i2c_handle_type hi2c1;

TaskHandle_t i2c_wait_task = NULL;   // 当前等待I2C完成的任务句柄

/* I2C 总线故障恢复：复位 I2C1 外设并重新初始化。
 * 背景：从机拉死总线 / 上次传输异常未复位时 BUSYF 持续为 1，后续所有传输
 * 在启动阶段（i2c_wait_flag 轮询 BUSYF/TDIS/TDC）即超时返回错误，此时中断
 * 从未使能、不会有完成通知；若调用方永久等通知会持锁卡死整机（曾实测：
 * 全任务阻塞只剩空闲任务）。复位清 BUSYF 与错误标志，总线自动恢复；
 * NVIC 中断使能不受外设复位影响，无需重新使能。 */
static void i2c_bus_recover(void)
{
    /* 先关 I2C 事件/错误中断，避免复位过程中 ISR 访问半复位状态 */
    i2c_interrupt_enable(I2C1, I2C_ERR_INT | I2C_TDC_INT | I2C_STOP_INT | I2C_ACKFIAL_INT | I2C_TD_INT | I2C_RD_INT, FALSE);
    i2c_enable(I2C1, FALSE);
    i2c_reset(I2C1);                      /* 外设复位：清 BUSYF/错误标志 */
    NVIC_ClearPendingIRQ(I2C1_EVT_IRQn);
    NVIC_ClearPendingIRQ(I2C1_ERR_IRQn);
    hi2c1.status = 0;                 /* 清 handle 残留传输状态（0=I2C_START） */
    hi2c1.error_code = I2C_OK;
    wk_i2c1_init();                       /* 重新初始化 GPIO/时钟/寄存器 */
}

/* 发送函数：启动失败或完成通知超时 → 复位总线恢复，保证调用方必能返回释放互斥锁 */
void I2C_Transmit(uint8_t addr, uint8_t* p_buf, uint16_t len){
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (i2c_master_transmit_int(&hi2c1, addr, p_buf, len, BSP_I2C_START_WAIT_CNT) == I2C_OK) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BSP_I2C_SESSION_TIMEOUT_MS)) == 0) {
            i2c_bus_recover();   /* 等完成通知超时：总线卡死，复位恢复 */
        }
    } else {
        i2c_bus_recover();       /* 启动失败：中断未使能、不会有通知，必须复位避免永久等待 */
    }
    i2c_wait_task = NULL;
}

/* 带寄存器设置的发送函数 */
void I2C_RegWrite(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len){
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (i2c_memory_write_int(&hi2c1, I2C_MEM_ADDR_WIDIH_8, addr, reg, p_buf, len, BSP_I2C_START_WAIT_CNT) == I2C_OK) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BSP_I2C_SESSION_TIMEOUT_MS)) == 0) {
            i2c_bus_recover();
        }
    } else {
        i2c_bus_recover();
    }
    i2c_wait_task = NULL;
}

/* 读取函数 */
void I2C_Receive(uint8_t addr, uint8_t* p_buf, uint16_t len){
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (i2c_master_receive_int(&hi2c1, addr, p_buf, len, BSP_I2C_START_WAIT_CNT) == I2C_OK) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BSP_I2C_SESSION_TIMEOUT_MS)) == 0) {
            i2c_bus_recover();
        }
    } else {
        i2c_bus_recover();
    }
    i2c_wait_task = NULL;
}

/* 带寄存器设置的读取函数 */
void I2C_RegRead(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len){
    i2c_wait_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (i2c_memory_read_int(&hi2c1, I2C_MEM_ADDR_WIDIH_8, addr, reg, p_buf, len, BSP_I2C_START_WAIT_CNT) == I2C_OK) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BSP_I2C_SESSION_TIMEOUT_MS)) == 0) {
            i2c_bus_recover();
        }
    } else {
        i2c_bus_recover();
    }
    i2c_wait_task = NULL;
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