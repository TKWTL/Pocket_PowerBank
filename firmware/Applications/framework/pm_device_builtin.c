#include "pm_device.h"

#include "drivers.h"   /* 统一驱动包含头：GC9D01 / SW6306 */

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

static void pm_sw6306_suspend(void *ctx)
{
    (void)ctx;
    /* 进入深睡：让 SW6306 进低功耗，大幅降低整机休眠电流。
     * 低功耗下按键或 VBUS 插入会唤醒 SW6306，并经 IRQ(EXINT8) 唤醒 MCU。 */
    SW6306_LPSet();
}

static void pm_sw6306_resume(void *ctx)
{
    (void)ctx;
    /* 唤醒：解除 SW6306 低功耗并重新解锁寄存器（LPSet 后需先 Unlock 才能写） */
    SW6306_Unlock();
    if (SW6306_IsInitialized() == 0) {
        SW6306_Init();
    }
}

static const pm_device_node_t s_pm_gc9d01 = {
    "gc9d01",
    pm_gc9d01_prepare,
    pm_gc9d01_suspend,
    pm_gc9d01_resume,
    0,
    10
};

static const pm_device_node_t s_pm_sw6306 = {
    "sw6306",
    0,
    pm_sw6306_suspend,
    pm_sw6306_resume,
    0,
    20
};

void pm_device_register_builtin(void)
{
    pm_device_register(&s_pm_gc9d01);
    pm_device_register(&s_pm_sw6306);
}
