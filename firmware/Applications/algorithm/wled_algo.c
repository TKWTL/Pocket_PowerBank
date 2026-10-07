/*
 * wled_algo.c - WLED brightness / thermal / metering policy
 *
 * Layering:
 *   UI -> request only
 *   wled_algo -> state / policy, reads SW6306 mirror only
 *   load_task -> executes PWM writes and SW6306 A1 insert/remove events
 *   drivers -> hardware access only
 */
#include "wled_algo.h"
#include "wled.h"
#include "sw6306.h"
#include "bsp_usart.h"
#include "FreeRTOS.h"
#include "task.h"

/* Temperature derating table (2C steps):
 *   <=0C:25, 30C:19, 40C:16, 60C:0.
 * 60C itself is a hard latched shutdown; the table only shapes the derating. */
static const uint8_t s_limit_table[] = {
     25, 25, 24, 24, 23, 23, 22, 22, 21, 21,
     21, 20, 20, 19, 19, 19, 18, 17, 17, 16,
     16, 14, 13, 11, 10,  8,  7,  5,  4,  2,
      0
};
#define WLED_LIMIT_TABLE_LEN (sizeof(s_limit_table) / sizeof(s_limit_table[0]))
typedef char wled_limit_table_len_check[
    (WLED_LIMIT_TABLE_LEN ==
     (((WLED_NTC_OFF_C - WLED_LIMIT_T_MIN_C) / WLED_LIMIT_STEP_C) + 1)) ? 1 : -1];

static uint8_t s_level;
static uint8_t s_out;
static uint8_t s_limit;
static uint8_t s_protected;
static uint8_t s_cut;

static uint16_t s_pwm;
static uint16_t s_pwm_from;
static uint16_t s_pwm_to;
static uint16_t s_pwm_tick;
static uint8_t s_pwm_dirty;
static uint8_t s_path_ok;

/* Fake A1 ownership is explicit: only a port event created by this policy may
 * later be removed by this policy. */
static uint8_t s_fake_a1_owned;
static uint8_t s_insert_pending;
static uint8_t s_remove_pending;
static uint16_t s_port_wait_ticks;
static uint16_t s_detach_ticks;
static uint16_t s_post_off_ticks;
static wled_port_cmd_t s_port_cmd;

/* Cross-task UI requests. */
static volatile uint8_t s_toggle_request;
static volatile int16_t s_step_request;

static uint16_t wled_level_to_pwm(uint16_t level)
{
    uint32_t pwm = (uint32_t)level * (uint32_t)level;
    return (pwm > WLED_PWM_MAX) ? WLED_PWM_MAX : (uint16_t)pwm;
}

static uint8_t wled_sw_mirror_valid(void)
{
    if (SW6306_IsInitialized() == 0U) return 0U;
    if (SW6306_ReadVBAT() < 1000U) return 0U;
    return 1U;
}

static uint8_t wled_temp_limit(void)
{
    int32_t idx;

    if (wled_sw_mirror_valid() == 0U) return WLED_LIMIT_SENSOR_FAULT_LV;

    idx = ((int32_t)SW6306_ReadTNTC() - (int32_t)WLED_LIMIT_T_MIN_C) /
          (int32_t)WLED_LIMIT_STEP_C;
    if (idx < 0) idx = 0;
    if (idx > (int32_t)WLED_LIMIT_TABLE_LEN - 1) {
        idx = (int32_t)WLED_LIMIT_TABLE_LEN - 1;
    }
    return s_limit_table[idx];
}

static uint8_t wled_cutoff_now(void)
{
    int16_t tntc;

    if (wled_sw_mirror_valid() == 0U) return 0U;

    tntc = SW6306_ReadTNTC();
    if (tntc >= WLED_NTC_OFF_C) {
        s_cut = 1U;
        return 1U;
    }

    if (s_cut != 0U) {
        if (tntc <= WLED_NTC_RECOVER_C) s_cut = 0U;
        return (s_cut != 0U) ? 1U : 0U;
    }
    return 0U;
}

static uint8_t wled_zero_capacity(void)
{
    if (SW6306_ReadVBAT() < 1000U) return 0U;
    return (SW6306_ReadCapacity() <= 0) ? 1U : 0U;
}

static void wled_set_pwm_now(uint16_t pwm)
{
    s_pwm = pwm;
    s_pwm_from = pwm;
    s_pwm_to = pwm;
    s_pwm_tick = WLED_RAMP_TICKS;
    s_pwm_dirty = 1U;
}

static void wled_protect_off(void)
{
    s_protected = 1U;
    s_level = 0U;
    s_out = 0U;
    wled_set_pwm_now(0U);
}

