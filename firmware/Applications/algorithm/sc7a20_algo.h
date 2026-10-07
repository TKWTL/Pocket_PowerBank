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
    SC7A20_ORIENT_FLAT_FACE_UP,
    SC7A20_ORIENT_FLAT_FACE_DOWN,
    SC7A20_ORIENT_LEFT,
    SC7A20_ORIENT_RIGHT,
    SC7A20_ORIENT_UP,
    SC7A20_ORIENT_DOWN
} sc7a20_orientation_t;

#define SC7A20_ALGO_SAMPLE_MS          40U   /* 25Hz */
#define SC7A20_ALGO_ENTER_MG          650.0f
#define SC7A20_ALGO_FLAT_MG           700.0f
#define SC7A20_ALGO_CONFIRM_SAMPLES     4U
#define SC7A20_ALGO_EMA_ALPHA           0.25f

void SC7A20_AlgoInit(void);
void SC7A20_AlgoUpdate(float x_mg, float y_mg, float z_mg);
sc7a20_orientation_t SC7A20_AlgoGetOrientation(void);
uint8_t SC7A20_AlgoTakeOrientationChanged(void);

#ifdef __cplusplus
}
#endif

#endif /* SC7A20_ALGO_H */
