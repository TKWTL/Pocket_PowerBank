
/**
 * @file lv_port_indev_templ.h
 *
 */

/*Copy this file as "lv_port_indev.h" and set this value to "1" to enable content*/
#if 1

#ifndef LV_PORT_INDEV_TEMPL_H
#define LV_PORT_INDEV_TEMPL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

#define LV_USE_INDEV_TOUCHPAD 	0x1u
#define LV_USE_INDEV_MOUSE	 	0x2u
#define LV_USE_INDEV_KEYPAD 	0x4u
#define LV_USE_INDEV_ENCODER 	0x8u
#define LV_USE_INDEV_BUTTON 	0x10u
#define LV_USE_INDEV  	        LV_USE_INDEV_KEYPAD			// 使用keypad



/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
 
#if ( LV_USE_INDEV & LV_USE_INDEV_TOUCHPAD ) == LV_USE_INDEV_TOUCHPAD
extern lv_indev_t * indev_touchpad;
#endif

#if ( LV_USE_INDEV & LV_USE_INDEV_MOUSE ) == LV_USE_INDEV_MOUSE
extern lv_indev_t * indev_mouse;
#endif

#if ( LV_USE_INDEV & LV_USE_INDEV_KEYPAD ) == LV_USE_INDEV_KEYPAD
extern lv_indev_t * indev_keypad;
#endif

#if ( LV_USE_INDEV & LV_USE_INDEV_ENCODER ) == LV_USE_INDEV_ENCODER
extern lv_indev_t * indev_encoder;
#endif

#if ( LV_USE_INDEV & LV_USE_INDEV_BUTTON ) == LV_USE_INDEV_BUTTON
extern lv_indev_t * indev_button;
#endif
 
void lv_port_indev_init(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_INDEV_TEMPL_H*/

#endif /*Disable/Enable content*/