static uint8_t wled_update_output(void)
{
    uint8_t out;

    if (wled_cutoff_now() != 0U) {
        if (s_protected == 0U || s_out != 0U) {
            wled_protect_off();
#if WLED_DEBUG_PRINT
            USART_Printf("[WLED] OFF: over temp, Tntc:%dC\r\n",
                         (int)SW6306_ReadTNTC());
#endif
            return 1U;
        }
        return 0U;
    }

    s_limit = wled_temp_limit();
    out = (s_level == 0U) ? 0U : ((s_level < s_limit) ? s_level : s_limit);
    if (out != s_out) {
        s_out = out;
        return 1U;
    }
    return 0U;
}

static void wled_set_brightness(uint16_t level)
{
    if (level > WLED_BRIGHTNESS_MAX) level = WLED_BRIGHTNESS_MAX;
    if (level != 0U && level < WLED_BRIGHTNESS_MIN) level = WLED_BRIGHTNESS_MIN;

    if (level == 0U) {
        s_level = 0U;
        s_out = 0U;
        s_protected = 0U;
        s_cut = 0U;
        wled_set_pwm_now(0U);
        return;
    }

    if (wled_cutoff_now() != 0U || wled_zero_capacity() != 0U) {
        wled_protect_off();
        return;
    }

    s_level = (uint8_t)level;
    s_protected = 0U;
    (void)wled_update_output();
}

static void wled_take_ui_requests(void)
{
    uint8_t toggle;
    int16_t step;
    int32_t level;

    taskENTER_CRITICAL();
    toggle = s_toggle_request;
    s_toggle_request = 0U;
    step = s_step_request;
    s_step_request = 0;
    taskEXIT_CRITICAL();

    if (toggle != 0U) {
        if (s_level != 0U) {
            wled_set_brightness(0U);
        } else {
            wled_set_brightness(WLED_BRIGHTNESS_MAX / 2U);
        }
    }

    if (step != 0) {
        level = (int32_t)s_level + (int32_t)step;
        if (level > (int32_t)WLED_BRIGHTNESS_MAX) level = WLED_BRIGHTNESS_MAX;
        if (level < (int32_t)WLED_BRIGHTNESS_MIN) level = WLED_BRIGHTNESS_MIN;
        wled_set_brightness((uint16_t)level);
    }
}

static void wled_pwm_ramp(void)
{
    uint16_t want;

    if (s_pwm_tick >= WLED_RAMP_TICKS) return;

    s_pwm_tick++;
    if (s_pwm_tick >= WLED_RAMP_TICKS) {
        want = s_pwm_to;
    } else if (s_pwm_to >= s_pwm_from) {
        want = (uint16_t)((uint32_t)s_pwm_from +
               ((uint32_t)(s_pwm_to - s_pwm_from) * s_pwm_tick) / WLED_RAMP_TICKS);
    } else {
        want = (uint16_t)((uint32_t)s_pwm_from -
               ((uint32_t)(s_pwm_from - s_pwm_to) * s_pwm_tick) / WLED_RAMP_TICKS);
    }

    if (want != s_pwm) {
        s_pwm = want;
        s_pwm_dirty = 1U;
#if WLED_DEBUG_PRINT
        USART_Printf("[WLED] PWM:%u\r\n", (unsigned)s_pwm);
#endif
    }
}

static void wled_request_port(wled_port_cmd_t cmd)
{
    if (s_port_cmd == WLED_PORT_CMD_NONE) s_port_cmd = cmd;
}

static void wled_update_port_confirmation(uint8_t a1_on)
{
    if (s_insert_pending != 0U) {
        if (a1_on != 0U) {
            s_fake_a1_owned = 1U;
            s_insert_pending = 0U;
            s_port_wait_ticks = 0U;
        } else if (++s_port_wait_ticks >= WLED_FAKE_A1_RETRY_TICKS) {
            s_insert_pending = 0U;
            s_port_wait_ticks = 0U;
        }
    } else if (s_remove_pending != 0U) {
        if (a1_on == 0U) {
            s_fake_a1_owned = 0U;
            s_remove_pending = 0U;
            s_port_wait_ticks = 0U;
        } else if (++s_port_wait_ticks >= WLED_FAKE_A1_RETRY_TICKS) {
            s_remove_pending = 0U;
            s_port_wait_ticks = 0U;
        }
    }
}

