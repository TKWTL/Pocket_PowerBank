/*
 * sd3078_algo.c - SD3078 application policy/state machines
 */
#include "sd3078_algo.h"
#include "framework/nvm_store.h"
#include "framework/pm_api.h"
#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    sd3078_time_t set_time;
    uint8_t time_set_mask;

    sd3078_backup_charge_mode_t backup_mode;
    uint8_t backup_charging;
    volatile uint8_t charge_apply_pending;
} sd3078_algo_state_t;

static sd3078_algo_state_t s_algo;

/************************* SD3078 后备电池充电算法开始 **************************/
/* MS621FE支持 Off / On / Auto。所有模式都受电池过压与温度硬保护；
 * Auto使用2.95V启充、3.10V停充迟滞，避免阈值附近反复开关。
 * 充电动作统一由load_task调用算法调度后执行，不从UI直接发I2C。 */
static i2c_status_type sd3078_algo_apply_charge(uint8_t enable)
{
    i2c_status_type st;
    i2c_status_type lock_st;

    enable = enable ? 1U : 0U;
    if (enable == s_algo.backup_charging) {
        return I2C_OK;
    }

    st = SD3078_Unlock();
    if (st != I2C_OK) return st;

    st = SD3078_ChargeSet(enable, SD3078_ALGO_CHARGE_RES_SEL);
    lock_st = SD3078_Lock();
    if (st == I2C_OK) st = lock_st;

    if (st == I2C_OK) {
        s_algo.backup_charging = enable;
    }
    return st;
}

static uint8_t sd3078_algo_charge_desired(void)
{
    uint16_t batt_mv = SD3078_ReadBatt();
    int8_t temp_c = SD3078_ReadTemp();

    /* Hard guards are active even in user "On" mode. */
    if (SD3078_IsBattHigh() != 0U) return 0U;
    if (temp_c < SD3078_ALGO_CHARGE_TEMP_MIN_C ||
        temp_c > SD3078_ALGO_CHARGE_TEMP_MAX_C) {
        return 0U;
    }

    switch (s_algo.backup_mode) {
    case SD3078_BACKUP_CHARGE_OFF:
        return 0U;

    case SD3078_BACKUP_CHARGE_ON:
        return 1U;

    case SD3078_BACKUP_CHARGE_AUTO:
    default:
        /* 0 means SD3078 has not produced a valid battery measurement yet.
         * Keep the current state rather than making a decision from an invalid sample. */
        if (batt_mv == 0U) return s_algo.backup_charging;

        if (s_algo.backup_charging) {
            return (batt_mv >= SD3078_ALGO_AUTO_CHARGE_OFF_MV) ? 0U : 1U;
        }
        return (batt_mv <= SD3078_ALGO_AUTO_CHARGE_ON_MV) ? 1U : 0U;
    }
}

/************************* SD3078 后备电池充电算法结束 **************************/

/************************** SD3078 RTC时间提交算法开始 **************************/
/* UI只提交被修改字段的RAM mask。真正写入前重新读取当前RTC，
 * 只覆盖用户改过的字段，再按SD3078要求整组写回00H~06H；
 * I2C失败时把mask恢复，等待下次重试，避免丢失用户修改。 */
static i2c_status_type sd3078_algo_process_time(void)
{
    sd3078_time_t req;
    sd3078_time_t t;
    uint8_t mask;
    i2c_status_type st;
    i2c_status_type lock_st;

    taskENTER_CRITICAL();
    mask = s_algo.time_set_mask;
    req = s_algo.set_time;
    s_algo.time_set_mask &= (uint8_t)~mask;
    taskEXIT_CRITICAL();

    if (mask == 0U) return I2C_OK;

    /* Read the RTC at commit time, then replace only fields edited by the user.
     * The driver still performs the mandatory full 00H~06H seven-byte write. */
    st = SD3078_TimeLoad();
    if (st != I2C_OK) {
        taskENTER_CRITICAL();
        s_algo.time_set_mask |= mask;
        taskEXIT_CRITICAL();
        return st;
    }

    /* Driver mirror is private to sd3078.c; rebuild the current decimal time only
     * through its public read-only API after TimeLoad() has atomically refreshed it. */
    SD3078_SEC(&t)   = SD3078_ReadSec();
    SD3078_MIN(&t)   = SD3078_ReadMin();
    SD3078_HOUR(&t)  = SD3078_ReadHour();
    SD3078_WEEK(&t)  = SD3078_ReadWeek();
    SD3078_DAY(&t)   = SD3078_ReadDay();
    SD3078_MONTH(&t) = SD3078_ReadMonth();
    SD3078_YEAR(&t)  = SD3078_ReadYear();

    if (mask & (1U << 0)) SD3078_SEC(&t)   = SD3078_SEC(&req);
    if (mask & (1U << 1)) SD3078_MIN(&t)   = SD3078_MIN(&req);
    if (mask & (1U << 2)) SD3078_HOUR(&t)  = SD3078_HOUR(&req);
    if (mask & (1U << 3)) SD3078_DAY(&t)   = SD3078_DAY(&req);
    if (mask & (1U << 4)) SD3078_MONTH(&t) = SD3078_MONTH(&req);
    if (mask & (1U << 5)) SD3078_YEAR(&t)  = SD3078_YEAR(&req);

    st = SD3078_Unlock();
    if (st == I2C_OK) {
        st = SD3078_TimeSetDec(&t);
        lock_st = SD3078_Lock();
        if (st == I2C_OK) st = lock_st;
    }

    if (st != I2C_OK) {
        taskENTER_CRITICAL();
        s_algo.time_set_mask |= mask;
        taskEXIT_CRITICAL();
        return st;
    }

    /* 周期 TimeLoad 最迟 500ms 后更新 UI 镜像；下一次编辑提交仍会先实时读硬件。 */
    return I2C_OK;
}

