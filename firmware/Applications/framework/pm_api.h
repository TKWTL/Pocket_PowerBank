#ifndef PM_API_H
#define PM_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_BLOCK_SW6306_LOAD    0x01U
#define PM_BLOCK_WLED           0x02U   /* WLED 开启时阻止系统休眠 */

void pm_api_refresh_idle(void);
void pm_api_force_sleep(void);          /* 手动休眠：立即进入休眠流程 */
void pm_api_set_sleep_timeout(int timeout_sec); /* 0=永久不休眠(No Auto Sleep)，>0=超时秒数 */
void pm_api_set_sleep_block(uint8_t mask, uint8_t enable);
uint8_t pm_api_ui_should_block(void);
uint8_t pm_api_is_sleeping(void);
void pm_api_set_unstable_wake(uint8_t unstable);
uint8_t pm_api_is_unstable_wake(void);

uint8_t pm_api_get_block_mask(void);

/* 数据刷新请求 / 唤醒数据就绪门闩（唤醒预取：先查后显） */
void pm_api_request_data_refresh(void);   /* 外部（EXINT 中断）：请求立即刷新数据镜像（单一 flag） */
uint8_t pm_api_data_refresh_pending(void);/* load_task 轮询：是否有待处理刷新请求 */
void pm_api_data_refresh_done(void);      /* load_task：完成一轮完整读取后调用（清请求 + 置数据就绪） */
void pm_api_mark_wake_data_stale(void);   /* powerdown_task：唤醒后置数据未就绪（阻塞 UI 亮屏） */
uint8_t pm_api_wake_data_ready(void);     /* ui_task：唤醒数据是否已就绪 */

/* 睡眠总线门控：进深睡前置门控，load 类任务停止发起总线读写；
 * powerdown 等待所有总线互斥锁空闲（已发起的读写完成）后再深睡，
 * 保证深睡期间无进行中的总线传输（异步 I2C 中断不被打断，唤醒后状态干净）。 */
void pm_api_sleep_gate_set(uint8_t on);   /* on=1 睡眠门控（load 任务停止新总线读写） */
uint8_t pm_api_sleep_gate_get(void);      /* 查询睡眠门控 */
uint8_t pm_api_bus_locks_idle(void);      /* 所有总线互斥锁（I2C/SPI）空闲？ */

#ifdef __cplusplus
}
#endif

#endif
