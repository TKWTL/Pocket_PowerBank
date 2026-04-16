#include "pm_device.h"

#include "GC9D01.h"
#include "sw6306.h"

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
    GC9D01_SetBL(64);
}

static void pm_sw6306_suspend(void *ctx)
{
    (void)ctx;
    SW6306_ForceOff();
}

static void pm_sw6306_resume(void *ctx)
{
    (void)ctx;
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
