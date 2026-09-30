#include "bsp_i2c.h"
#include "bsp_usart.h"   /* 事务失败打点：USART_SendByte('!') */

extern i2c_handle_type hi2c1;

TaskHandle_t i2c_wait_task = NULL;   // 当前等待I2C完成的任务句柄

/* I2C 故障现场（sticky）：在错误中断入口抓取，早于官方 i2c_err_irq_handler()
 * 清标志，因此调试器 Watch 中能看到“最初的错误现场”。复位后自动清零。 */
volatile uint32_t i2c_fault_sts;
volatile uint32_t i2c_fault_ctrl1;
volatile uint32_t i2c_fault_ctrl2;
volatile uint32_t i2c_fault_txdt;
volatile uint32_t i2c_fault_count;
volatile uint32_t i2c_fault_code;
volatile uint32_t i2c_recover_count;
volatile uint32_t i2c_recover_reason;

void i2c_fault_capture(void)
{
    i2c_fault_sts   = I2C1->sts;
    i2c_fault_ctrl1 = I2C1->ctrl1;
    i2c_fault_ctrl2 = I2C1->ctrl2;
    i2c_fault_txdt  = I2C1->txdt;
    i2c_fault_count++;
}

/* I2C 总线故障恢复：复位 I2C1 外设并重新初始化。
 * 背景：从机拉死总线 / 上次传输异常未复位时 BUSYF 持续为 1，后续所有传输
 * 在启动阶段（i2c_wait_flag 轮询 BUSYF/TDIS/TDC）即超时返回错误，此时中断
 * 从未使能、不会有完成通知；若调用方永久等通知会持锁卡死整机（曾实测：
 * 全任务阻塞只剩空闲任务）。复位清 BUSYF 与错误标志，总线自动恢复；
 * NVIC 中断使能不受外设复位影响，无需重新使能。 */
static void i2c_bus_recover(uint32_t reason, uint32_t detail)
{
    i2c_recover_count++;
    i2c_recover_reason = (reason << 16) | (detail & 0xFFFFU);
    /* 先关 I2C 事件/错误中断，避免复位过程中 ISR 访问半复位状态 */
    i2c_interrupt_enable(I2C1, I2C_ERR_INT | I2C_TDC_INT | I2C_STOP_INT | I2C_ACKFIAL_INT | I2C_TD_INT | I2C_RD_INT, FALSE);
    i2c_enable(I2C1, FALSE);
    i2c_reset(I2C1);                      /* 外设复位：清 BUSYF/错误标志 */
    NVIC_ClearPendingIRQ(I2C1_EVT_IRQn);
    NVIC_ClearPendingIRQ(I2C1_ERR_IRQn);
    if (hi2c1.error_code != I2C_OK) {
        i2c_fault_code = (uint32_t)hi2c1.error_code;   /* 保留错误码（TDC/TCRLD/INTERRUPT/ACKFAIL…）供 Watch 判读 */
    }
    hi2c1.status = 0;                 /* 清 handle 残留传输状态（0=I2C_START） */
    hi2c1.error_code = I2C_OK;
    wk_i2c1_init();                       /* 重新初始化 GPIO/时钟/寄存器 */
}

/* ==================== 事务监督器 ====================
 * 语义约定（关键）：中断通知只表示"本次等待条件已结束"——正常完成与错误中断都会唤醒，
 * 所以【收到通知 ≠ 成功】。成功必须由 status==I2C_END 且 error_code==I2C_OK 双确认
 * （该库在 ACKFAIL/TDC/TCRLD 等错误后也会把 status 置为 I2C_END）。
 * 失败处理：整笔重试，而不是从断点续传——恢复总线后重新调用库的 *_int() 接口，
 * 由它重新装填 addr/reg/pbuff/pcount/error_code，从第 0 字节重新开始。
 * 每次整笔失败输出一个 '!'，便于串口定位（成功路径零输出）。 */
#define BSP_I2C_ATTEMPTS   2U   /* 1 次初始 + 1 次重试 */

