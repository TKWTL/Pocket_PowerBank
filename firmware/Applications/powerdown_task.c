#include "applications.h"

#include "framework/pm_controller.h"
#include "framework/pm_device.h"
#include "freertos_app.h"   /* mutex_i2c_handle / mutex_gspi_handle：睡眠总线门控 */

static pm_controller_t s_pm_ctrl;
static uint8_t s_pm_ready;
static volatile uint8_t s_pm_refresh_req;
static volatile uint8_t s_pm_block_mask;

/* 数据刷新请求（EXINT 置位，load_task 消费）与唤醒数据就绪门闩 */
static volatile uint8_t s_data_refresh_req;
static volatile uint8_t s_wake_data_ready = 1;   /* 默认就绪（RUN 态无需预取） */

/* 睡眠总线门控：1=睡眠准备/深睡中，load 类任务停止发起总线读写 */
static volatile uint8_t s_pm_sleep_gate;

/** 唤醒后按键不响应静默计数 (~100ms) */
static uint8_t s_wk_btn;

void pm_api_refresh_idle(void)
{
    s_pm_refresh_req = 1;
}

void pm_api_force_sleep(void)
{
    /* 手动休眠：立即进入休眠流程。
     * 关键点：先丢弃 indev/按键暂存的刷新请求，否则状态机会被拉回 RUN。 */
    taskENTER_CRITICAL();
    s_pm_refresh_req = 0;                    /* 丢弃暂存刷新请求 */
    pm_sleep_timer_force_expire();           /* 定时器立即到期 */
    pm_controller_mark_ui_active(&s_pm_ctrl, 0); /* 标记 UI 不活跃，状态机随即离开 RUN */
    taskEXIT_CRITICAL();
}

void pm_api_set_sleep_timeout(int timeout_sec)
{
    /* 0 = 关闭自动休眠（停止倒计时，永久不休眠）；>0 = 设定超时秒数 */
    if (timeout_sec <= 0) {
        pm_sleep_timer_disable();
    } else {
        pm_sleep_timer_set((uint32_t)timeout_sec * 1000UL);
    }
}

void pm_api_set_sleep_block(uint8_t mask, uint8_t enable)
{
    if (enable != 0) {
        s_pm_block_mask |= mask;
    } else {
        s_pm_block_mask &= (uint8_t)(~mask);
    }
}

uint8_t pm_api_ui_should_block(void)
{
    /* UI 阻塞：非 RUN 状态，或唤醒后数据尚未预取就绪（先查后显，避免旧值闪现） */
    return pm_controller_is_ui_blocked(&s_pm_ctrl) || (s_wake_data_ready == 0);
}

uint8_t pm_api_is_sleeping(void)
{
    /* state != RUN 即认为系统处于休眠中 */
    return pm_controller_is_ui_blocked(&s_pm_ctrl);
}

static volatile uint8_t s_unstable_wake;

void pm_api_set_unstable_wake(uint8_t unstable)
{
    s_unstable_wake = unstable;
}

uint8_t pm_api_is_unstable_wake(void)
{
    return s_unstable_wake;
}

uint8_t pm_api_get_block_mask(void)
{
    return s_pm_block_mask;
}

void pm_api_request_data_refresh(void)
{
    s_data_refresh_req = 1;
}

uint8_t pm_api_data_refresh_pending(void)
{
    return s_data_refresh_req;
}

void pm_api_data_refresh_done(void)
{
    s_data_refresh_req = 0;
    s_wake_data_ready = 1;   /* 数据就绪（唤醒使能）：UI 可解除阻塞亮屏 */
}

void pm_api_mark_wake_data_stale(void)
{
    s_wake_data_ready = 0;
}

uint8_t pm_api_wake_data_ready(void)
{
    return s_wake_data_ready;
}

void pm_api_sleep_gate_set(uint8_t on)
{
    s_pm_sleep_gate = on;
}

uint8_t pm_api_sleep_gate_get(void)
{
    return s_pm_sleep_gate;
}

/* 所有总线互斥锁空闲？（非阻塞获取成功即空闲，获取后立即释放，不改变状态） */
uint8_t pm_api_bus_locks_idle(void)
{
    if (mutex_i2c_handle != NULL && xSemaphoreTake(mutex_i2c_handle, 0) != pdTRUE) {
        return 0;   /* I2C 忙：有任务正在读写 */
    }
    if (mutex_i2c_handle != NULL) {
        xSemaphoreGive(mutex_i2c_handle);
    }
    if (mutex_gspi_handle != NULL && xSemaphoreTake(mutex_gspi_handle, 0) != pdTRUE) {
        return 0;   /* SPI 显示忙 */
    }
    if (mutex_gspi_handle != NULL) {
        xSemaphoreGive(mutex_gspi_handle);
    }
    return 1;
}

