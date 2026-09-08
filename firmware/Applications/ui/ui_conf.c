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
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"//使用的图片资源

//需要调用的项目函数
#include "application.h"

/*Page*/
ui_page_t Menu_Page;
ui_page_t Setting_Page, Power_Page;
ui_page_t Game_Page;
ui_page_t Tool_Page;

/*item */
ui_item_t HomeHead_Item;

ui_item_t Setting_Item, SettingHead_Item;

ui_item_t Power_Item, PowerHead_Item;

ui_item_t Game_Item, GameHead_Item;
ui_item_t Snake_Item;
ui_item_t Dino_Item;

ui_item_t Tool_Item, ToolHead_Item;
ui_item_t Burn_in_Test_Item;

ui_item_t Github_Item;

/**
 * 在此建立所需显示或更改的数据
 * 无参数
 * 无返回值
 */
void Create_Parameter(ui_t *ui)
{    
    //设置区，这些值在调用设置前必须进入有效范围，否则卡死！
    Create_ConvMode_Parameters(ui);
    Create_PWM_Parameters(ui);
    Create_ADC_Parameters(ui);
    Create_Protection_Parameters1(ui);
    Create_Protection_Parameters2(ui);
    
    Create_Disp_Parameters(ui);
    
    Create_TimeSet_Parameters(ui);
    
    Create_SystemCtrl_Parameters(ui);
    
    //数据区
    Create_Statistics_Parameters(ui);
}

/**
 * 在此建立所需显示或更改的文本
 * 无参数
 * 无返回值
 */
void Create_Text(ui_t *ui)
{
    static ui_text_t github_text;
    github_text.font = UI_FONT;
    github_text.fontHight = UI_FONT_HIGHT;
    github_text.fontWidth = UI_FONT_WIDTH;
    github_text.ptr = "    VSG Inverter\n Git:github.com/TKWTL/VSG_Inverter\n UI Provider:github.com/JFeng-Z/MiaoUI";
    static ui_element_t github_element;
    github_element.text = &github_text;
    Create_element(&Github_Item, &github_element);
}

/*
 * 菜单构建函数
 * 该函数不接受参数，也不返回任何值。
 * 功能：静态地构建一个菜单系统。
 */
/**
 * 添加项目到页面
 * 
 * 该函数用于向指定的页面添加一个新的项目。项目包括名称、类型、元素、图像等信息，
 * 并且可以设置项目的功能回调。添加的项目将按照链表的形式管理，同时维护页面上项目的数量。
 *
 * @param name 项目名称，不可为NULL。
 * @param type 项目类型，如数据、开关、文本等。
 * @param Element 项目关联的元素，根据项目类型不同，包含不同的数据。
 * @param image 项目显示的图像，如果为NULL，则使用预设图像。
 * @param item 待添加的项目结构体。
 * @param localPage 项目所在的页面结构体。
 * @param nextPage 项目跳转的下一个页面结构体，如果无跳转，则为NULL。
 * @param function 项目对应的函数指针，用于处理项目特定的功能。
 */
/**
 * @brief 向页面中添加一个新页面。
 * 
 * @param name 新页面的名称。
 * @param page 指向新页面结构体的指针。
 * @param type 新页面的类型。
 */
void Create_MenuTree(ui_t *ui)
{
    AddPage("[MainPage]", &Menu_Page, UI_PAGE_ICON, NULL);
    
        AddItem("-DashBoard", UI_ITEM_ONCE_FUNCTION, img_dashboard, &HomeHead_Item, &Menu_Page, NULL, DashBoard);
    
        AddItem("-Settings", UI_ITEM_PARENTS, img_configuration, &Setting_Item, &Menu_Page, &Setting_Page, NULL);
            AddPage("[Setting]", &Setting_Page, UI_PAGE_ICON, &Menu_Page);
                AddItem("[Menu]", UI_ITEM_RETURN, img_home, &SettingHead_Item, &Setting_Page, &Menu_Page, NULL);
	
                AddItem("-PowerSet", UI_ITEM_PARENTS, img_power, &Power_Item, &Setting_Page, &Power_Page, NULL);
                    AddPage("[Setting]", &Power_Page, UI_PAGE_TEXT, &Setting_Page);
                        AddItem("[Menu]", UI_ITEM_RETURN, NULL, &PowerHead_Item, &Power_Page, &Setting_Page, NULL);
                        Add_ConvMode_Items(&Power_Page);
                        Add_PWM_Items(&Power_Page);
                        Add_ADC_Items(&Power_Page);
                
                Add_Protection_Page(&Setting_Page);
    
                Add_Disp_Items(&Setting_Page);
                Add_TimeSet_Items(&Setting_Page);
                Add_SystemCtrl_Items(&Setting_Page);
                
        Add_Statistics_Items(&Menu_Page);
                
        AddItem("-Tools", UI_ITEM_PARENTS, img_tools, &Tool_Item, &Menu_Page, &Tool_Page, NULL);
            AddPage("[ToolPage]", &Tool_Page, UI_PAGE_ICON, &Menu_Page);
                AddItem("[Menu]", UI_ITEM_RETURN, img_home, &ToolHead_Item, &Tool_Page, &Menu_Page, NULL);
                AddItem(" Burn-in Test", UI_ITEM_ONCE_FUNCTION, img_burn_in, &Burn_in_Test_Item, &Tool_Page, NULL, Burn_in_Tester);
    
        AddItem("-Games", UI_ITEM_PARENTS, img_games, &Game_Item, &Menu_Page, &Game_Page, NULL);
            AddPage("[GamePage]", &Game_Page, UI_PAGE_ICON, &Menu_Page);
                AddItem("[Menu]", UI_ITEM_RETURN, img_home, &GameHead_Item, &Game_Page, &Menu_Page, NULL);
                AddItem("-Game Snake", UI_ITEM_ONCE_FUNCTION, img_snake, &Snake_Item, &Game_Page, NULL, Game_Snake);
                AddItem("-Game Dinosaur", UI_ITEM_ONCE_FUNCTION, img_dinosuar, &Dino_Item, &Game_Page, NULL, Game_DinoSaur);
                
        AddItem("-About", UI_ITEM_WORD, img_toby_fox, &Github_Item, &Menu_Page, NULL, NULL);
}

void MiaoUi_Setup(ui_t *ui)
{
    Create_UI(ui, &HomeHead_Item); // 创建UI, 必须给定一个头项目
    DashBoard(ui);
}
