#include "applications.h"

#include "at32f423_wk_config.h"

#include <stdarg.h>

static void usart_printf(const char *format, ...)
{
    char buf[64];
    uint8_t i;
    va_list arg;

    va_start(arg, format);
    vsprintf(buf, format, arg);
    va_end(arg);

    for (i = 0; i < 64; i++) {
        if (buf[i] == 0x00) {
            break;
        }
        while (usart_flag_get(USART1, USART_TDBE_FLAG) == RESET) {
        }
        usart_data_transmit(USART1, buf[i]);
    }
}

void SW6306_task_func(void *pvParameters)
{
    (void)pvParameters;
    uint8_t i = 0;

    while (1) {
        if (SW6306_IsInitialized() == 0) {
            SW6306_ForceOff();
            SW6306_Init();
            SW6306_IextEnSet(0);
            SW6306_IextDirSet(1);
            SW6306_IextSet(20);
        }
        vTaskDelay(100);
        SW6306_ADCLoad();
        vTaskDelay(100);
        SW6306_StatusLoad();
        vTaskDelay(100);
        SW6306_PortStatusLoad();
        vTaskDelay(200);
        SW6306_PowerLoad();
        if (i == 0) {
            usart_printf("VBUS:%dmV\tIBUS:%dmA\tPBUS:%.3fW\n", SW6306_ReadVBUS(), SW6306_ReadIBUS(), SW6306_ReadVBUS() * SW6306_ReadIBUS() * 0.000001f);
        }
        if (i == 1) {
            usart_printf("VBAT:%dmV\tIBAT:%dmA\tPBAT:%.3fW\n", SW6306_ReadVBAT(), SW6306_ReadIBAT(), SW6306_ReadVBAT() * SW6306_ReadIBAT() * 0.000001f);
        }
        SW6306_CapacityLoad();
        if (i == 2) {
            usart_printf("TChip:%.1f'C\tCap:%d%%\n\n", SW6306_ReadTCHIP(), SW6306_ReadCapacity());
        }
        if (i == 4) {
            i = 0;
        } else {
            i++;
        }
    }
}
