/*
 * nvm_store.h - SD3078 Backup RAM based minimal NVM
 *
 * Format: 68-byte payload + CRC16, no stored LENGTH/VERSION.
 * CRC seed is a layout constant: firmware rebuilds keep data valid; only a
 * serialized layout change bumps the seed and invalidates old data.
 */
#ifndef NVM_STORE_H
#define NVM_STORE_H

#include <stdint.h>
#include "bsp_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVM_PROTO_VERSION_MAJOR   0U
#define NVM_PROTO_VERSION_MINOR   3U
#define NVM_PROTO_VERSION_PATCH   0U
#define NVM_PROTO_VERSION_STRING  "0.3.0-prototype"

#define NVM_TOTAL_SIZE            70U
#define NVM_PAYLOAD_SIZE          68U
#define NVM_CRC_OFFSET            68U

#define NVM_DEFAULT_FACTORY_CAPACITY_WH       21.6f
#define NVM_DEFAULT_EQUIVALENT_CYCLES          0.0f
#define NVM_DEFAULT_IBAT_ZERO_RAW                 0
#define NVM_DEFAULT_IBAT_SLOPE_MA_PER_LSB   8.333333f /* 5mA/LSB @5mOhm scaled to 3mOhm */
#define NVM_DEFAULT_BACKUP_CHARGE_MODE          2U   /* 0=Off, 1=On, 2=Auto */

typedef struct {
    float factory_capacity_wh;       /* 出厂可用电池能量，SOH 分母 */
    float equivalent_cycles;         /* 等效完整循环次数 EFC */
    float ibat_slope_ma_per_lsb;     /* IBAT 原始码斜率 */
    int16_t ibat_zero_raw;           /* IBAT 原始码零点 */
    uint8_t backup_charge_mode;      /* SD3078 后备电池充电模式：0/1/2 = Off/On/Auto */
} nvm_values_t;

/* 当前 RAM 镜像。业务模块只改镜像并置 dirty；实际 SRAM 写入由 load_task 调 nvm_process()。 */
extern nvm_values_t nvm_data;

/* 生命周期 */
i2c_status_type nvm_init(void);       /* 读 SD3078；CRC/语义无效时写入默认值 */
i2c_status_type nvm_load(void);       /* 只加载；I2C_OK 不代表 CRC 一定有效，见 nvm_is_valid() */
i2c_status_type nvm_save(void);       /* 立即整块 70B 写入；通常仅初始化/调试直接调用 */
i2c_status_type nvm_process(void);    /* dirty 时写入；无 dirty 时不产生 I2C */
void nvm_request_save(void);          /* 仅置 dirty，可从其它任务调用 */
uint8_t nvm_is_dirty(void);
void nvm_reset_defaults(void);
uint8_t nvm_is_valid(void);

/* 固定字节序打包接口（little-endian） */
void nvm_pack_float(uint8_t out[4], float value);
float nvm_unpack_float(const uint8_t in[4]);
void nvm_pack_i16(uint8_t out[2], int16_t value);
int16_t nvm_unpack_i16(const uint8_t in[2]);

/* 持久化字段接口；setter 会自动置 dirty，但不会在调用线程直接访问 I2C。 */
float nvm_get_factory_capacity_wh(void);
void nvm_set_factory_capacity_wh(float value);
float nvm_get_equivalent_cycles(void);
void nvm_set_equivalent_cycles(float value);
void nvm_add_equivalent_cycles(float delta);
int16_t nvm_get_ibat_zero_raw(void);
void nvm_set_ibat_zero_raw(int16_t value);
float nvm_get_ibat_slope_ma_per_lsb(void);
void nvm_set_ibat_slope_ma_per_lsb(float value);
uint8_t nvm_get_backup_charge_mode(void);
void nvm_set_backup_charge_mode(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* NVM_STORE_H */
