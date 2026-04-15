#include "buttons.h"

#ifndef DEBOUNCE_TIME_10MS
    #define DEBOUNCE_TIME_10MS              3
#endif

#define DEFAULT_LONGPRESS_TIME_10MS     150

/****************************用户按键配置区************************************/
/***********************在这里编写按键状态获取函数*****************************/
uint8_t KEY_NEXT_GetIO(void) {
    return gpio_input_data_bit_read(NEXT_GPIO_PORT, NEXT_PIN);
}
uint8_t KEY_CONF_GetIO(void) {
    return gpio_input_data_bit_read(CONF_GPIO_PORT, CONF_PIN);
}
uint8_t KEY_PREV_GetIO(void) {
    return gpio_input_data_bit_read(MENU_GPIO_PORT, MENU_PIN);
}

const KeyInfo_t KeyInfo_Array[KeyIndex_Max] = {
    /*{LongPressTime, DASInterval, MultiClickTime, ZeroIsPress, GetIOFunc(Must not NULL)} 
     * LongPressTime：长按判定时间，为0时使用默认长按时间
     * DASInterval：触发长按连击时的连击时间间隔，不能为0
     * MultiClickTime：连击判定超时时间
     * ZeroIsPress低电平代表按下
     * GetIOFunc获取按键状态的uint8_t func(void)型函数名 */
    {100, 20, 50, 1, KEY_NEXT_GetIO},
    {100, 20, 50, 1, KEY_CONF_GetIO},
    {100, 20, 50, 1, KEY_PREV_GetIO},
};

/***********************************功能代码***************************************/
static Key_t KeyValue_Array[KeyIndex_Max];                                      //储存着按键所有状态的数组


void Key_Scand(void) {
    uint8_t i;
    uint8_t IOState;

    for (i = 0; i < KeyIndex_Max; ++i) {                                        //对于for循环i++与++i等价，但后者执行效率高
        IOState = KeyInfo_Array[i].GetIOFunc();
        IOState = (KeyInfo_Array[i].ZeroIsPress ? (!IOState) : (IOState));

        if (KeyValue_Array[i].CurrIOState != IOState) {
            KeyValue_Array[i].CurrIOState = IOState;
            KeyValue_Array[i].DebounceCounter = DEBOUNCE_TIME_10MS;             //键值变化，开启消抖计数器
        }

        //Wait for debounce
        if (KeyValue_Array[i].LastIOState != KeyValue_Array[i].CurrIOState) {   //键值变化
            if (KeyValue_Array[i].DebounceCounter == 0) {                       //消抖时间到
                if ((KeyValue_Array[i].CurrIOState) && (!KeyValue_Array[i].LastIOState)) {  //上次为假，这次为真
                    KeyValue_Array[i].State = KeyState_ShortPress;                          //识别为短按
                    
                    if (KeyInfo_Array[i].LongPressTime) {       //如果对应按键开启了长按
                        KeyValue_Array[i].DebounceCounter = KeyInfo_Array[i].LongPressTime;
                    } 
                    else {
                        KeyValue_Array[i].DebounceCounter = DEFAULT_LONGPRESS_TIME_10MS;
                    }
                } 
                else if ((!KeyValue_Array[i].CurrIOState) && (KeyValue_Array[i].LastIOState)) { //上次为真，这次为假
                    KeyValue_Array[i].State = KeyState_Release;                                 //按键被释放
                } 
                else {
                    KeyValue_Array[i].State = KeyState_None;
                }
                KeyValue_Array[i].LastIOState = KeyValue_Array[i].CurrIOState;
            }
        }
        
        if (KeyValue_Array[i].State == KeyState_ShortPress) {                   //已经是短按状态，计时器归零后进入长按
            if (KeyValue_Array[i].DebounceCounter == 0) {
                KeyValue_Array[i].State = KeyState_LongPress;
                KeyValue_Array[i].DebounceCounter = KeyInfo_Array[i].DASInterval;                
            }
        }
        //更新边沿
        if(KeyValue_Array[i].State == KeyValue_Array[i].LastState) KeyValue_Array[i].Edge = KeyEdge_Null;
        else if((KeyValue_Array[i].State == KeyState_ShortPress)&&((KeyValue_Array[i].LastState == KeyState_None)||(KeyValue_Array[i].LastState == KeyState_Release))) KeyValue_Array[i].Edge = KeyEdge_Rising;
        else if((KeyValue_Array[i].State == KeyState_Release)&&((KeyValue_Array[i].LastState == KeyState_ShortPress)||(KeyValue_Array[i].LastState == KeyState_LongPress))) KeyValue_Array[i].Edge = KeyEdge_Falling;
        else if((KeyValue_Array[i].State == KeyState_LongPress)&&(KeyValue_Array[i].LastState == KeyState_ShortPress)) KeyValue_Array[i].Edge = KeyEdge_Holding;
        else KeyValue_Array[i].Edge = KeyEdge_Error;        
        KeyValue_Array[i].LastState = KeyValue_Array[i].State;
        //更新连击
        if(KeyValue_Array[i].Edge == KeyEdge_Rising) KeyValue_Array[i].ClickCounter = KeyInfo_Array[i].MultiClickTime;
        if(KeyValue_Array[i].ClickCounter && KeyValue_Array[i].Edge == KeyEdge_Falling){
            KeyValue_Array[i].MultiClick++;
            KeyValue_Array[i].ClickCounter = KeyInfo_Array[i].MultiClickTime;
        }
    }
}

