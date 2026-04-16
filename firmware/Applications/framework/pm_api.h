#ifndef PM_API_H
#define PM_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_BLOCK_SW6306_LOAD    0x01U

void pm_api_refresh_idle(void);
void pm_api_set_sleep_block(uint8_t mask, uint8_t enable);

#ifdef __cplusplus
}
#endif

#endif
