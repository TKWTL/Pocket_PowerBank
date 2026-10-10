/*
 * gravity_calibrate.c - SC7A20 six-face gravity calibration tool
 *
 * UI never performs I2C. load_task keeps the SC7A20 mirror fresh at 5Hz;
 * this app only consumes right-aligned raw codes, detects a stable face,
 * derives per-axis zero/slope, writes the RAM calibration and requests one
 * transactional NVM update.
 */
#include "applications.h"
#include "lvgl.h"
#include "menu.h"
#include "functions.h"

#define CAL_PERIOD_MS          200U
#define CAL_HOLD_INTERVALS      10U   /* first sample + 10 intervals = about 2s */
#define CAL_AXIS_MIN_ABS       700
#define CAL_OTHER_MAX_ABS      200
#define CAL_STABLE_RANGE       100
#define CAL_MIN_SPAN          1000

typedef enum {
    CAL_X_NEG = 0,  /* screen top up: X ~= -1g */
    CAL_X_POS,      /* screen bottom up: X ~= +1g */
    CAL_Y_NEG,      /* right edge up: Y ~= -1g */
    CAL_Y_POS,      /* left edge up: Y ~= +1g */
    CAL_Z_NEG,      /* screen face up: Z ~= -1g */
    CAL_Z_POS,      /* screen face down: Z ~= +1g */
    CAL_FACE_COUNT,
    CAL_FACE_NONE = 0xFF
} cal_face_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_up;
static lv_obj_t *s_down;
static lv_obj_t *s_left;
static lv_obj_t *s_right;
static lv_obj_t *s_face;
static lv_timer_t *s_timer;

static int16_t s_face_raw[CAL_FACE_COUNT][3];
static uint8_t s_done[CAL_FACE_COUNT];
static cal_face_t s_candidate;
static uint8_t s_hold_intervals;
static uint8_t s_sample_count;
static int16_t s_min[3], s_max[3];
static int32_t s_sum[3];
static uint8_t s_saving;
static uint8_t s_error;
static uint8_t s_exit_queued;

static int16_t cal_abs16(int16_t v)
{
    return (v < 0) ? (int16_t)-v : v;
}

static cal_face_t cal_detect_face(int16_t x, int16_t y, int16_t z)
{
    int16_t ax = cal_abs16(x);
    int16_t ay = cal_abs16(y);
    int16_t az = cal_abs16(z);

    if (ax > CAL_AXIS_MIN_ABS && ay < CAL_OTHER_MAX_ABS && az < CAL_OTHER_MAX_ABS) {
        return (x < 0) ? CAL_X_NEG : CAL_X_POS;
    }
    if (ay > CAL_AXIS_MIN_ABS && ax < CAL_OTHER_MAX_ABS && az < CAL_OTHER_MAX_ABS) {
        return (y < 0) ? CAL_Y_NEG : CAL_Y_POS;
    }
    if (az > CAL_AXIS_MIN_ABS && ax < CAL_OTHER_MAX_ABS && ay < CAL_OTHER_MAX_ABS) {
        return (z < 0) ? CAL_Z_NEG : CAL_Z_POS;
    }
    return CAL_FACE_NONE;
}

static void cal_reset_candidate(void)
{
    s_candidate = CAL_FACE_NONE;
    s_hold_intervals = 0U;
    s_sample_count = 0U;
}

static void cal_start_candidate(cal_face_t face, int16_t x, int16_t y, int16_t z)
{
    int16_t raw[3] = { x, y, z };
    uint8_t i;

    s_candidate = face;
    s_hold_intervals = 0U;
    s_sample_count = 1U;
    for (i = 0U; i < 3U; i++) {
        s_min[i] = raw[i];
        s_max[i] = raw[i];
        s_sum[i] = raw[i];
    }
}

