/*
 * This file is part of the MiaoUI Library.
 *
 * Copyright (c) 2025, JFeng-Z, <2834294740@qq.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Created on: 2025-02-08
 */
#include "widget/parameter.h"
#include "stdio.h"
#include "core/ui.h"
#include "display/dispDriver.h"

void ParameterSetting_Widget(ui_t *ui)
{
    uint8_t color;
    char value[20] = {0};
    int x = 4, y = 12, w = UI_HOR_RES - 8, h = UI_VER_RES - 32;
    float Step = ui->nowItem->element->data->step;

    #if ( UI_USE_FREERTOS == 1 )
    if(ui->nowItem->element->data->functionType == UI_DATA_FUNCTION_EXIT_EXECUTE)
    {
        if(ui->nowItem->element->data->dataRootMutex != NULL)xSemaphoreTake(*ui->nowItem->element->data->dataRootMutex, portMAX_DELAY);
        if(ui->nowItem->element->data->dataRootTask != NULL)vTaskSuspend(*ui->nowItem->element->data->dataRootTask);
        if(ui->nowItem->element->data->dataRootMutex != NULL)xSemaphoreGive(*ui->nowItem->element->data->dataRootMutex);
    }
    #endif
    
    if(Dialog_Show(ui, x, y, w, h))
    {
        switch (ui->action)
        {
        case UI_ACTION_PLUS:
            Draw_Scrollbar(ui, x + 4, y + 18, w - 12, 6, 3, Step);
            if(ui->nowItem->element->data->function != NULL && ui->nowItem->element->data->functionType == UI_DATA_FUNCTION_STEP_EXECUTE)ui->nowItem->element->data->function(ui);
            break;
        case UI_ACTION_MINUS:
            Step = -Step;
            Draw_Scrollbar(ui, x + 4, y + 18, w - 12, 6, 3, Step);
            if(ui->nowItem->element->data->function != NULL && ui->nowItem->element->data->functionType == UI_DATA_FUNCTION_STEP_EXECUTE)ui->nowItem->element->data->function(ui);
            break;
        default:
            Draw_Scrollbar(ui, x + 4, y + 18, w - 12, 6, 3, 0);
            break;
        }
        
        if(ui->nowItem->element->data->name == NULL) ui->nowItem->element->data->name = "Null!!";
        
        switch (ui->nowItem->element->data->dataType)
        {
        case UI_DATA_INT:
            snprintf(value, sizeof(value), "%s:%d ", ui->nowItem->element->data->name, *(int *)(ui->nowItem->element->data->ptr));
            break;
        case UI_DATA_FLOAT:
            snprintf(value, sizeof(value), "%s:%.2f ", ui->nowItem->element->data->name, *(float *)(ui->nowItem->element->data->ptr));
        default:
            break;
        }
        //清除上一次的字符
        Disp_SetDrawColor(&ui->bgColor);
        Disp_DrawBox(x + 10, y + 1, 108, 12);
        color = ui->bgColor ? 0 : 1;
        Disp_SetDrawColor(&color);
        //绘制数据
        Disp_DrawStr(x + 4, y + 13, value);
        Disp_SendBuffer();
    }
}

void Switch_Widget(ui_t *ui)
{
    ui_data_t *data;
    ui_page_t *page;
    ui_item_t *it;

    if (ui == NULL || ui->nowItem == NULL || ui->nowItem->element == NULL || ui->nowItem->element->data == NULL)
        return;

    data = ui->nowItem->element->data;
    if (data->dataType != UI_DATA_SWITCH || data->ptr == NULL)
        return;

    /* ===== 多选一（radio）判定：min==1 且 max>0 ===== */
    if (data->min == 1 && data->max > 0)
    {
        int group_id = data->max;

        /* 先全 0 */
        page = ui->nowItem->page.location;
        it = page->item.head;
        for (uint16_t i = 0; i <= page->length; i++)
        {
            if (it->itemType == UI_ITEM_DATA &&
                it->element && it->element->data &&
                it->element->data->dataType == UI_DATA_SWITCH &&
                it->element->data->min == 1 &&              /* 也是 radio */
                it->element->data->max == group_id &&       /* 同组 */
                it->element->data->ptr != NULL)
            {
                *(uint8_t *)it->element->data->ptr = 0;
            }
            it = it->nextItem;
        }

        /* 再把当前项写 1（保证同组始终只有一个为 1） */
        *(uint8_t *)data->ptr = 1;
    }
    else
    {
        /* ===== 普通 switch（多选）保持原行为：翻转 ===== */
        *(uint8_t *)data->ptr = !*(uint8_t *)data->ptr;
    }

    /* ===== 保留用户自定义回调，不占用 function ===== */
    if (data->function != NULL)
        data->function(ui);

    /* ===== 原有 RTOS task suspend/resume 逻辑保持不变 ===== */
    #if ( UI_USE_FREERTOS == 1 )
    if (data->dataRootTask != NULL)
    {
        switch (*(uint8_t *)data->ptr)
        {
        case false:
            vTaskResume(*data->dataRootTask);
            break;
        case true:
            vTaskSuspend(*data->dataRootTask);
            break;
        default:
            break;
        }
    }
    #endif
}
