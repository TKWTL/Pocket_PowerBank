#ifndef __APPLICATIONS_H__
#define __APPLICATIONS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 系统级头文件（所有任务文件共用，在此集中包含，便于维护） ===== */
/* 各任务 .c 文件仅需 #include "applications.h" 即可使用以下模块 */
#include "bsp_usart.h"                  /* USART 打印输出（printf 调试用） */
#include "framework/pm_api.h"           /* PM 框架 API（休眠控制接口） */
#include "framework/pm_sleep_timer.h"   /* 休眠定时器（获取剩余时间等） */

/* ===== 外设驱动头文件 =====
 * 统一包含头 drivers.h：增删外部功能芯片时只改 Drivers/drivers.h 即可移植。
 * 传感器/时间镜像集成在各驱动句柄内（SD3078_Status / SC7A20_Status）：
 * load_task 0.5s 刷新句柄镜像（TimeLoad/TempLoad/BattLoad + AccelLoad），
 * UI 只读驱动 Read* 系列（读句柄镜像，不访问 I2C），避免并发。 */
#include "drivers.h"

#ifdef __cplusplus
}
#endif

#endif