static void cal_update_labels(void)
{
    lv_label_set_text_static(s_up,    s_done[CAL_X_NEG] ? "OK" : "UP");
    lv_label_set_text_static(s_down,  s_done[CAL_X_POS] ? "OK" : "DOWN");
    lv_label_set_text_static(s_left,  s_done[CAL_Y_POS] ? "OK" : "LEFT");
    lv_label_set_text_static(s_right, s_done[CAL_Y_NEG] ? "OK" : "RIGHT");

    if (s_error != 0U) {
        lv_label_set_text_static(s_face, "CAL ERROR");
    } else if (s_saving != 0U) {
        lv_label_set_text_static(s_face, "SAVING");
    } else if (s_done[CAL_Z_NEG] && s_done[CAL_Z_POS]) {
        lv_label_set_text_static(s_face, "FACE UOK DOK");
    } else if (s_done[CAL_Z_NEG]) {
        lv_label_set_text_static(s_face, "FACE UOK D--");
    } else if (s_done[CAL_Z_POS]) {
        lv_label_set_text_static(s_face, "FACE U-- DOK");
    } else {
        lv_label_set_text_static(s_face, "FACE U-- D--");
    }
    lv_obj_invalidate(s_scr);
}

static uint8_t cal_all_done(void)
{
    uint8_t i;
    for (i = 0U; i < CAL_FACE_COUNT; i++) {
        if (s_done[i] == 0U) return 0U;
    }
    return 1U;
}

static void cal_finish(void)
{
    int32_t dx = (int32_t)s_face_raw[CAL_X_POS][0] - s_face_raw[CAL_X_NEG][0];
    int32_t dy = (int32_t)s_face_raw[CAL_Y_POS][1] - s_face_raw[CAL_Y_NEG][1];
    int32_t dz = (int32_t)s_face_raw[CAL_Z_POS][2] - s_face_raw[CAL_Z_NEG][2];
    int16_t zx, zy, zz;
    float sx, sy, sz;

    /* Face detector already guarantees >1.4g separation. Keep a second guard
     * before dividing so corrupted/stale samples can never create bad slopes. */
    if (dx < CAL_MIN_SPAN || dy < CAL_MIN_SPAN || dz < CAL_MIN_SPAN) {
        s_error = 1U;
        cal_update_labels();
        return;
    }

    zx = (int16_t)(((int32_t)s_face_raw[CAL_X_POS][0] + s_face_raw[CAL_X_NEG][0]) / 2);
    zy = (int16_t)(((int32_t)s_face_raw[CAL_Y_POS][1] + s_face_raw[CAL_Y_NEG][1]) / 2);
    zz = (int16_t)(((int32_t)s_face_raw[CAL_Z_POS][2] + s_face_raw[CAL_Z_NEG][2]) / 2);
    sx = 2000.0f / (float)dx;
    sy = 2000.0f / (float)dy;
    sz = 2000.0f / (float)dz;

    /* Runtime and NVM switch together only after all six faces are valid. */
    SC7A20_SetXCalibration(zx, sx);
    SC7A20_SetYCalibration(zy, sy);
    SC7A20_SetZCalibration(zz, sz);
    SC7A20_AlgoInit();  /* 丢弃旧校准参数下缓存的NORMAL/FLIPPED判定 */
    nvm_set_sc7a20_calibration(zx, sx, zy, sy, zz, sz);

    s_saving = 1U;
    cal_update_labels();
}

static void cal_capture(cal_face_t face)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++) {
        s_face_raw[face][i] = (int16_t)(s_sum[i] / (int32_t)s_sample_count);
    }
    s_done[face] = 1U;
    cal_reset_candidate();
    cal_update_labels();

    if (cal_all_done() != 0U) cal_finish();
}

static void cal_exit_async(void *user_data)
{
    (void)user_data;
    if (s_scr) menu_app_exit();
}

