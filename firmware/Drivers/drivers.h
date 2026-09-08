/*
 * drivers.h - 统一驱动包含头（Driver 层）
 *
 * 集中包含所有外部功能芯片/外设驱动，供应用层一条 include 使用。
 *
 * 增删外部功能芯片时的移植步骤：
 *  1. 在此处加/删一行 #include；
 *  2. 在 Keil 工程（Pocket_PowerBank.uvprojx）加/删：
 *     - IncludePath 中对应的 Drivers\<芯片> 路径；
 *     - 对应 Group 中的驱动源文件。
 * 应用层（ui_task 等）经 applications.h → drivers.h 即可使用全部驱动。
 */
#ifndef __DRIVERS_H__
#define __DRIVERS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 按键输入（3 键：MENU/CONF/NEXT） */
#include "buttons.h"

/* 显示屏 GC9D01（160x40 RGB565） */
#include "GC9D01.h"

/* 电源管理芯片 SW6306（充放电/I2C） */
#include "sw6306.h"

/* 加速度计 SC7A20（I2C） */
#include "sc7a20.h"

/* RTC 时钟芯片 SD3078（I2C） */
#include "sd3078.h"

/* WLED 灯（温度/电压保护逻辑） */
#include "wled.h"

#ifdef __cplusplus
}
#endif

#endif /* __DRIVERS_H__ */