static void pm_enter_deep_sleep(void)
{
    /* ── 休眠前排空 ── */

    /* ① 睡眠总线门控：置位 → load/SW6306 任务停止发起新总线读写（下一轮让出） */
    pm_api_sleep_gate_set(1);

    /* ② 等待所有总线互斥锁空闲（已发起的 I2C/SPI 读写完成）：
     *    调度器仍在运行，load 任务见门控后停止新事务，当前事务完成后释放锁；
     *    比固定忙等更可靠（不依赖时间猜测）。最多等 1s。 */
    {
        uint16_t wait = 0;
        while (pm_api_bus_locks_idle() == 0 && wait < 1000) {
            vTaskDelay(pdMS_TO_TICKS(5));
            wait += 5;
        }
    }

    /* ③ 挂起 FreeRTOS 调度器：防止休眠过程中其他任务启动新的事务 */
    vTaskSuspendAll();

    /* ④ 忙等 10ms：确认无残留总线中断待处理（调度器已挂起，无新事务） */
    wk_delay_ms(10);

    /* ⑤ 停止 wk_tick（TMR7）时基：防止休眠过程中产生意外中断 */
    tmr_interrupt_enable(TMR7, TMR_OVF_INT, FALSE);
    tmr_counter_enable(TMR7, FALSE);
    NVIC_ClearPendingIRQ(TMR7_GLOBAL_IRQn);
    tmr_flag_clear(TMR7, TMR_OVF_FLAG);

    /* ⑥ 禁止 SysTick 中断并清 pending：防止 WFI 被时间片中断立即唤醒 */
    SysTick->CTRL &= ~(uint32_t)SysTick_CTRL_TICKINT_Msk;
    SysTick->VAL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* ⑦ 关闭未用外设时钟：DeepSleep 期间 CPU 停，外设时钟仅增加静态功耗。
       保留 GPIOA/B/F（EXINT 唤醒需要）与 PWC/SCFG，唤醒后再恢复。 */
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_CRC_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, FALSE);
    crm_periph_clock_enable(CRM_I2C1_PERIPH_CLOCK, FALSE);

    /* ⑧ 确保 SW6306 INT(EXINT8) 为 DeepSleep 唤醒源：使能中断+清 pending+清标志 */
    nvic_irq_enable(EXINT9_5_IRQn, 5, 0);
    NVIC_ClearPendingIRQ(EXINT9_5_IRQn);
    exint_flag_clear(EXINT_LINE_8);

    /* ── 进入 DeepSleep ── */
    pwc_voltage_regulate_set(PWC_REGULATOR_LOW_POWER);
    pwc_deep_sleep_mode_enter(PWC_DEEP_SLEEP_ENTER_WFI);

    /* ──── 唤醒后 ──── */

    /* ★ 清除 SLEEPDEEP 位：AT32 库函数未在唤醒后清除此位，
       否则下次空闲钩子执行 __WFI() 会再次进入 DeepSleep */
    SCB->SCR &= ~(uint32_t)SCB_SCR_SLEEPDEEP_Msk;

    /* ── 恢复 LDO 电压至 1.3V ──
     *
     * AT32F423 数据手册要求：
     *   LDO 1.0V → AHB ≤ 64 MHz
     *   LDO 1.2V → AHB ≤ 120 MHz
     *   LDO 1.3V → AHB ≤ 150 MHz
     *
     * DeepSleep 唤醒后 LDOOVSEL 回到复位值 0x2 (1.2V)，
     * 必须先在安全频率下切到 1.3V，再上 150MHz PLL，
     * 否则芯片工作可能不稳定甚至硬 fault。 */
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);
    while (crm_flag_get(CRM_HICK_STABLE_FLAG) != SET);
    crm_sysclk_switch(CRM_SCLK_HICK);
    while (crm_sysclk_switch_status_get() != CRM_SCLK_HICK);
    pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);            /* LDO → 1.3V */
    flash_psr_set(FLASH_WAIT_CYCLE_4);                          /* 150MHz 需 4 个等待周期 */

    /* ① 恢复系统时钟：DeepSleep 期间 PLL 及总线分频被硬件复位 */
    crm_pll_config(CRM_PLL_SOURCE_HICK, 75, 1, CRM_PLL_FR_2);   /* 150MHz: HICK×75/1/2 */
    crm_ahb_div_set(CRM_AHB_DIV_1);                             /* AHB = 系统时钟 /1 */
    crm_apb2_div_set(CRM_APB2_DIV_2);                           /* APB2 = AHB /2 */
    crm_apb1_div_set(CRM_APB1_DIV_2);                           /* APB1 = AHB /2 */
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);         /* 使能 PLL */
    while (crm_flag_get(CRM_PLL_STABLE_FLAG) != SET);           /* 等待 PLL 锁定 */
    crm_sysclk_switch(CRM_SCLK_PLL);                             /* 切回 PLL 作为系统时钟 */
    while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL);     /* 等待切换完成 */
    system_core_clock_update();                                   /* 更新全局时钟变量 */


    /* ③ 恢复 FreeRTOS 调度（先于 SysTick，确保调度器恢复时 tick 计数干净） */
    xTaskResumeAll();

    /* ④ 恢复 SysTick 中断 */
    SysTick->CTRL |= (uint32_t)SysTick_CTRL_TICKINT_Msk;

    /* ⑤ 恢复 wk_tick（TMR7）时基 */
    tmr_counter_enable(TMR7, FALSE);
    tmr_flag_clear(TMR7, TMR_OVF_FLAG);
    tmr_counter_enable(TMR7, TRUE);
    tmr_interrupt_enable(TMR7, TMR_OVF_INT, TRUE);

    /* ⑦ 恢复外设时钟（与休眠前关闭的逐一对应） */
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_CRC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_I2C1_PERIPH_CLOCK, TRUE);

    /* 唤醒数据预取门：置数据未就绪 → ui_task 保持阻塞，
     * 直到 load_task 检测到刷新请求完成一轮完整读取（先查后显） */
    pm_api_mark_wake_data_stale();

    /* ⑥ 设置唤醒静默期：刚唤醒时不响应按键，避免误操作 */
    s_wk_btn = 2;   /* ~100ms (2×50ms) */
    pm_api_set_unstable_wake(1);

    /* 解除睡眠门控：load 类任务恢复总线读写 */
    pm_api_sleep_gate_set(0);
}