static void wled_meter_path_tick(void)
{
    uint8_t c1_on = (SW6306_IsPortC1ON() != 0U) ? 1U : 0U;
    uint8_t a1_on = (SW6306_IsPortA1ON() != 0U) ? 1U : 0U;
    uint8_t charging = (SW6306_IsCharging() != 0U) ? 1U : 0U;
    uint8_t on = (s_level != 0U) ? 1U : 0U;
    uint8_t real_path = (c1_on != 0U || charging != 0U) ? 1U : 0U;

    wled_update_port_confirmation(a1_on);

    if (on != 0U) {
        s_post_off_ticks = 0U;

        /* Any already-active SW6306 path is enough for metering. Fake A1 is only
         * needed when WLED is the sole load. */
        if (real_path != 0U || a1_on != 0U) {
            s_path_ok = 1U;
        } else {
            s_path_ok = 0U;
            if (s_insert_pending == 0U && s_remove_pending == 0U) {
                wled_request_port(WLED_PORT_CMD_A1_INSERT);
                s_insert_pending = 1U;
                s_port_wait_ticks = 0U;
            }
        }

        /* If a real path takes over, release only the fake A1 that we own. */
        if (s_fake_a1_owned != 0U && real_path != 0U && s_remove_pending == 0U) {
            if (++s_detach_ticks >= WLED_FAKE_A1_DETACH_TICKS) {
                wled_request_port(WLED_PORT_CMD_A1_REMOVE);
                s_remove_pending = 1U;
                s_port_wait_ticks = 0U;
                s_detach_ticks = 0U;
            }
        } else {
            s_detach_ticks = 0U;
        }
        return;
    }

    s_path_ok = 1U;
    s_detach_ticks = 0U;

    /* Off is immediate PWM=0. Keep the fake path for another 200ms so its
     * current has settled before removing it. */
    if (s_fake_a1_owned != 0U && s_remove_pending == 0U) {
        if (s_pwm == 0U) {
            if (++s_post_off_ticks >= WLED_FAKE_A1_POST_OFF_TICKS) {
                wled_request_port(WLED_PORT_CMD_A1_REMOVE);
                s_remove_pending = 1U;
                s_port_wait_ticks = 0U;
                s_post_off_ticks = 0U;
            }
        } else {
            s_post_off_ticks = 0U;
        }
    }
}

void WLED_AlgoInit(void)
{
    s_level = 0U;
    s_out = 0U;
    s_limit = WLED_BRIGHTNESS_MAX;
    s_protected = 0U;
    s_cut = 0U;

    s_pwm = 0U;
    s_pwm_from = 0U;
    s_pwm_to = 0U;
    s_pwm_tick = WLED_RAMP_TICKS;
    s_pwm_dirty = 1U;
    s_path_ok = 1U;

    s_fake_a1_owned = 0U;
    s_insert_pending = 0U;
    s_remove_pending = 0U;
    s_port_wait_ticks = 0U;
    s_detach_ticks = 0U;
    s_post_off_ticks = 0U;
    s_port_cmd = WLED_PORT_CMD_NONE;

    s_toggle_request = 0U;
    s_step_request = 0;
}

void WLED_AlgoRequestToggle(void)
{
    taskENTER_CRITICAL();
    s_toggle_request ^= 1U;
    taskEXIT_CRITICAL();
}

void WLED_AlgoRequestBrightnessStep(int8_t step)
{
    taskENTER_CRITICAL();
    s_step_request += step;
    taskEXIT_CRITICAL();
}

void WLED_AlgoTick10ms(void)
{
    uint8_t changed;
    uint16_t target;

    wled_take_ui_requests();
    wled_meter_path_tick();

    changed = wled_update_output();
#if WLED_DEBUG_PRINT
    if (changed != 0U) {
        USART_Printf("[WLED] limit L%u->L%u Tntc:%dC\r\n",
                     (unsigned)s_level, (unsigned)s_out,
                     (int)SW6306_ReadTNTC());
    }
#else
    (void)changed;
#endif

    target = wled_level_to_pwm(s_out);
    if (target != s_pwm_to) {
        s_pwm_from = s_pwm;
        s_pwm_to = target;
        s_pwm_tick = 0U;
    }

    if (s_path_ok == 0U && target != 0U) return;
    wled_pwm_ramp();
}

void WLED_AlgoUpdate500ms(void)
{
    if (s_level != 0U && wled_zero_capacity() != 0U) {
        wled_protect_off();
#if WLED_DEBUG_PRINT
        USART_Printf("[WLED] OFF: zero capacity\r\n");
#endif
    }
}

uint8_t WLED_AlgoTakePwm(uint16_t *pwm)
{
    if (s_pwm_dirty == 0U || pwm == 0) return 0U;
    *pwm = s_pwm;
    s_pwm_dirty = 0U;
    return 1U;
}

wled_port_cmd_t WLED_AlgoTakePortCommand(void)
{
    wled_port_cmd_t cmd = s_port_cmd;
    s_port_cmd = WLED_PORT_CMD_NONE;
    return cmd;
}

uint16_t WLED_AlgoGetBrightness(void)
{
    return s_level;
}

uint16_t WLED_AlgoGetOutputLevel(void)
{
    return s_out;
}

uint16_t WLED_AlgoGetPwm(void)
{
    return s_pwm;
}

uint8_t WLED_AlgoIsOn(void)
{
    return (s_level != 0U) ? 1U : 0U;
}

uint8_t WLED_AlgoIsProtectedOff(void)
{
    return s_protected;
}

uint8_t WLED_AlgoOwnsFakeA1(void)
{
    return s_fake_a1_owned;
}