void SD3078_AlgoInit(void)
{
    taskENTER_CRITICAL();
    /* 不清 time_set_mask：若 SD3078 因一次 I2C 错误被重新初始化，UI 已提交但尚未
     * 成功落到 RTC 的时间修改必须继续保留。静态对象在真正上电时本来就是 0。 */
    s_algo.backup_charging = 0U; /* SD3078_Init() always starts with charger disabled. */
    if (nvm_is_valid() && nvm_get_backup_charge_mode() <= SD3078_BACKUP_CHARGE_AUTO) {
        s_algo.backup_mode = (sd3078_backup_charge_mode_t)nvm_get_backup_charge_mode();
    } else {
        s_algo.backup_mode = SD3078_BACKUP_CHARGE_AUTO;
    }
    s_algo.charge_apply_pending = 1U;
    taskEXIT_CRITICAL();
}

i2c_status_type SD3078_AlgoRequestTimeSet(uint8_t year, uint8_t month, uint8_t day,
                                         uint8_t hour, uint8_t min, uint8_t sec)
{
    if (year > 99U || month < 1U || month > 12U || day < 1U || day > 31U ||
        hour > 23U || min > 59U || sec > 59U) {
        return I2C_ERR_STEP_1;
    }

    taskENTER_CRITICAL();
    SD3078_YEAR(&s_algo.set_time)  = year;
    SD3078_MONTH(&s_algo.set_time) = month;
    SD3078_DAY(&s_algo.set_time)   = day;
    SD3078_HOUR(&s_algo.set_time)  = hour;
    SD3078_MIN(&s_algo.set_time)   = min;
    SD3078_SEC(&s_algo.set_time)   = sec;
    s_algo.time_set_mask |= 0x3FU;
    taskEXIT_CRITICAL();
    return I2C_OK;
}

i2c_status_type SD3078_AlgoRequestTimeFieldSet(sd3078_time_field_t field, uint8_t value)
{
    uint8_t bit;

    switch (field) {
    case SD3078_TIME_FIELD_SEC:
        if (value > 59U) return I2C_ERR_STEP_1;
        bit = 0U; break;
    case SD3078_TIME_FIELD_MIN:
        if (value > 59U) return I2C_ERR_STEP_1;
        bit = 1U; break;
    case SD3078_TIME_FIELD_HOUR:
        if (value > 23U) return I2C_ERR_STEP_1;
        bit = 2U; break;
    case SD3078_TIME_FIELD_DAY:
        if (value < 1U || value > 31U) return I2C_ERR_STEP_1;
        bit = 3U; break;
    case SD3078_TIME_FIELD_MONTH:
        if (value < 1U || value > 12U) return I2C_ERR_STEP_1;
        bit = 4U; break;
    case SD3078_TIME_FIELD_YEAR:
        if (value > 99U) return I2C_ERR_STEP_1;
        bit = 5U; break;
    default:
        return I2C_ERR_STEP_1;
    }

    taskENTER_CRITICAL();
    switch (field) {
    case SD3078_TIME_FIELD_SEC:   SD3078_SEC(&s_algo.set_time) = value; break;
    case SD3078_TIME_FIELD_MIN:   SD3078_MIN(&s_algo.set_time) = value; break;
    case SD3078_TIME_FIELD_HOUR:  SD3078_HOUR(&s_algo.set_time) = value; break;
    case SD3078_TIME_FIELD_DAY:   SD3078_DAY(&s_algo.set_time) = value; break;
    case SD3078_TIME_FIELD_MONTH: SD3078_MONTH(&s_algo.set_time) = value; break;
    case SD3078_TIME_FIELD_YEAR:  SD3078_YEAR(&s_algo.set_time) = value; break;
    default: break;
    }
    s_algo.time_set_mask |= (uint8_t)(1U << bit);
    taskEXIT_CRITICAL();
    return I2C_OK;
}

