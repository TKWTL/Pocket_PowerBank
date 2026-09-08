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
 * +8B 尾随余量：GC9D01 写屏实测需比窗口 w*h 多 2 像素（4B），给 px_map 越界读留安全空间。
 * 放文件作用域，初始化清屏时也复用这块缓冲。 */
static uint8_t fb[MY_DISP_HOR_RES * 16 * 2 + 8];

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
    
    /* Full-screen single buffer（fb 在文件作用域定义，5120B 不含尾随余量） */
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

/* GC9D01 上电时整片 RAM 是随机值，而 LVGL 只刷新被标记失效的区域：
 * 面板上从未被刷到的位置（如右边界中段、右下角）会一直显示上电白点/垃圾。
 * 这里初始化时全屏清黑一次，消除所有残留。
 * 注意：160×40×2 = 12.8KB 无法一次静态缓冲，用 5120B 缓冲分 3 段清（16/16/8 行）。 */
static void disp_clear_screen(void)
{
    uint16_t r, h;

    for (r = 0; r < MY_DISP_VER_RES; r += 16) {
        h = (MY_DISP_VER_RES - r < 16) ? (MY_DISP_VER_RES - r) : 16;
        memset(fb, 0, sizeof(fb));
        GC9D01_SetWindow(0, r, MY_DISP_HOR_RES - 1, r + h - 1);
        /* 与 disp_flush 一致：多写 2 像素（+4 字节），确保窗口右下角写满，不留上电残留 */
        GC9D01_RamWrite(fb, h * MY_DISP_HOR_RES * 2 + 4);
    }
}

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    GC9D01_Init();
    //GC9D01_rotation(1);
    disp_clear_screen();
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
     * fb 已加 8B 尾随余量，px_map 读取不会越界。 */
    uint32_t px_cnt = w * h * 2;

    /* 3) 设置窗口 + 连续写入像素流 */
    GC9D01_SetWindow((uint16_t)area->x1, (uint16_t)area->y1, (uint16_t)area->x2, (uint16_t)area->y2);

    /* px_map 是 LVGL 提供的像素块起始地址；
       RGB565 时每像素 2 字节，所以直接当 uint16_t* 用 */
    GC9D01_RamWrite(px_map, px_cnt + 4);
    
    lv_display_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
