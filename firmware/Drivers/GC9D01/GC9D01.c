/*

  ******************************************************************************
  * @file 			( фаил ):   GC9D01.c
  * @brief 		( описание ):  	
  ******************************************************************************
  * @attention 	( внимание ):	 author: Golinskiy Konstantin	e-mail: golinskiy.konstantin@gmail.com
  ******************************************************************************
  
*/

#include <GC9D01.h>


uint16_t GC9D01_X_Start = GC9D01_XSTART;	
uint16_t GC9D01_Y_Start = GC9D01_YSTART;

uint16_t GC9D01_Width = 0;
uint16_t GC9D01_Height = 0;


static void GC9D01_SendCmd(uint8_t Cmd);
static void GC9D01_SendData(uint8_t Data );
static void GC9D01_SendDataMASS(uint8_t* buff, size_t buff_size);
void GC9D01_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void GC9D01_RamWrite(uint8_t *pBuff, uint32_t Len);
static void GC9D01_ColumnSet(uint16_t ColumnStart, uint16_t ColumnEnd);
static void GC9D01_RowSet(uint16_t RowStart, uint16_t RowEnd);
static void SwapInt16Values(int16_t *pValue1, int16_t *pValue2);
static void GC9D01_DrawLine_Slow(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);

	
//##############################################################################
	  
	  
//==============================================================================
	  
	  
	  