void Key_DebounceService_10ms(void) {
    uint8_t i;
    for (i = 0; i < KeyIndex_Max; ++i) {
        if(KeyValue_Array[i].DebounceCounter) {
            KeyValue_Array[i].DebounceCounter--;
        }
        if(KeyValue_Array[i].ClickCounter){
            KeyValue_Array[i].ClickCounter--;
        }
        else KeyValue_Array[i].MultiClick = 0;
    }
}

void Key_Init(void) {
    uint8_t i = 0;
    for(i = 0; i < KeyIndex_Max; ++i) {
        KeyValue_Array[i].CurrIOState = 0;
        KeyValue_Array[i].LastIOState = 0;
        KeyValue_Array[i].DebounceCounter = 0;
        KeyValue_Array[i].MultiClick = 0;
        KeyValue_Array[i].ClickCounter = 0;
        KeyValue_Array[i].State = KeyState_None;
        KeyValue_Array[i].LastState = KeyState_None;
        KeyValue_Array[i].Edge = KeyEdge_Null;
    }
}

KeyEdge_t Key_EdgeDetect(KeyIndex_t KeySelector) {
    return KeyValue_Array[KeySelector].Edge;
}

KeyState_t KEY_GetState(KeyIndex_t KeySelector) {
    return KeyValue_Array[KeySelector].State;
}

uint8_t KEY_GetDASClick(KeyIndex_t KeySelector) {
    KeyEdge_t Edge = Key_EdgeDetect(KeySelector);
    if(Edge == KeyEdge_Rising) return 1;    //有按键按下
    if(Edge == KeyEdge_Holding && KeyInfo_Array[KeySelector].DASInterval) return 1; //识别到长按的瞬间也触发
    if(KeyInfo_Array[KeySelector].DASInterval && KEY_GetState(KeySelector) == KeyState_LongPress && KeyValue_Array[KeySelector].DebounceCounter == 0){
        KeyValue_Array[KeySelector].DebounceCounter = KeyInfo_Array[KeySelector].DASInterval;
        return 1;
    }
    return 0;
}

/*指定按键当前的连击次数是否等于输入值
/连击指按下，在指定时间内松开，计一次点击，指定时间内不再次点击则归零，若再次点击并松开，次数加一
/等于后连击计数器归零，使连击判定只生效一次
*/
uint8_t KEY_GetClickTimes(KeyIndex_t KeySelector, uint8_t times) {
    if(KeyValue_Array[KeySelector].MultiClick == times){
        KeyValue_Array[KeySelector].MultiClick = 0;
        return 1;
    }
    else return 0;
}
