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
#include "framework/nvm_store.h"       /* SD3078 Backup RAM NVM */
#include "algorithm/sd3078_algo.h"      /* RTC提交 + MS621FE 后备电池策略 */
#include "algorithm/sw6306_algo.h"      /* SW6306 配置命令 + SOH/EFC */
#include "algorithm/sc7a20_algo.h"      /* 重力方向/旋转判定 */

/* ===== 外设驱动头文件 =====
 * Driver 只负责器件寄存器/镜像；algorithm 层负责业务状态机。
 * load_task：SD3078服务 + SC7A20 5Hz采样 + NVM提交；
 * SW6306_task：SW6306周期采样 + SW6306算法。
 * UI 只读镜像/算法状态并发送RAM request，不直接访问I2C。 */
#include "drivers.h"

#ifdef __cplusplus
}
#endif

#endif