static void cal_timer_cb(lv_timer_t *t)
{
    int16_t raw[3];
    cal_face_t face;
    uint8_t i;
    (void)t;

    if (s_saving != 0U) {
        /* load_task是唯一NVM写者。确认dirty真正清零后异步退出，
         * 避免在当前LVGL timer回调里删除自身。 */
        if (nvm_is_dirty() == 0U && s_exit_queued == 0U) {
            s_exit_queued = 1U;
            lv_timer_pause(s_timer);
            (void)lv_async_call(cal_exit_async, NULL);
        }
        return;
    }
    if (s_error != 0U || !SC7A20_IsInitialized()) return;

    SC7A20_ReadRawCode(&raw[0], &raw[1], &raw[2]);
    face = cal_detect_face(raw[0], raw[1], raw[2]);

    if (face == CAL_FACE_NONE || s_done[face] != 0U) {
        cal_reset_candidate();
        return;
    }

    if (face != s_candidate) {
        cal_start_candidate(face, raw[0], raw[1], raw[2]);
        return;
    }

    for (i = 0U; i < 3U; i++) {
        if (raw[i] < s_min[i]) s_min[i] = raw[i];
        if (raw[i] > s_max[i]) s_max[i] = raw[i];

        if ((int32_t)s_max[i] - (int32_t)s_min[i] > CAL_STABLE_RANGE) {
            cal_start_candidate(face, raw[0], raw[1], raw[2]);
            return;
        }
    }

    for (i = 0U; i < 3U; i++) s_sum[i] += raw[i];
    s_sample_count++;
    if (++s_hold_intervals >= CAL_HOLD_INTERVALS) cal_capture(face);
}

static lv_obj_t *cal_make_label(const char *text)
{
    lv_obj_t *label = lv_label_create(s_scr);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, menu_font_small(), 0);
    lv_label_set_text_static(label, text);
    return label;
}

static void gravity_calibrate_create(void)
{
    uint8_t i;

    if (s_scr) return;

    for (i = 0U; i < CAL_FACE_COUNT; i++) s_done[i] = 0U;
    s_saving = 0U;
    s_error = 0U;
    s_exit_queued = 0U;
    cal_reset_candidate();

    pm_api_set_sleep_block(PM_BLOCK_CALIBRATION, 1U);
    pm_api_refresh_idle();
    ui_orientation_lock_set(1U);

    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, 160, 40);
    lv_obj_set_scrollbar_mode(s_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    s_up = cal_make_label("UP");
    lv_obj_align(s_up, LV_ALIGN_TOP_MID, 0, -1);

    s_down = cal_make_label("DOWN");
    lv_obj_align(s_down, LV_ALIGN_BOTTOM_MID, 0, 1);

    s_left = cal_make_label("LEFT");
    lv_obj_align(s_left, LV_ALIGN_LEFT_MID, 1, 0);

    s_right = cal_make_label("RIGHT");
    lv_obj_align(s_right, LV_ALIGN_RIGHT_MID, -1, 0);

    s_face = cal_make_label("FACE U-- D--");
    lv_obj_set_width(s_face, 82);
    lv_obj_set_style_text_align(s_face, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_face, LV_ALIGN_CENTER, 0, 0);

    s_timer = lv_timer_create(cal_timer_cb, CAL_PERIOD_MS, NULL);
    lv_obj_invalidate(s_scr);
}

static void gravity_calibrate_activate(void)
{
    if (s_scr) {
        lv_screen_load(s_scr);
        lv_obj_invalidate(s_scr);
    }
}

static void gravity_calibrate_run(app_action_t action)
{
    /* 六面采集期间任意实体按键退出；一旦进入NVM提交阶段则等待真正落盘后自动退出。 */
    if (s_saving == 0U && action != APP_ACTION_NONE) menu_app_exit();
}

static void gravity_calibrate_destroy(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_up = s_down = s_left = s_right = s_face = NULL;
    pm_api_set_sleep_block(PM_BLOCK_CALIBRATION, 0U);
    pm_api_refresh_idle();
    ui_orientation_lock_set(0U);
}

const menu_app_t menu_app_gravity_calibrate = {
    .label    = "Gravity Calibrate",
    .create   = gravity_calibrate_create,
    .activate = gravity_calibrate_activate,
    .run      = gravity_calibrate_run,
    .destroy  = gravity_calibrate_destroy,
};
