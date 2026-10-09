#include "bsp_usart.h"
#include "mini_format.h"

static uint8_t s_init_done;

/* 环形发送缓冲区 (128 字节，约 11ms @115200) */
static uint8_t          tx_buf[BSP_USART_TX_BUF_SIZE];
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static volatile uint16_t tx_active_len;   /* 当前 DMA 正在发送的长度 */

/* 启动 DMA 搬运下一段连续数据 */
static void usart_start_tx_dma(void)
{
    uint16_t head = tx_head;
    uint16_t tail = tx_tail;
    uint16_t cnt;

    /* 首次启动时使能 DMA 传送完成中断。
     * 必须先清 FDT 标志再开中断：main.c 启动时曾以 0 长度使能过 DMA，
     * FDT 标志可能已是置位状态，若带着遗留标志开中断会立刻进 ISR，
     * 而此时 tx_active_len 尚为 0/旧值，会造成 tx_head 误推进。 */
    if (s_init_done == 0) {
        s_init_done = 1;
        dma_flag_clear(DUART_TX_FDT);
        dma_interrupt_enable(DUART_DMATX_CH, DMA_FDT_INT, TRUE);
    }

    if (head == tail) {
        /* 缓冲区已空，停止 DMA */
        dma_channel_enable(DUART_DMATX_CH, FALSE);
        return;
    }

    /* 计算从 head 到 tail（或缓冲区末端）的连续长度 */
    if (head < tail) {
        cnt = tail - head;
    } else {
        cnt = BSP_USART_TX_BUF_SIZE - head;
    }

    tx_active_len = cnt;

    dma_channel_enable(DUART_DMATX_CH, FALSE);
    dma_flag_clear(DUART_TX_FDT);
    DUART_DMATX_CH->paddr = (uint32_t)&(DUART->dt);
    DUART_DMATX_CH->maddr = (uint32_t)&tx_buf[head];
    dma_data_number_set(DUART_DMATX_CH, cnt);
    dma_channel_enable(DUART_DMATX_CH, TRUE);
}

/* 向环形缓冲区写入一个字节 */
static void usart_tx_write(uint8_t data)
{
    uint16_t next = (tx_tail + 1) & (BSP_USART_TX_BUF_SIZE - 1);

    /* 缓冲区满则阻塞等待 */
    while (next == (uint16_t)tx_head);

    tx_buf[tx_tail] = data;
    tx_tail = next;

    /* 首次或 DMA 空闲时启动传输 */
    if (s_init_done == 0 || DUART_DMATX_CH->ctrl_bit.chen == FALSE) {
        usart_start_tx_dma();
    }
}

void USART_SendByte(uint8_t data)
{
    usart_tx_write(data);
}

void USART_SendString(const char *str)
{
    while (*str) {
        usart_tx_write((uint8_t)*str++);
    }
}

static void usart_format_putc(char ch, void *ctx)
{
    (void)ctx;
    usart_tx_write((uint8_t)ch);
}

void USART_Printf(const char *format, ...)
{
    va_list arg;

    va_start(arg, format);
    (void)mini_vformat(usart_format_putc, NULL, format, arg);
    va_end(arg);
}

/* 被 at32f423_int.c 中的 DMA1_Channel6_IRQHandler 调用 */
void USART_TxIRQHandler(void)
{
    if (dma_flag_get(DUART_TX_FDT) != RESET) {
        dma_flag_clear(DUART_TX_FDT);
        tx_head = (tx_head + tx_active_len) & (BSP_USART_TX_BUF_SIZE - 1);
        usart_start_tx_dma();
    }
}
