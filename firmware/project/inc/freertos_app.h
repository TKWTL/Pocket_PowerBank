/* add user code begin Header */
/**
  ******************************************************************************
  * File Name          : freertos_app.h
  * Description        : Code for freertos applications
  */
/* add user code end Header */
  
#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"
#include "event_groups.h"
#include "wk_system.h"

/* private includes -------------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* exported types -------------------------------------------------------------*/
/* add user code begin exported types */

/* add user code end exported types */

/* exported constants --------------------------------------------------------*/
/* add user code begin exported constants */

/* add user code end exported constants */

/* exported macro ------------------------------------------------------------*/
/* add user code begin exported macro */

/* add user code end exported macro */

/* task handler */
extern TaskHandle_t ui_task_handle;
extern TaskHandle_t powerdown_task_handle;
extern TaskHandle_t button_task_handle;
extern TaskHandle_t SW6306_task_handle;
/* variables for task tcb and stack */
extern StackType_t my_task01_stack[768];
extern StackType_t my_task02_stack[128];
extern StackType_t my_task03_stack[64];
extern StackType_t my_task04_stack[128];
extern StaticTask_t my_task01_buffer;
extern StaticTask_t my_task02_buffer;
extern StaticTask_t my_task03_buffer;
extern StaticTask_t my_task04_buffer;
/* declaration for task function */
void ui_task_func(void *pvParameters);
void powerdown_task_func(void *pvParameters);
void button_task_func(void *pvParameters);
void SW6306_task_func(void *pvParameters);

/* mutex handler */
extern SemaphoreHandle_t mutex_i2c_handle;
extern SemaphoreHandle_t mutex_gspi_handle;
extern SemaphoreHandle_t mutex_fspi_handle;
/* the variable used to hold the mutex's data structure */
extern StaticSemaphore_t my_mutex01_buffer;
extern StaticSemaphore_t my_mutex02_buffer;
extern StaticSemaphore_t my_mutex03_buffer;

/* add user code begin 0 */

/* add user code end 0 */

void freertos_task_create(void);
void freertos_semaphore_create(void);
void wk_freertos_init(void);

/* add user code begin 1 */

/* add user code end 1 */

#endif /* FREERTOS_APP_H */
