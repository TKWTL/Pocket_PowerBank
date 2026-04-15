/*
 *按键支持库文件
 *提供轻量级非阻塞式按键消抖、边沿检测与多平台兼容性
 *
 *技术来源：https://blog.csdn.net/hxj_csdn/article/details/104318108
 *
 *需要在defines.h加入的宏定义：（未定义将采用默认设置）
 *DEBOUNCE_TIME_10MS
*/

#ifndef __BUTTONS_H__
#define __BUTTONS_H__

#ifdef __cplusplus
extern "C" {
#endif
    
/*****************************用户配置区开始***********************************/
#include "at32f423_wk_config.h"                                                 //包含按键状态读取函数的库
/*****************************用户配置区结束***********************************/

    typedef enum {
        KeyState_None,
        KeyState_ShortPress,
        KeyState_LongPress,                                                     //在得到LongPress前会先得到一个ShortPress
        KeyState_Release,
    } KeyState_t;                                                               //按键状态枚举

    typedef enum {
        KeyEdge_Null,                                                           //按键状态无变化
        KeyEdge_Rising,                                                         //从None/Release到ShortPress
        KeyEdge_Falling,                                                        //从ShortPress/LongPress到Release
        KeyEdge_Holding,                                                        //从ShortPress到LongPress
        KeyEdge_Error,                                                          //正常程序不会出现的情况
    } KeyEdge_t;                                                                //按键变化边沿类型枚举

    typedef uint8_t (*fpGetIOStateFunc)(void);
    typedef struct {
        uint8_t LongPressTime;
        uint8_t DASInterval;
        uint8_t MultiClickTime;
        uint8_t ZeroIsPress;
        fpGetIOStateFunc GetIOFunc;
    } KeyInfo_t;

    typedef struct {
        uint8_t CurrIOState;
        uint8_t LastIOState;
        uint8_t DebounceCounter;
        uint8_t MultiClick;
        uint8_t ClickCounter;
        KeyState_t State;
        KeyState_t LastState;
        KeyEdge_t Edge;
    } Key_t;


typedef enum {
/*****************************用户配置区开始***********************************/
/***************************在这里注册按键名称*********************************/
    KeyIndex_NEXT,
    KeyIndex_CONF,
    KeyIndex_MENU,
/*******************************用户配置区结束*********************************/
    KeyIndex_Max
} KeyIndex_t;                                                                   //用户配置的按键枚举表单

/**********************************函数声明区**********************************/
    extern void Key_Init(void);                                                 //初始化和唤醒时执行
    extern void Key_DebounceService_10ms(void);                                 //减小消抖计时器值，每10ms执行一次
    extern void Key_Scand(void);                                                //刷新键值，每10ms执行一次
    extern KeyEdge_t Key_EdgeDetect(KeyIndex_t KeySelector);                    //按键边沿检测，读取它不会改变已有状态
    extern KeyState_t KEY_GetState(KeyIndex_t KeySelector);                     //获取按键状态，读取它不会改变已有状态
    uint8_t KEY_GetDASClick(KeyIndex_t KeySelector);                            //延迟连击读取函数，在按下与触发一次，并在长按后触发连击
    /*指定按键当前的连击次数是否等于输入值
    /连击指按下，在指定时间内松开，计一次点击，指定时间内不再次点击则归零，若再次点击并松开，次数加一
    /等于后连击计数器归零，使连击判定只生效一次*/
    uint8_t KEY_GetClickTimes(KeyIndex_t KeySelector, uint8_t times);

#ifdef __cplusplus
}
#endif

#endif