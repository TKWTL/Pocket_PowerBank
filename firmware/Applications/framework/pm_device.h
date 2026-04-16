#ifndef PM_DEVICE_H
#define PM_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_hook_t)(void *ctx);

typedef struct {
    const char *name;
    pm_hook_t on_prepare;
    pm_hook_t on_suspend;
    pm_hook_t on_resume;
    void *ctx;
    uint8_t order;
} pm_device_node_t;

int pm_device_register(const pm_device_node_t *node);
void pm_device_prepare_all(void);
void pm_device_suspend_all(void);
void pm_device_resume_all(void);
void pm_device_register_builtin(void);
void pm_device_register_linker_set(void);
void pm_device_register_all(void);

#ifdef __cplusplus
}
#endif

#endif
