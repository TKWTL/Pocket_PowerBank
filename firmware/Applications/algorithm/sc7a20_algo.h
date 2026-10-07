/*
 * sc7a20_algo.h - gravity/orientation algorithm for SC7A20 mirror data
 *
 * No I2C and no task creation. load_task feeds samples; UI may read the result.
 */
#ifndef SC7A20_ALGO_H
#define SC7A20_ALGO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SC7A20_ORIENT_UNKNOWN = 0,
    SC7A20_ORIENT_NORMAL,     /* X ≈ -1g：当前显示方向 */
    SC7A20_ORIENT_FLIPPED     /* X ≈ +1g：翻转180° */
} sc7a20_orientation_t;

#define SC7A20_ALGO_SAMPLE_MS         200U   /* 5Hz application sampling */
#define SC7A20_ALGO_ENTER_MG        650.0f
#define SC7A20_ALGO_CONFIRM_SAMPLES   10U    /* 10 further 200ms samples = 2s stable */

void SC7A20_AlgoInit(void);
void SC7A20_AlgoUpdate(float x_mg);
sc7a20_orientation_t SC7A20_AlgoGetOrientation(void);

#ifdef __cplusplus
}
#endif

#endif /* SC7A20_ALGO_H */
