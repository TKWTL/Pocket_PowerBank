/*
 * sc7a20_algo.c - low-rate up/down orientation estimator
 */
#include "sc7a20_algo.h"

static volatile sc7a20_orientation_t s_orientation;
static sc7a20_orientation_t s_candidate;
static uint8_t s_candidate_count;

void SC7A20_AlgoInit(void)
{
    s_orientation = SC7A20_ORIENT_UNKNOWN;
    s_candidate = SC7A20_ORIENT_UNKNOWN;
    s_candidate_count = 0U;
}

void SC7A20_AlgoUpdate(float y_mg)
{
    sc7a20_orientation_t next;

    if (y_mg >= SC7A20_ALGO_ENTER_MG) {
        next = SC7A20_ORIENT_UP;
    } else if (y_mg <= -SC7A20_ALGO_ENTER_MG) {
        next = SC7A20_ORIENT_DOWN;
    } else {
        s_candidate = SC7A20_ORIENT_UNKNOWN;
        s_candidate_count = 0U;
        return;
    }

    if (next == s_orientation) {
        s_candidate = SC7A20_ORIENT_UNKNOWN;
        s_candidate_count = 0U;
        return;
    }

    if (next != s_candidate) {
        s_candidate = next;
        s_candidate_count = 0U;
        return;
    }

    if (++s_candidate_count >= SC7A20_ALGO_CONFIRM_SAMPLES) {
        s_orientation = next;
        s_candidate = SC7A20_ORIENT_UNKNOWN;
        s_candidate_count = 0U;
    }
}

sc7a20_orientation_t SC7A20_AlgoGetOrientation(void)
{
    return s_orientation;
}