//==============================================================================
// Процедура инициализации дисплея
//==============================================================================
void GC9D01_Init(void){	
	GC9D01_Width = GC9D01_WIDTH;
	GC9D01_Height = GC9D01_HEIGHT;

    GC9D01_HardReset(); 
	
	vTaskDelay(200);	
	
	//--- init ------------
	GC9D01_SendCmd(0xFE);
    GC9D01_SendCmd(0xEF);

	// Включение внутреннего регистра 80~8Fh -----
    GC9D01_SendCmd(0x80);
    GC9D01_SendData(0xFF);

    GC9D01_SendCmd(0x81);
    GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x82);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x83);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x84);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x85);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x86);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x87);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x88);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x89);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x8A);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x8B);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x8C);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x8D);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x8E);
  GC9D01_SendData(0xFF);

  GC9D01_SendCmd(0x8F);
  GC9D01_SendData(0xFF);
	//--------------------------------------
	
	// Установить режим переворота
  GC9D01_SendCmd(0xEC);
  GC9D01_SendData(0x11);

	// Размер VGL
  GC9D01_SendCmd(0x7E);
  GC9D01_SendData(0x7a);

	// Изменить частоту кадров
  GC9D01_SendCmd(0x74);
  GC9D01_SendData(0x02);
  GC9D01_SendData(0x0E);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x28);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
	
	// Внутренняя регулировка напряжения
  GC9D01_SendCmd(0x98);
  GC9D01_SendData(0x3E);
  GC9D01_SendCmd(0x99);
  GC9D01_SendData(0x3E);

	// Внутренние настройки porch
  GC9D01_SendCmd(0xB5);		//  Blanking Porch Control (B5h) VFP=14 VBP=14 HBP=Off
  GC9D01_SendData(0x0E);
  GC9D01_SendData(0x0E);

	// начало времени gip
  GC9D01_SendCmd(0x60);
  GC9D01_SendData(0x38);
  GC9D01_SendData(0x09);
  GC9D01_SendData(0x6D);
  GC9D01_SendData(0x67);


  GC9D01_SendCmd(0x63);
  GC9D01_SendData(0x38);
  GC9D01_SendData(0xAD);
  GC9D01_SendData(0x6D);
  GC9D01_SendData(0x67);
  GC9D01_SendData(0x05);


  GC9D01_SendCmd(0x64);
  GC9D01_SendData(0x38);
  GC9D01_SendData(0x0B);
  GC9D01_SendData(0x70);
  GC9D01_SendData(0xAB);
  GC9D01_SendData(0x6D);
  GC9D01_SendData(0x67);


  GC9D01_SendCmd(0x66);
  GC9D01_SendData(0x38);
  GC9D01_SendData(0x0F);
  GC9D01_SendData(0x70);
  GC9D01_SendData(0xAF);
  GC9D01_SendData(0x6d);
  GC9D01_SendData(0x67);

  GC9D01_SendCmd(0x6A);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);

  GC9D01_SendCmd(0x68);
  GC9D01_SendData(0x3B);
  GC9D01_SendData(0x08);
  GC9D01_SendData(0x04);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x04);
  GC9D01_SendData(0x64);
  GC9D01_SendData(0x67);



  GC9D01_SendCmd(0x6C);
  GC9D01_SendData(0x22);
  GC9D01_SendData(0x02);
  GC9D01_SendData(0x22);
  GC9D01_SendData(0x02);
  GC9D01_SendData(0x22);
  GC9D01_SendData(0x22);
  GC9D01_SendData(0x50);

  GC9D01_SendCmd(0x6E);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x07);
  GC9D01_SendData(0x01);
  GC9D01_SendData(0x13);
  GC9D01_SendData(0x11);

  GC9D01_SendData(0x0B);
  GC9D01_SendData(0x09);
  GC9D01_SendData(0x16);
  GC9D01_SendData(0x15);
  GC9D01_SendData(0x1D);
  GC9D01_SendData(0x1E);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);

  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x1E);

  GC9D01_SendData(0x1D);
  GC9D01_SendData(0x15);
  GC9D01_SendData(0x16);
  GC9D01_SendData(0x0A);
  GC9D01_SendData(0x0C);

  GC9D01_SendData(0x12);
  GC9D01_SendData(0x14);
  GC9D01_SendData(0x02);
  GC9D01_SendData(0x08);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
  GC9D01_SendData(0x00);
	// окончание времени gip


	// Начинается настройка внутреннего напряжения
  GC9D01_SendCmd(0xA9);
  GC9D01_SendData(0x1B);

  GC9D01_SendCmd(0xA8);
  GC9D01_SendData(0x6B);

  GC9D01_SendCmd(0xA8);
  GC9D01_SendData(0x6D);

  GC9D01_SendCmd(0xA7);
  GC9D01_SendData(0x40);

  GC9D01_SendCmd(0xAD);
  GC9D01_SendData(0x47);


  GC9D01_SendCmd(0xAF);
  GC9D01_SendData(0x73);

  GC9D01_SendCmd(0xAF);
  GC9D01_SendData(0x73);

  GC9D01_SendCmd(0xAC);
  GC9D01_SendData(0x44);

  GC9D01_SendCmd(0xA3);
  GC9D01_SendData(0x6C);

		
  GC9D01_SendCmd(0xCB);
  GC9D01_SendData(0x00);

  GC9D01_SendCmd(0xCD);
  GC9D01_SendData(0x22);


  GC9D01_SendCmd(0xC2);
  GC9D01_SendData(0x10);

  GC9D01_SendCmd(0xC5);
  GC9D01_SendData(0x00);

  GC9D01_SendCmd(0xC6);
  GC9D01_SendData(0x0E);

  GC9D01_SendCmd(0xC7);
  GC9D01_SendData(0x1f);

  GC9D01_SendCmd(0xC8);
  GC9D01_SendData(0x0E);
	// Настройка внутреннего напряжения завершена

	// Выберите режим одиночных ворот
  GC9D01_SendCmd(0xbf);		//  Dual-Single Gate Select (BFh) 0=>Single gate
  GC9D01_SendData(0x00);

	//Корректировки, связанные с SOU
  GC9D01_SendCmd(0xF9);
  GC9D01_SendData(0x20);

	//регулировка напряжения vreg
  GC9D01_SendCmd(0x9b);
  GC9D01_SendData(0x3b);
  GC9D01_SendCmd(0x93);
  GC9D01_SendData(0x33);
  GC9D01_SendData(0x7f);
  GC9D01_SendData(0x00);

	//регулировка VGH/VGL CLK 70，71h
  GC9D01_SendCmd(0x70);
  GC9D01_SendData(0x0E);
  GC9D01_SendData(0x0f);
  GC9D01_SendData(0x03);
  GC9D01_SendData(0x0e);
  GC9D01_SendData(0x0f);
  GC9D01_SendData(0x03);

  GC9D01_SendCmd(0x71);
  GC9D01_SendData(0x0e);
  GC9D01_SendData(0x16);
  GC9D01_SendData(0x03);

	// Внутренняя регулировка напряжения
    GC9D01_SendCmd(0x91);
    GC9D01_SendData(0x0e);
    GC9D01_SendData(0x09);

	// регулировка напряжения vreg
    GC9D01_SendCmd(0xc3);
    GC9D01_SendData(0x2c);
    GC9D01_SendCmd(0xc4);
    GC9D01_SendData(0x1a);

	// gamma F1~F3h
    GC9D01_SendCmd(0xf0);		// SET_GAMMA1 (F0h)
    GC9D01_SendData(0x51);
    GC9D01_SendData(0x13);
    GC9D01_SendData(0x0c);
    GC9D01_SendData(0x06);
    GC9D01_SendData(0x00);
    GC9D01_SendData(0x2f);

    GC9D01_SendCmd(0xf2);		// SET_GAMMA3 (F2h)
    GC9D01_SendData(0x51);
    GC9D01_SendData(0x13);
    GC9D01_SendData(0x0c);
    GC9D01_SendData(0x06);
    GC9D01_SendData(0x00);
    GC9D01_SendData(0x33);

    GC9D01_SendCmd(0xf1);		// SET_GAMMA2 (F1h)
    GC9D01_SendData(0x3c);
    GC9D01_SendData(0x94);
    GC9D01_SendData(0x4f);
    GC9D01_SendData(0x33);
    GC9D01_SendData(0x34);
    GC9D01_SendData(0xCf);

    GC9D01_SendCmd(0xf3);		// SET_GAMMA4 (F3h)
    GC9D01_SendData(0x4d);
    GC9D01_SendData(0x94);
    GC9D01_SendData(0x4f);
    GC9D01_SendData(0x33);
    GC9D01_SendData(0x34);
    GC9D01_SendData(0xCf);
	
	// Scan direction
	GC9D01_SendCmd(GC9D01_DisplayFunctionControl);
	GC9D01_SendData(0x00);
	GC9D01_SendData(0x10); 
		
	// def rotation
    GC9D01_SendCmd(GC9D01_MADCTL);
    GC9D01_SendData(GC9D01_ROTATION);
	
	// ColorMode
	GC9D01_SendCmd(GC9D01_COLMOD);
	GC9D01_SendData(ColorMode_MCU_16bit & 0x77);
	
    GC9D01_SendCmd(0x11);	// Sleep Out Mode (11h) and delay(200)
		
    vTaskDelay(100);
		
    GC9D01_SendCmd(0x29);	// Display ON (29h) and delay(20)
    GC9D01_SendCmd(0x2C);	// Memory Write (2Ch) D=0
	
	//GC9D01_ClearFrameBuffer();
}
//==============================================================================