static void pm_enter_standby(void)
{
    pwc_wakeup_pin_enable(PWC_WAKEUP_PIN_1, TRUE);
    pwc_flag_clear(PWC_WAKEUP_FLAG | PWC_STANDBY_FLAG);
    pwc_standby_mode_enter();
}

static void pm_task_init_once(void)
{
    if (s_pm_ready != 0) {
        return;
    }

    pm_controller_init(&s_pm_ctrl, 30000);
    pm_controller_mark_ui_active(&s_pm_ctrl, 1);
    pm_device_register_all();

    s_pm_ready = 1;
}

void powerdown_task_func(void *pvParameters)
{
    pm_power_state_t state;
    uint8_t sleep_blocked = 0;

    (void)pvParameters;
    pm_task_init_once();

    while (1) {
        if (s_pm_refresh_req != 0) {
            pm_controller_refresh_idle(&s_pm_ctrl);
            s_pm_refresh_req = 0;
        }

        if (s_pm_block_mask != 0) {
            if (sleep_blocked == 0) {
                pm_controller_pause_idle(&s_pm_ctrl);
                sleep_blocked = 1;
            }
            pm_controller_notify_wake(&s_pm_ctrl);
        } else if (sleep_blocked != 0) {
            pm_controller_resume_idle(&s_pm_ctrl);
            sleep_blocked = 0;
        }

        /* 空闲超时：清除 UI 活跃标志，允许状态机离开 RUN。
         * expired() 已内含短路：停止倒计时→0（不休眠）、立刻休眠→1。 */
        if (s_pm_refresh_req == 0 && pm_sleep_timer_expired() != 0) {
            pm_controller_mark_ui_active(&s_pm_ctrl, 0);
        }

        /* 唤醒静默期：递减计数器 */
        if (s_wk_btn > 0 && --s_wk_btn == 0) {
            pm_api_set_unstable_wake(0);  /* ~100ms 后恢复按键响应 */
        }

        state = pm_controller_step(&s_pm_ctrl);

        switch (state) {
        case PM_STATE_RUN:
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        case PM_STATE_UI_OFF:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        case PM_STATE_SLEEP_PREPARE:
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        case PM_STATE_DEEPSLEEP:
            pm_sleep_timer_clear_force();   /* 进入休眠分支后无条件清除立刻休眠标志 */
            pm_enter_deep_sleep();
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        case PM_STATE_SLEEP_PENDING:
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        case PM_STATE_STANDBY:
            pm_sleep_timer_clear_force();   /* 进入休眠分支后无条件清除立刻休眠标志 */
            pm_enter_standby();
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }
    }
}
