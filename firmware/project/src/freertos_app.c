/* add user code begin Header */
/**
  ******************************************************************************
  * File Name          : freertos_app.c
  * Description        : Code for freertos applications
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "freertos_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/* task handler */
TaskHandle_t ui_task_handle;
TaskHandle_t powerdown_task_handle;
TaskHandle_t button_task_handle;
TaskHandle_t SW6306_task_handle;
/* variables for task tcb and stack */
StackType_t my_task01_stack[768];
StackType_t my_task02_stack[128];
StackType_t my_task03_stack[64];
StackType_t my_task04_stack[128];
StaticTask_t my_task01_buffer;
StaticTask_t my_task02_buffer;
StaticTask_t my_task03_buffer;
StaticTask_t my_task04_buffer;

/* mutex handler */
SemaphoreHandle_t mutex_i2c_handle;
SemaphoreHandle_t mutex_gspi_handle;
SemaphoreHandle_t mutex_fspi_handle;
/* the variable used to hold the mutex's data structure */
StaticSemaphore_t my_mutex01_buffer;
StaticSemaphore_t my_mutex02_buffer;
StaticSemaphore_t my_mutex03_buffer;

/* Idle task control block and stack */
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

static StaticTask_t idle_task_tcb;
static StaticTask_t timer_task_tcb;

/* External Idle and Timer task static memory allocation functions */
extern void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );
extern void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer, StackType_t ** ppxTimerTaskStackBuffer, uint32_t * pulTimerTaskStackSize );

/*
  vApplicationGetIdleTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &idle_task_tcb;
  *ppxIdleTaskStackBuffer = &idle_task_stack[0];
  *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
}
/*
  vApplicationGetTimerTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer, StackType_t ** ppxTimerTaskStackBuffer, uint32_t * pulTimerTaskStackSize )
{
  *ppxTimerTaskTCBBuffer = &timer_task_tcb;
  *ppxTimerTaskStackBuffer = &timer_task_stack[0];
  *pulTimerTaskStackSize = (uint32_t)configTIMER_TASK_STACK_DEPTH;
}

void vApplicationIdleHook( void )
{
  /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
   task. It is essential that code added to this hook function never attempts
   to block in any way (for example, call xQueueReceive() with a block time
   specified, or call vTaskDelay()). If the application makes use of the
   vTaskDelete() API function (as this demo application does) then it is also
   important that vApplicationIdleHook() is permitted to return to its calling
   function, because it is the responsibility of the idle task to clean up
   memory allocated by the kernel to any task that has since been deleted. */
   
/* add user code begin vApplicationIdleHook */
    __WFI();
/* add user code end vApplicationIdleHook */
}

/* add user code begin 1 */

/* add user code end 1 */

/**
  * @brief  initializes all task.
  * @param  none
  * @retval none
  */
void freertos_task_create(void)
{
  /* create the ui_task task by static */
  ui_task_handle = xTaskCreateStatic(ui_task_func,
                                       "ui_task",
                                       768,
                                       NULL,
                                       14,
                                       my_task01_stack,
                                       &my_task01_buffer);

  /* create the powerdown_task task by static */
  powerdown_task_handle = xTaskCreateStatic(powerdown_task_func,
                                       "powerdown_task",
                                       128,
                                       NULL,
                                       15,
                                       my_task02_stack,
                                       &my_task02_buffer);

  /* create the button_task task by static */
  button_task_handle = xTaskCreateStatic(button_task_func,
                                       "button_task",
                                       64,
                                       NULL,
                                       15,
                                       my_task03_stack,
                                       &my_task03_buffer);

  /* create the SW6306_task task by static */
  SW6306_task_handle = xTaskCreateStatic(SW6306_task_func,
                                       "SW6306_task",
                                       128,
                                       NULL,
                                       14,
                                       my_task04_stack,
                                       &my_task04_buffer);
}

/**
  * @brief  initializes all semaphore.
  * @param  none
  * @retval none
  */
void freertos_semaphore_create(void)
{
  /* Create the mutex_i2c by static */
  mutex_i2c_handle = xSemaphoreCreateMutexStatic(&my_mutex01_buffer);

  /* Create the mutex_gspi by static */
  mutex_gspi_handle = xSemaphoreCreateMutexStatic(&my_mutex02_buffer);

  /* Create the mutex_fspi by static */
  mutex_fspi_handle = xSemaphoreCreateMutexStatic(&my_mutex03_buffer);
}

/**
  * @brief  freertos init and begin run.
  * @param  none
  * @retval none
  */
void wk_freertos_init(void)
{
  /* add user code begin freertos_init 0 */

  /* add user code end freertos_init 0 */

  /* enter critical */
  taskENTER_CRITICAL();

  freertos_semaphore_create();
  freertos_task_create();
	
  /* add user code begin freertos_init 1 */
    
  /* add user code end freertos_init 1 */

  /* exit critical */
  taskEXIT_CRITICAL();

  /* start scheduler */
  vTaskStartScheduler();
}

/**
  * @brief ui_task function.
  * @param  none
  * @retval none
  */
__WEAK void ui_task_func(void *pvParameters)
{
  /* add user code begin ui_task_func 0 */

  /* add user code end ui_task_func 0 */

  /* add user code begin ui_task_func 2 */

  /* add user code end ui_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin ui_task_func 1 */

     vTaskDelay(1);

  /* add user code end ui_task_func 1 */
  }
}

/**
  * @brief powerdown_task function.
  * @param  none
  * @retval none
  */
__WEAK void powerdown_task_func(void *pvParameters)
{
  /* add user code begin powerdown_task_func 0 */

  /* add user code end powerdown_task_func 0 */

  /* add user code begin powerdown_task_func 2 */

  /* add user code end powerdown_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin powerdown_task_func 1 */

    vTaskDelay(1);

  /* add user code end powerdown_task_func 1 */
  }
}

/**
  * @brief button_task function.
  * @param  none
  * @retval none
  */
__WEAK void button_task_func(void *pvParameters)
{
  /* add user code begin button_task_func 0 */

  /* add user code end button_task_func 0 */

  /* add user code begin button_task_func 2 */

  /* add user code end button_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin button_task_func 1 */

    vTaskDelay(1);

  /* add user code end button_task_func 1 */
  }
}

/**
  * @brief SW6306_task function.
  * @param  none
  * @retval none
  */
__WEAK void SW6306_task_func(void *pvParameters)
{
  /* add user code begin SW6306_task_func 0 */

  /* add user code end SW6306_task_func 0 */

  /* add user code begin SW6306_task_func 2 */

  /* add user code end SW6306_task_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin SW6306_task_func 1 */

    vTaskDelay(1);

  /* add user code end SW6306_task_func 1 */
  }
}

/* add user code begin 2 */

/* add user code end 2 */