__inline static void GC9D01_SendCmd(uint8_t Cmd){	
    gpio_bits_reset(DC_GPIO_PORT, DC_PIN);
    GSPI_Transmit(&Cmd, 1);
    gpio_bits_set(DC_GPIO_PORT, DC_PIN);
}

__inline static void GC9D01_SendData(uint8_t Data ){
    gpio_bits_set(DC_GPIO_PORT, DC_PIN);
    GSPI_Transmit(&Data, 1);
}

__inline static void GC9D01_SendDataMASS(uint8_t* buff, size_t buff_size){
    gpio_bits_set(DC_GPIO_PORT, DC_PIN);
    GSPI_Transmit(buff, buff_size);
}

void GC9D01_HardReset(void){
	gpio_bits_reset(SRST_GPIO_PORT, SRST_PIN);
	vTaskDelay(20);	
	gpio_bits_set(SRST_GPIO_PORT, SRST_PIN);
}



void GC9D01_SleepModeEnter( void ){
	GC9D01_SendCmd(GC9D01_SLPIN);
	vTaskDelay(250);
}

void GC9D01_SleepModeExit( void ){
	GC9D01_SendCmd(GC9D01_SLPOUT);
	vTaskDelay(250);
}

void GC9D01_DisplayPower(uint8_t On){
  if (On){
    GC9D01_SendCmd(GC9D01_DISPON);
  }
  else{
    GC9D01_SendCmd(GC9D01_DISPOFF);
  }
}

void GC9D01_InversionMode(uint8_t Mode){
  if (Mode){
    GC9D01_SendCmd(GC9D01_INVON);
  }
  else{
    GC9D01_SendCmd(GC9D01_INVOFF);
  }
}

void GC9D01_RamWrite(uint8_t *pBuff, uint32_t Len){
  GC9D01_SendDataMASS(pBuff, Len);
}

void GC9D01_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1){	
	
	GC9D01_ColumnSet(x0, x1);
	GC9D01_RowSet(y0, y1);
	
	// write to RAM
	GC9D01_SendCmd(GC9D01_RAMWR);
}

