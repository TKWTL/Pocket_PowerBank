/*
 * sd3078_algo.h - SD3078 application policy/state machines
 *
 * Driver(sd3078.c) only performs register access.  This layer owns:
 *   - RTC edit request merging and deferred full-7-byte commit
 *   - MS621FE backup-cell Off/On/Auto charging policy
 *
 * It never creates a task; load_task is the single executor.
 */
#ifndef SD3078_ALGO_H
#define SD3078_ALGO_H

#include <stdint.h>
#include "sd3078.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SD3078_TIME_FIELD_SEC = 0,
    SD3078_TIME_FIELD_MIN,
    SD3078_TIME_FIELD_HOUR,
    SD3078_TIME_FIELD_DAY,
    SD3078_TIME_FIELD_MONTH,
    SD3078_TIME_FIELD_YEAR
} sd3078_time_field_t;

typedef enum {
    SD3078_BACKUP_CHARGE_OFF = 0,
    SD3078_BACKUP_CHARGE_ON,
    SD3078_BACKUP_CHARGE_AUTO
} sd3078_backup_charge_mode_t;

/* MS621FE + SD3078 policy
 * SII: MS621FE standard charge voltage 3.1V, allowed charge voltage 2.8~3.3V.
 * SD3078 charger source is 3.3V with 2k/5k/10k selectable series resistance.
 * 5k is deliberately gentle (SII minimum recommended resistor at 3.3V is 620 ohm).
 * Auto mode keeps the cell near 3.0V instead of permanently floating at 3.3V. */
#define SD3078_ALGO_CHARGE_RES_SEL       SD3078_CHARGE_RES_5K
#define SD3078_ALGO_AUTO_CHARGE_ON_MV    2950U
#define SD3078_ALGO_AUTO_CHARGE_OFF_MV   3100U
#define SD3078_ALGO_CHARGE_TEMP_MIN_C    (-20)
#define SD3078_ALGO_CHARGE_TEMP_MAX_C    60

void SD3078_AlgoInit(void);

/* UI thread: RAM-only request functions; no I2C. */
i2c_status_type SD3078_AlgoRequestTimeSet(uint8_t year, uint8_t month, uint8_t day,
                                         uint8_t hour, uint8_t min, uint8_t sec);
i2c_status_type SD3078_AlgoRequestTimeFieldSet(sd3078_time_field_t field, uint8_t value);
i2c_status_type SD3078_AlgoSetBackupChargeMode(sd3078_backup_charge_mode_t mode);

sd3078_backup_charge_mode_t SD3078_AlgoGetBackupChargeMode(void);
uint8_t SD3078_AlgoIsBackupCharging(void);

/* load_task only:
 * ProcessFast: 10ms调用；仅有时间/充电请求时才产生I2C。
 * LoadFast: 500ms调用；只刷新RTC时间。
 * LoadSlow: 60s调用；刷新温度+VBAT，并基于fresh镜像复核Auto充电。
 * LoadAll: 唤醒/初始化预取，强制刷新Time+Temp+VBAT。 */
i2c_status_type SD3078_AlgoProcessFast(void);
i2c_status_type SD3078_AlgoLoadFast(void);
i2c_status_type SD3078_AlgoLoadSlow(void);
i2c_status_type SD3078_AlgoLoadAll(void);

#ifdef __cplusplus
}
#endif

#endif /* SD3078_ALGO_H */
