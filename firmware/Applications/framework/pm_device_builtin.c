#include "pm_device.h"

#include "drivers.h"   /* GC9D01 */
#include "algorithm/sw6306_algo.h"
#include "algorithm/sc7a20_algo.h"

static void pm_gc9d01_prepare(void *ctx)
{
    (void)ctx;
    GC9D01_SetBL(0);
}

static void pm_gc9d01_suspend(void *ctx)
{
    (void)ctx;
    GC9D01_DisplayPower(0);
    GC9D01_SleepModeEnter();
}

static void pm_gc9d01_resume(void *ctx)
{
    (void)ctx;
    GC9D01_SleepModeExit();
    GC9D01_DisplayPower(1);
    /* 保持灭：亮屏由 ui_task 在唤醒数据预取完成、绘出新图后按菜单背光值恢复
     * （避免休眠前旧值闪现）。 */
    GC9D01_SetBL(0);
}

/* SW6306 / SC7A20 的具体低功耗策略已移入各自 algorithm。
 * framework这里只保留设备注册顺序，避免跨层持有外设业务判断。 */

static const pm_device_node_t s_pm_gc9d01 = {
    "gc9d01",
    pm_gc9d01_prepare,
    pm_gc9d01_suspend,
    pm_gc9d01_resume,
    0,
    10
};

static const pm_device_node_t s_pm_sc7a20 = {
    "sc7a20",
    0,
    SC7A20_AlgoPmSuspend,
    SC7A20_AlgoPmResume,
    0,
    15
};

static const pm_device_node_t s_pm_sw6306 = {
    "sw6306",
    0,
    SW6306_AlgoPmSuspend,
    SW6306_AlgoPmResume,
    0,
    20
};

void pm_device_register_builtin(void)
{
    pm_device_register(&s_pm_gc9d01);
    pm_device_register(&s_pm_sc7a20);
    pm_device_register(&s_pm_sw6306);
}