//==============================================================================
// Процедура установки начального и конечного адресов колонок
//==============================================================================
static void GC9D01_ColumnSet(uint16_t ColumnStart, uint16_t ColumnEnd){
	
  if (ColumnStart > ColumnEnd){
    return;
  }
  
  if (ColumnEnd > GC9D01_Width){
    return;
  }
	
  ColumnStart += GC9D01_X_Start;
  ColumnEnd += GC9D01_X_Start;
  
  GC9D01_SendCmd(GC9D01_CASET);
  GC9D01_SendData(ColumnStart >> 8);  
  GC9D01_SendData(ColumnStart & 0xFF);  
  GC9D01_SendData(ColumnEnd >> 8);  
  GC9D01_SendData(ColumnEnd & 0xFF);  
  
}
//==============================================================================


//==============================================================================
// Процедура установки начального и конечного адресов строк
//==============================================================================
static void GC9D01_RowSet(uint16_t RowStart, uint16_t RowEnd){
	
  if (RowStart > RowEnd){
    return;
  }
  
  if (RowEnd > GC9D01_Height){
    return;
  }
  
  RowStart += GC9D01_Y_Start;
  RowEnd += GC9D01_Y_Start;
 
  GC9D01_SendCmd(GC9D01_RASET);
  GC9D01_SendData(RowStart >> 8);  
  GC9D01_SendData(RowStart & 0xFF);  
  GC9D01_SendData(RowEnd >> 8);  
  GC9D01_SendData(RowEnd & 0xFF);  

}
//==============================================================================


//==============================================================================
// Процедура управления подсветкой (ШИМ)
//==============================================================================
void GC9D01_SetBL(uint8_t Value){
	
//  if (Value > 100)
//    Value = 100;

//	tmr2_PWM_set(ST77xx_PWM_TMR2_Chan, Value);

}
//==============================================================================


//==============================================================================
// Процедура ротации ( положение ) дисплея
//==============================================================================
// па умолчанию 1 режим ( всего 1, 2, 3, 4 )
void GC9D01_rotation( uint8_t rotation ){
	GC9D01_SendCmd(GC9D01_MADCTL);

	// длайвер расчитан на экран 160 х 160 (  максимальный размер )
	// для подгона под любой другой нужно отнимать разницу пикселей

	  switch (rotation) {
		
		case 1:
			//== 0.99" 40 x 160 GC9D01 =================================================
			#ifdef GC9D01_IS_40X160
				GC9D01_SendData( GC9D01_MADCTL_MV | GC9D01_MADCTL_ML | GC9D01_MADCTL_RGB );
				GC9D01_Width = 160;
				GC9D01_Height = 40;
				GC9D01_X_Start = -60;
				GC9D01_Y_Start = 60;
				//GC9D01_FillScreen(0);
			#endif
			//==========================================================================

		 break;
		
		case 2:
			//== 0.99" 40 x 160 GC9D01 =================================================
			#ifdef GC9D01_IS_40X160
				GC9D01_SendData( GC9D01_MADCTL_RGB );
				GC9D01_Width = 40;
				GC9D01_Height = 160;
				GC9D01_X_Start = 0;
				GC9D01_Y_Start = 0;
				//GC9D01_FillScreen(0);
			#endif
			//==========================================================================
	
		 break;
		
	   case 3:
		   //== 0.99" 40 x 160 GC9D01 =================================================
			#ifdef GC9D01_IS_40X160
				GC9D01_SendData( GC9D01_MADCTL_MV | GC9D01_MADCTL_MX | GC9D01_MADCTL_MY | GC9D01_MADCTL_ML | GC9D01_MADCTL_RGB );
				GC9D01_Width = 160;
				GC9D01_Height = 40;
				GC9D01_X_Start = -60;
				GC9D01_Y_Start = 60;
				//GC9D01_FillScreen(0);
			#endif
			//==========================================================================
			
		 break;
	   
	   case 4:
		   //== 0.99" 40 x 160 GC9D01 =================================================
			#ifdef GC9D01_IS_40X160
				GC9D01_SendData(GC9D01_MADCTL_MX | GC9D01_MADCTL_MY | GC9D01_MADCTL_RGB);
				GC9D01_Width = 40;
				GC9D01_Height = 160;
				GC9D01_X_Start = 0;
				GC9D01_Y_Start = 0;
				//GC9D01_FillScreen(0);
			#endif
			//==========================================================================
	   
		 break;
	   
	   default:
		 break;
	  }
}
//==============================================================================


//#########################################################################################################################
//#########################################################################################################################


/************************ (C) COPYRIGHT GKP *****END OF FILE****/