/************************** SD3078 RTC时间提交算法结束 **************************/

/************************** SD3078 分层寄存器Load算法开始 ************************/
/* RTC时间需要界面较快刷新，保持500ms；芯片温度与后备电池电压的硬件更新本身
 * 是分钟级，因此降到60s。任务层只负责调用不同周期入口，不再直接知道寄存器Load顺序。 */
i2c_status_type SD3078_AlgoLoadFast(void)
{
    return SD3078_TimeLoad();
}

i2c_status_type SD3078_AlgoLoadSlow(void)
{
    i2c_status_type st;

    st = SD3078_TempLoad();
    if (st != I2C_OK) return st;

    st = SD3078_BattLoad();
    if (st != I2C_OK) return st;

    return sd3078_algo_apply_charge(sd3078_algo_charge_desired());
}

i2c_status_type SD3078_AlgoLoadAll(void)
{
    i2c_status_type st;

    st = SD3078_TimeLoad();
    if (st != I2C_OK) return st;
    st = SD3078_TempLoad();
    if (st != I2C_OK) return st;
    return SD3078_BattLoad();
}
/************************** SD3078 分层寄存器Load算法结束 ************************/

/************************** SD3078 算法接口与调度开始 ***************************/
/* 模式修改只更新RAM并标记pending，同时写入NVM；Fast路径处理立即请求，
 * Slow路径约每分钟依据最新VBAT/TEMP复核Auto策略。 */
i2c_status_type SD3078_AlgoSetBackupChargeMode(sd3078_backup_charge_mode_t mode)
{
    if (mode > SD3078_BACKUP_CHARGE_AUTO) return I2C_ERR_STEP_1;

    taskENTER_CRITICAL();
    s_algo.backup_mode = mode;
    s_algo.charge_apply_pending = 1U;
    taskEXIT_CRITICAL();

    nvm_set_backup_charge_mode((uint8_t)mode);
    return I2C_OK;
}

sd3078_backup_charge_mode_t SD3078_AlgoGetBackupChargeMode(void)
{
    return s_algo.backup_mode;
}

uint8_t SD3078_AlgoIsBackupCharging(void)
{
    return s_algo.backup_charging;
}

i2c_status_type SD3078_AlgoProcessFast(void)
{
    i2c_status_type st;
    uint8_t do_charge;

    st = sd3078_algo_process_time();
    if (st != I2C_OK) return st;

    taskENTER_CRITICAL();
    do_charge = s_algo.charge_apply_pending;
    s_algo.charge_apply_pending = 0U;
    taskEXIT_CRITICAL();

    if (do_charge != 0U) {
        st = sd3078_algo_apply_charge(sd3078_algo_charge_desired());
        if (st != I2C_OK) {
            s_algo.charge_apply_pending = 1U;
            return st;
        }
    }
    return I2C_OK;
}

/***************************** SD3078 低功耗策略 *****************************/
void SD3078_AlgoPmSuspend(void *ctx)
{
    i2c_status_type st;
    (void)ctx;

    if (pm_api_transport_mode_requested() == 0U) return;

    /* 运输模式：RTC继续走时，但关闭所有本项目会用到的可选耗电功能。
     * Standby唤醒会复位MCU，故无需恢复；正常DeepSleep不走这里的强制关闭。 */
    st = SD3078_Unlock();
    if (st == I2C_OK) {
        (void)SD3078_ChargeSet(0U, SD3078_ALGO_CHARGE_RES_SEL);
        (void)SD3078_F32KSet(0U);
        (void)SD3078_CountdownEnable(0U);
        (void)SD3078_ByteModify(SD3078_CTRG_CTR2,
                                SD3078_CTR2_INTFE | SD3078_CTR2_INTAE |
                                SD3078_CTR2_INTDE | SD3078_CTR2_FOBAT, 0U);
        (void)SD3078_ByteModify(SD3078_CTRG_CTR4,
                                SD3078_CTR4_INTTHE | SD3078_CTR4_INTTLE |
                                SD3078_CTR4_INTBHE | SD3078_CTR4_INTBLE, 0U);
        (void)SD3078_Lock();
    }
    s_algo.backup_charging = 0U;
}
/*************************** SD3078 低功耗策略结束 ***************************/

/************************** SD3078 算法接口与调度结束 ***************************/