#define BSP_I2C_OP_MEM_RD  0U
#define BSP_I2C_OP_MEM_WR  1U
#define BSP_I2C_OP_RD      2U
#define BSP_I2C_OP_WR      3U

static i2c_status_type i2c_exec(uint8_t kind, uint8_t addr, uint8_t reg,
                                uint8_t *p_buf, uint16_t len)
{
    i2c_status_type last = I2C_OK;   /* 最近一次失败码：必须在 recover 之前捕获——
                                      * i2c_bus_recover() 会把 error_code 清成 I2C_OK，
                                      * 若在末尾再读它就会把"失败"当成功返回。 */
    uint8_t attempt;
    for (attempt = 0U; attempt < BSP_I2C_ATTEMPTS; attempt++) {
        i2c_status_type rc;

        i2c_wait_task = xTaskGetCurrentTaskHandle();
        (void)ulTaskNotifyTake(pdTRUE, 0);          /* 清上一笔残留通知 */

        switch (kind) {
        case BSP_I2C_OP_MEM_RD:
            rc = i2c_memory_read_int(&hi2c1, I2C_MEM_ADDR_WIDIH_8, addr, reg, p_buf, len, BSP_I2C_START_WAIT_CNT);
            break;
        case BSP_I2C_OP_MEM_WR:
            rc = i2c_memory_write_int(&hi2c1, I2C_MEM_ADDR_WIDIH_8, addr, reg, p_buf, len, BSP_I2C_START_WAIT_CNT);
            break;
        case BSP_I2C_OP_RD:
            rc = i2c_master_receive_int(&hi2c1, addr, p_buf, len, BSP_I2C_START_WAIT_CNT);
            break;
        default:
            rc = i2c_master_transmit_int(&hi2c1, addr, p_buf, len, BSP_I2C_START_WAIT_CNT);
            break;
        }

        if (rc != I2C_OK) {
            last = rc;
            USART_SendByte('!');
            i2c_bus_recover(BSP_I2C_RC_START_FAIL, (uint32_t)rc);
            continue;
        }
        /* 等通知超时：总线卡死（中断未使能则永不来通知） */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BSP_I2C_SESSION_TIMEOUT_MS)) == 0) {
            last = I2C_ERR_TIMEOUT;
            USART_SendByte('!');
            i2c_bus_recover(BSP_I2C_RC_NOTIFY_TIMEOUT, 0);
            continue;
        }
        /* 通知到达但事务状态异常（错误中断也会唤醒，必须查 error_code） */
        if (hi2c1.status == BSP_I2C_STATUS_END && hi2c1.error_code == I2C_OK) {
            i2c_wait_task = NULL;
            return I2C_OK;
        }
        last = (hi2c1.error_code != I2C_OK) ? hi2c1.error_code : I2C_ERR_INTERRUPT;
        USART_SendByte('!');
        i2c_bus_recover(BSP_I2C_RC_TRANS_ERROR, (uint32_t)hi2c1.error_code);
    }
    i2c_wait_task = NULL;
    return last;                  /* 两次都失败：返回最后一次库错误码（HAL 风格） */
}

/* 发送函数：整笔事务最多尝试 BSP_I2C_ATTEMPTS 次，失败返回非 I2C_OK */
i2c_status_type I2C_Transmit(uint8_t addr, uint8_t* p_buf, uint16_t len){
    return i2c_exec(BSP_I2C_OP_WR, addr, 0U, p_buf, len);
}

/* 带寄存器设置的发送函数 */
i2c_status_type I2C_RegWrite(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len){
    return i2c_exec(BSP_I2C_OP_MEM_WR, addr, reg, p_buf, len);
}

/* 读取函数 */
i2c_status_type I2C_Receive(uint8_t addr, uint8_t* p_buf, uint16_t len){
    return i2c_exec(BSP_I2C_OP_RD, addr, 0U, p_buf, len);
}

/* 带寄存器设置的读取函数 */
i2c_status_type I2C_RegRead(uint8_t addr, uint8_t reg, uint8_t* p_buf, uint16_t len){
    return i2c_exec(BSP_I2C_OP_MEM_RD, addr, reg, p_buf, len);
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