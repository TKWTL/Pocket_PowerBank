#include "pm_device.h"

#define PM_DEVICE_MAX 16

static const pm_device_node_t *s_nodes[PM_DEVICE_MAX];
static uint8_t s_count;

static void pm_call_prepare(const pm_device_node_t *node)
{
    if (node->on_prepare != 0) {
        node->on_prepare(node->ctx);
    }
}

static void pm_call_suspend(const pm_device_node_t *node)
{
    if (node->on_suspend != 0) {
        node->on_suspend(node->ctx);
    }
}

static void pm_call_resume(const pm_device_node_t *node)
{
    if (node->on_resume != 0) {
        node->on_resume(node->ctx);
    }
}

int pm_device_register(const pm_device_node_t *node)
{
    uint8_t i;

    if (node == 0 || s_count >= PM_DEVICE_MAX) {
        return -1;
    }

    i = s_count;
    while (i > 0 && s_nodes[i - 1]->order > node->order) {
        s_nodes[i] = s_nodes[i - 1];
        i--;
    }
    s_nodes[i] = node;
    s_count++;
    return 0;
}

void pm_device_prepare_all(void)
{
    uint8_t i;

    for (i = 0; i < s_count; i++) {
        pm_call_prepare(s_nodes[i]);
    }
}

void pm_device_suspend_all(void)
{
    uint8_t i;

    for (i = 0; i < s_count; i++) {
        pm_call_suspend(s_nodes[i]);
    }
}

void pm_device_resume_all(void)
{
    int i;

    for (i = (int)s_count - 1; i >= 0; i--) {
        pm_call_resume(s_nodes[i]);
    }
}

__attribute__((weak)) void pm_device_register_linker_set(void)
{
}

void pm_device_register_all(void)
{
    pm_device_register_builtin();
    pm_device_register_linker_set();
}
