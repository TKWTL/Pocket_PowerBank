/*
 * sc7a20_algo.c - low-rate gravity orientation estimator
 */
#include "sc7a20_algo.h"
#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    float x_f;
    float y_f;
    float z_f;
    uint8_t filter_valid;
    sc7a20_orientation_t orientation;
    sc7a20_orientation_t candidate;
    uint8_t candidate_count;
    volatile uint8_t changed;
} sc7a20_algo_state_t;

static sc7a20_algo_state_t s_algo;

static float absf_local(float v)
{
    return (v < 0.0f) ? -v : v;
}

static sc7a20_orientation_t sc7a20_classify(float x, float y, float z)
{
    float ax = absf_local(x);
    float ay = absf_local(y);
    float az = absf_local(z);

    if (az >= SC7A20_ALGO_FLAT_MG && az >= ax && az >= ay) {
        return (z >= 0.0f) ? SC7A20_ORIENT_FLAT_FACE_UP
                           : SC7A20_ORIENT_FLAT_FACE_DOWN;
    }

    if (ax >= SC7A20_ALGO_ENTER_MG && ax >= ay) {
        return (x >= 0.0f) ? SC7A20_ORIENT_RIGHT : SC7A20_ORIENT_LEFT;
    }

    if (ay >= SC7A20_ALGO_ENTER_MG) {
        return (y >= 0.0f) ? SC7A20_ORIENT_UP : SC7A20_ORIENT_DOWN;
    }

    return SC7A20_ORIENT_UNKNOWN;
}

void SC7A20_AlgoInit(void)
{
    s_algo.x_f = 0.0f;
    s_algo.y_f = 0.0f;
    s_algo.z_f = 0.0f;
    s_algo.filter_valid = 0U;
    s_algo.orientation = SC7A20_ORIENT_UNKNOWN;
    s_algo.candidate = SC7A20_ORIENT_UNKNOWN;
    s_algo.candidate_count = 0U;
    s_algo.changed = 0U;
}

void SC7A20_AlgoUpdate(float x_mg, float y_mg, float z_mg)
{
    sc7a20_orientation_t next;

    if (!s_algo.filter_valid) {
        s_algo.x_f = x_mg;
        s_algo.y_f = y_mg;
        s_algo.z_f = z_mg;
        s_algo.filter_valid = 1U;
    } else {
        s_algo.x_f += SC7A20_ALGO_EMA_ALPHA * (x_mg - s_algo.x_f);
        s_algo.y_f += SC7A20_ALGO_EMA_ALPHA * (y_mg - s_algo.y_f);
        s_algo.z_f += SC7A20_ALGO_EMA_ALPHA * (z_mg - s_algo.z_f);
    }

    next = sc7a20_classify(s_algo.x_f, s_algo.y_f, s_algo.z_f);

    /* UNKNOWN is a dead-band, not an orientation. Keep the current orientation
     * through transient angles instead of flapping back to UNKNOWN. */
    if (next == SC7A20_ORIENT_UNKNOWN) {
        s_algo.candidate = SC7A20_ORIENT_UNKNOWN;
        s_algo.candidate_count = 0U;
        return;
    }

    if (next == s_algo.orientation) {
        s_algo.candidate = next;
        s_algo.candidate_count = 0U;
        return;
    }

    if (next != s_algo.candidate) {
        s_algo.candidate = next;
        s_algo.candidate_count = 1U;
        return;
    }

    if (s_algo.candidate_count < SC7A20_ALGO_CONFIRM_SAMPLES) {
        s_algo.candidate_count++;
    }

    if (s_algo.candidate_count >= SC7A20_ALGO_CONFIRM_SAMPLES) {
        s_algo.orientation = next;
        s_algo.candidate_count = 0U;
        s_algo.changed = 1U;
    }
}

sc7a20_orientation_t SC7A20_AlgoGetOrientation(void)
{
    return s_algo.orientation;
}

uint8_t SC7A20_AlgoTakeOrientationChanged(void)
{
    uint8_t changed;
    taskENTER_CRITICAL();
    changed = s_algo.changed;
    s_algo.changed = 0U;
    taskEXIT_CRITICAL();
    return changed;
}
