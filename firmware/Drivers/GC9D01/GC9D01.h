/*
  ******************************************************************************
  * @file    	GC9D01.h
  * @brief   	GC9D01 单色 TFT 液晶驱动（本项目用 0.99" 160 x 40，RGB565）声明
  * @note    	LVGL 通过 Applications/LVGL/porting/lv_port_disp.c 调用本驱动，
  *          	只用到 GC9D01_Init / GC9D01_SetWindow / GC9D01_RamWrite。
  *          	字体（fonts.c/.h）与点阵图（bitmap.h）已删除：本项目全部文字
  *          	由 LVGL 用内置字库渲染，不经本驱动画字符。
  * @par 参考来源
  *          	上游：Golinskiy Konstantin 的 STM32 GC9D01 驱动库
  *          	作者：Golinskiy Konstantin  邮箱：golinskiy.konstantin\@gmail.com
  *          	（本文件为上游版本的中文化注释 + 裁剪版）
  ******************************************************************************
 */
 
 
#ifndef _GC9D01_H
#define _GC9D01_H


/* C++ detection */
#ifdef __cplusplus
extern C {
#endif
// 上游要求必须包含 #include "main.h"，以便一次引入 MCU 与标准库相关头文件；
// 本项目改由下面两个 BSP 头文件提供 SPI/TMR 依赖。
#include "bsp_spi.h"
#include "bsp_tmr.h"

#include "stdlib.h"
#include "stdint.h"
#include "string.h"
#include "math.h"



//#######  SETUP  ##############################################################################################
//>>>>>>>>>  该库仅与帧缓冲区一起使用（需要大量内存） <<<<<<<<<<<<<<

//== 选择显示器: =======================================================
//-- 保留需要的部分，注释掉其他部分（重要的是只能选择一个）---------

#define	GC9D01_IS_40X160		// 0.99" 40 x 160 GC9D01 		

//=============================================================================
		
		
//##############################################################################################################

extern uint16_t GC9D01_Width, GC9D01_Height;

extern uint16_t GC9D01_X_Start;
extern uint16_t GC9D01_Y_Start;

#define RGB565(r, g, b)         (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))

#define PI 	3.14159265
//--- 预定义颜色 ------------------------------
#define   	GC9D01_BLACK   			0x0000
#define   	GC9D01_BLUE    			0x001F
#define   	GC9D01_RED     			0xF800
#define   	GC9D01_GREEN   			0x07E0
#define     GC9D01_CYAN    			0x07FF
#define     GC9D01_MAGENTA 			0xF81F
#define     GC9D01_YELLOW  			0xFFE0
#define     GC9D01_WHITE   			0xFFFF
//------------------------------------------------


#define GC9D01_MADCTL_MY  				0x80
#define GC9D01_MADCTL_MX  				0x40
#define GC9D01_MADCTL_MV  				0x20
#define GC9D01_MADCTL_ML  				0x10
#define GC9D01_MADCTL_RGB 				0x00
#define GC9D01_MADCTL_BGR 				0x08
#define GC9D01_MADCTL_MH  				0x04
//-------------------------------------------------


#define GC9D01_SWRESET 						0x01
#define GC9D01_SLPIN   						0x10
#define GC9D01_SLPOUT  						0x11
#define GC9D01_NORON   						0x13
#define GC9D01_INVOFF  						0x20
#define GC9D01_INVON   						0x21
#define GC9D01_DISPOFF 						0x28
#define GC9D01_DISPON  						0x29
#define GC9D01_CASET   						0x2A
#define GC9D01_RASET   						0x2B
#define GC9D01_RAMWR   						0x2C
#define GC9D01_COLMOD  						0x3A
#define GC9D01_MADCTL  						0x36
#define GC9D01_RAMWR_CONT       	0x3C
//-----------------------------------------------


//==============================================================================
// 传递给 GC9D01_COLMOD 命令的参数值
#define ColorMode_RGB_16bit  			0x50
#define ColorMode_RGB_18bit  			0x60
#define ColorMode_MCU_12bit  			0x03
#define ColorMode_MCU_16bit  			0x05
#define ColorMode_MCU_18bit  			0x06


#define GC9D01_DisplayFunctionControl    	0xB6


//### 0.99 英寸 40 x 160 GC9D01 显示屏参数 ###################################
// 0.99 英寸 40 x 160 GC9D01 显示屏，默认方向
// 上游驱动按 160 x 160（最大尺寸）设计，其他尺寸靠 XSTART/YSTART 抵消像素差
#ifdef GC9D01_IS_40X160
	
	#define GC9D01_WIDTH  			160
	#define GC9D01_HEIGHT 			40
	#define GC9D01_XSTART 			-60
	#define GC9D01_YSTART 			60
	#define GC9D01_ROTATION 		(GC9D01_MADCTL_MV | GC9D01_MADCTL_ML | GC9D01_MADCTL_RGB)
	
#endif
	
	
//##############################################################################

void GC9D01_Init(void);
void GC9D01_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data);	
void GC9D01_HardReset(void);
void GC9D01_SleepModeEnter( void );
void GC9D01_SleepModeExit( void );
void GC9D01_ColorModeSet(uint8_t ColorMode);
void GC9D01_MemAccessModeSet(uint8_t Rotation, uint8_t VertMirror, uint8_t HorizMirror, uint8_t IsBGR);
void GC9D01_InversionMode(uint8_t Mode);
void GC9D01_FillScreen(uint16_t color);
void GC9D01_Clear(void);
void GC9D01_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void GC9D01_SetBL(uint8_t Value);
void GC9D01_DisplayPower(uint8_t On);
void GC9D01_DrawRectangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void GC9D01_DrawRectangleFilled(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t fillcolor);
void GC9D01_DrawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void GC9D01_DrawLineWithAngle(int16_t x, int16_t y, uint16_t length, float angle_degrees, uint16_t color);
void GC9D01_DrawTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t color);
void GC9D01_DrawFilledTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t color);
void GC9D01_DrawPixel(int16_t x, int16_t y, uint16_t color);
void GC9D01_DrawCircleFilled(int16_t x0, int16_t y0, int16_t radius, uint16_t fillcolor);
void GC9D01_DrawCircle(int16_t x0, int16_t y0, int16_t radius, uint16_t color);
void GC9D01_DrawEllipse(int16_t x0, int16_t y0, int16_t radiusX, int16_t radiusY, uint16_t color);
void GC9D01_DrawEllipseFilled(int16_t x0, int16_t y0, int16_t radiusX, int16_t radiusY, uint16_t color);
void GC9D01_DrawEllipseFilledWithAngle(int16_t x0, int16_t y0, int16_t radiusX, int16_t radiusY, float angle_degrees, uint16_t color);
void GC9D01_DrawEllipseWithAngle(int16_t x0, int16_t y0, int16_t radiusX, int16_t radiusY, float angle_degrees, uint16_t color);
void GC9D01_rotation( uint8_t rotation );
void GC9D01_DrawCircleHelper(int16_t x0, int16_t y0, int16_t radius, int8_t quadrantMask, uint16_t color);
void GC9D01_DrawFillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint16_t color);
void GC9D01_DrawFillRoundRect(int16_t x, int16_t y, uint16_t width, uint16_t height, int16_t cornerRadius, uint16_t color);
void GC9D01_DrawRoundRect(int16_t x, int16_t y, uint16_t width, uint16_t height, int16_t cornerRadius, uint16_t color);
void GC9D01_DrawArc(int16_t x0, int16_t y0, int16_t radius, int16_t startAngle, int16_t endAngle, uint16_t color, uint8_t thick);
void GC9D01_DrawLineThick(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color, uint8_t thick);
void GC9D01_DrawLineThickWithAngle(int16_t x, int16_t y, int16_t length, float angle_degrees, uint16_t color, uint8_t thick);

void GC9D01_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void GC9D01_RamWrite(uint8_t *pBuff, uint32_t Len);
void GC9D01_Update(void);
void GC9D01_ClearFrameBuffer(void);



/* C++ detection */
#ifdef __cplusplus
}
#endif

#endif	/*	_GC9D01_H */

/************************ (C) COPYRIGHT GKP *****END OF FILE****/
