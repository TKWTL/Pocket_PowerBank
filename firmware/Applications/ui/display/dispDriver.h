/*
 * This file is part of the MiaoUI Library.
 *
 * Copyright (c) 2025, JFeng-Z, <2834294740@qq.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Created on: 2025-02-08
 */
#ifndef _DISPDRIVER_H_
#define _DISPDRIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "application.h"
    
//U8G2句柄
extern u8g2_t u8g2;

extern int Contrast;//系统亮度
    
/******************************I/O操作区开始***********************************/
void diapInit(void);
void Disp_SendBuffer(void);
void Disp_UpdateDisplayArea(uint8_t tx, uint8_t ty, uint8_t tw, uint8_t th);
void Disp_SetContrast(ui_t *ui);
void Disp_SetContrast2(uint8_t contrast);
void Disp_SetPowerSave(uint8_t is_enable);
/******************************I/O操作区结束***********************************/
    
void Disp_ClearBuffer(void);//清缓冲区
void Disp_SetFont(const uint8_t  *font);
void Disp_DrawPixel(uint16_t x, uint16_t y);//画点函数
void Disp_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
uint16_t Disp_Putchar(uint16_t x, uint16_t y, uint16_t encoding);
uint16_t Disp_DrawStr(uint16_t x, uint16_t y, const char *str);
void Disp_SetDrawColor(void *color);//设置绘制颜色
void Disp_SetBitmapMode(uint8_t is_transparent);//设置覆盖模式，必须在全屏缓冲模式下使用
void Disp_DrawFrame(uint16_t x, uint16_t y, uint16_t w, uint16_t h);//绘制中空方框
void Disp_DrawRFrame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r);//绘制圆角方框
void Disp_DrawBox(uint16_t x, uint16_t y, uint16_t w, uint16_t h);//绘制长方形
void Disp_DrawRBox(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r);//绘制圆角方形
void Disp_DrawCircle(uint16_t x, uint16_t y, uint16_t r, uint8_t opt);//绘制空心圆圈
void Disp_DrawDisc(uint16_t x, uint16_t y, uint16_t r, uint8_t opt);//绘制实心圆
void Disp_DrawXBMP(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *bitmap);
uint8_t Disp_GetBufferTileHeight(void);
uint8_t Disp_GetBufferTileWidth(void);
uint8_t *Disp_GetBufferPtr(void);
void Disp_SetClipWindow(uint16_t clip_x0, uint16_t clip_y0, uint16_t clip_x1, uint16_t clip_y1);
void Disp_SetMaxClipWindow(void);
void Disp_SetBufferCurrTileRow(uint8_t row);
uint16_t Disp_DrawUTF8(uint16_t x, uint16_t y, const char *str);
uint16_t Disp_GetUTF8Width(const char *str);

void Disp_SetRotation(uint8_t rotation);//设置屏幕的旋转方向，不改变ui_t中的方向
void Disp_ResumeRotation(ui_t *ui);//恢复屏幕的旋转方向（记录在ui_t全局结构体中）
void Disp_UpdateRotation(ui_t *ui, uint8_t rotation_state);//更新屏幕的旋转方向，并改变ui_t中的方向

/***********************以下函数须在ui_conf.c中调用****************************/
//创建显示屏模块所需的参数
void Create_Disp_Parameters (ui_t *ui);
//将显示屏模块的对象添加到菜单中
void Add_Disp_Items(ui_page_t *ParentPage);

#ifdef __cplusplus
}
#endif

#endif
