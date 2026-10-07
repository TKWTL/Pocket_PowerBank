#include "pm_device.h"

#include "drivers.h"   /* 统一驱动包含头：GC9D01 / SW6306 / SC7A20 */

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

/* SC7A20：休眠时 ODR 置零（Power-down，约 0.5uA），唤醒后恢复。
 * 用 LowPowerSet() 而不是 SetODR(POWERDOWN)：它除了把 CTRL_REG1 写 0x00，
 * 还会把 INT 极性改成低有效，避免 INT1 在无事件时主动拉低、掩盖共用同一根
 * EXINT8 的 SW6306 唤醒脉冲（见 sc7a20.c 该函数注释）。
 *
 * 顺序（order 15）夹在 gc9d01(10) 与 sw6306(20) 之间：
 *  - suspend 按 order 升序：先灭屏，再停加速度计，最后 SW6306；
 *  - resume 按 order 降序：先 SW6306，再恢复加速度计，最后亮屏。
 * pm_device_suspend_all() 在 SLEEP_PREPARE→DEEPSLEEP 转换时调用，早于
 * pm_enter_deep_sleep() 置睡眠门控，所以这笔 I2C 一定能发出去；
 * resume 在回到 RUN 那一帧执行，此时门控已解除。 */
static void pm_sc7a20_suspend(void *ctx)
{
    (void)ctx;
    (void)SC7A20_LowPowerSet();
}

static void pm_sc7a20_resume(void *ctx)
{
    (void)ctx;
    /* 必须用 Init() 而不是 SetODR()：Power-down 写的是 CTRL_REG1=0x00，
     * 轴使能位（XEN/YEN/ZEN）也一起被清零，而 SetODR 只改 ODR 位 ——
     * 只恢复 ODR 会留下"三轴关闭、读数恒为 0"的静默故障。
     * Init() 一次写回 ODR + 三轴使能 + 量程/BDU/HR，并重建振动窗口。
     * 姿态算法状态不动：即使镜像里还留着休眠前的旧样本，也要连续 10 次
     * （约 2s）反向读数才会翻转，单次旧样本不会误触发。 */
    (void)SC7A20_Init();
}

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
    pm_sc7a20_suspend,
    pm_sc7a20_resume,
    0,
    15
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
    pm_device_register(&s_pm_sc7a20);
    pm_device_register(&s_pm_sw6306);
}
