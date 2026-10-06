/**
 * @file lv_port_disp_template.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include <string.h>

#include "applications.h"
/*********************
 *      DEFINES
 *********************/
#define MY_DISP_HOR_RES    160
#define MY_DISP_VER_RES    40
 
#ifndef MY_DISP_HOR_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen width, default value 320 is used for now.
    #define MY_DISP_HOR_RES    320
#endif

#ifndef MY_DISP_VER_RES
    #warning Please define or replace the macro MY_DISP_VER_RES with the actual screen height, default value 240 is used for now.
    #define MY_DISP_VER_RES    240
#endif

#define BYTE_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565_SWAPPED)) /*will be 2 for RGB565 */

/* 写屏时比窗口多送的 2 个像素（4 字节）：位于窗口右下角之外，不承载图像内容，
 * 由 disp_fill_ramwr_tail() 固定填 0x10，不发送缓冲区残留的随机值。 */
#define GC9D01_RAMWR_TAIL_BYTES     4U
#define GC9D01_RAMWR_TAIL_FILL      0x10U

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/**********************
 *  STATIC VARIABLES
 **********************/
/* LVGL 渲染缓冲：160×16×2 = 5120B（RGB565，可容纳 160×16 行；≤6K，缓解移动边界残留）。
 * 尾部余量精确等于 GC9D01_RAMWR_TAIL_BYTES（4B），刚好容纳多送的 2 像素，不多留。 */
static uint8_t fb[MY_DISP_HOR_RES * 16 * 2 + GC9D01_RAMWR_TAIL_BYTES];

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();
    
    /*------------------------------------
     * Create a display and set a flush_cb
     * -----------------------------------*/
    lv_display_t * disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    
    /* Full-screen single buffer（fb 在文件作用域定义；此处只把 5120B 交给 LVGL，
       尾部 4B 余量不纳入缓冲区尺寸，留给 RAMWR 多送的 2 像素） */
    LV_ATTRIBUTE_MEM_ALIGN
    lv_display_set_buffers(disp,
                           fb,                // buf1: full screen
                           NULL,              // buf2: none (single buffer)
                           MY_DISP_HOR_RES * 16 * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 填充 RAMWR 多送 2 像素用的 4 字节尾部填充值。
 * 缓冲区尾部余量正好只有这 4B，若 px_map 不是缓冲区首地址，调用方需自行保证尾部有余量。 */
static void disp_fill_ramwr_tail(uint8_t * px_map, uint32_t px_cnt)
{
    uint8_t * tail = px_map + px_cnt;
    uint32_t i;

    for (i = 0; i < GC9D01_RAMWR_TAIL_BYTES; i++) {
        tail[i] = (uint8_t)GC9D01_RAMWR_TAIL_FILL;
    }
}

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    GC9D01_Init();
    //GC9D01_rotation(1);
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display.
 *`px_map` contains the rendered image as raw pixel map and it should be copied to `area` on the display.
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_display_flush_ready()' has to be called when it's finished.*/
static void disp_flush(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * px_map)
{
    if(!disp_flush_enabled) {
        lv_display_flush_ready(disp_drv);
        return;
    }

    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    /* RAMWR 写 w*h*2 + 4 字节（RGB565 + GC9D01 尾随 2 像素）：
     * 该 40×160 模组在 XSTART=-60 偏移下，精确 w*h 像素写不满窗口右下角
     * (x2,y2) 处 2×2（电池右下角黑点）。阻塞 SPI/精确长度均复现，已排除
     * DMA 尾部与传输丢失——是模组/芯片固有特性（非法负偏移的未定义行为）。
     * 多写 2 像素落在窗口末尾：右边界贴屏缘(x2=159)时落在面板外无影响。
     * 若局部刷新(右边界在屏内)出现毛边，需改按 x2==159 条件 +4。
     * 这 4 字节由 disp_fill_ramwr_tail() 显式填 0x10；缓冲区尾部余量正好是 4B。 */
    uint32_t px_cnt = w * h * 2;

    /* 3) 设置窗口 + 连续写入像素流 */
    GC9D01_SetWindow((uint16_t)area->x1, (uint16_t)area->y1, (uint16_t)area->x2, (uint16_t)area->y2);

    /* px_map 是 LVGL 提供的像素块起始地址（lv_refr.c 传的是缓冲区首地址）；
       RGB565 时每像素 2 字节；多送的 2 像素显式填 0x10，不读缓冲区残留 */
    disp_fill_ramwr_tail(px_map, px_cnt);
    GC9D01_RamWrite(px_map, px_cnt + GC9D01_RAMWR_TAIL_BYTES);
    
    lv_display_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
