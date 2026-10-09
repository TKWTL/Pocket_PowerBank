/*
 * nvm_store.c - persistent store on SD3078 70-byte Backup RAM
 *
 * Layout (offset relative to SD3078 RAM 0x2C):
 *   0x00..0x03  factory_capacity_wh
 *   0x04..0x07  equivalent_cycles
 *   0x08..0x0B  shunt_mohm (float)
 *   0x0C..0x0D  sc7a20_x_zero_raw (int16)
 *   0x0E..0x11  sc7a20_x_slope_mg_per_lsb (float)
 *   0x12..0x13  sc7a20_y_zero_raw (int16)
 *   0x14..0x17  sc7a20_y_slope_mg_per_lsb (float)
 *   0x18..0x19  sc7a20_z_zero_raw (int16)
 *   0x1A..0x1D  sc7a20_z_slope_mg_per_lsb (float)
 *   0x1E        backup_charge_mode
 *   0x1F..0x43  reserved (37B)
 *   0x44..0x45  HW CRC16 over payload[0..67], seeded by layout magic
 */
#include "nvm_store.h"
#include "sd3078.h"
#include "at32f423_crc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_usart.h"
#include <string.h>

#define NVM_OFF_FACTORY_CAPACITY_WH     0U
#define NVM_OFF_EQUIVALENT_CYCLES       4U
#define NVM_OFF_SHUNT_MOHM              8U
#define NVM_OFF_SC7A20_X_ZERO_RAW      12U
#define NVM_OFF_SC7A20_X_SLOPE         14U
#define NVM_OFF_SC7A20_Y_ZERO_RAW      18U
#define NVM_OFF_SC7A20_Y_SLOPE         20U
#define NVM_OFF_SC7A20_Z_ZERO_RAW      24U
#define NVM_OFF_SC7A20_Z_SLOPE         26U
#define NVM_OFF_BACKUP_CHARGE_MODE     30U

/* Bump only when serialized offsets/types change. Normal firmware rebuilds must
 * keep this value so existing NVM remains valid. */
#define NVM_LAYOUT_CRC_INIT        0x4E05U

/* 存储格式明确绑定 32-bit IEEE754 float / 16-bit int16_t。 */
typedef char nvm_float_must_be_4_bytes[(sizeof(float) == 4U) ? 1 : -1];
typedef char nvm_i16_must_be_2_bytes[(sizeof(int16_t) == 2U) ? 1 : -1];
typedef char nvm_layout_must_fill_sd3078[
    ((NVM_PAYLOAD_SIZE + 2U) == NVM_TOTAL_SIZE &&
     NVM_TOTAL_SIZE == SD3078_RAM_LEN) ? 1 : -1];

nvm_values_t nvm_data;

static uint8_t s_nvm_valid;
static volatile uint8_t s_nvm_dirty;

static uint16_t nvm_crc16(const uint8_t *data)
{
    uint16_t i;

    /* CRC clock is enabled by wk_config and restored after DeepSleep. */
    CRC->ctrl = 0U;
    CRC->ctrl_bit.poly_size = CRC_POLY_SIZE_16B;
    CRC->poly = 0x1021U;
    CRC->idt = NVM_LAYOUT_CRC_INIT;
    CRC->ctrl_bit.rst = 1U;

    for (i = 0U; i < NVM_PAYLOAD_SIZE; i += 4U) {
        CRC->dt = (uint32_t)data[i] |
                  ((uint32_t)data[i + 1U] << 8) |
                  ((uint32_t)data[i + 2U] << 16) |
                  ((uint32_t)data[i + 3U] << 24);
    }
    return (uint16_t)CRC->dt;
}

void nvm_pack_float(uint8_t out[4], float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    out[0] = (uint8_t)(raw >> 0);
    out[1] = (uint8_t)(raw >> 8);
    out[2] = (uint8_t)(raw >> 16);
    out[3] = (uint8_t)(raw >> 24);
}

float nvm_unpack_float(const uint8_t in[4])
{
    uint32_t raw = ((uint32_t)in[0] << 0) |
                   ((uint32_t)in[1] << 8) |
                   ((uint32_t)in[2] << 16) |
                   ((uint32_t)in[3] << 24);
    float value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

void nvm_pack_i16(uint8_t out[2], int16_t value)
{
    uint16_t raw = (uint16_t)value;
    out[0] = (uint8_t)(raw >> 0);
    out[1] = (uint8_t)(raw >> 8);
}

int16_t nvm_unpack_i16(const uint8_t in[2])
{
    uint16_t raw = (uint16_t)in[0] | ((uint16_t)in[1] << 8);
    return (int16_t)raw;
}

static uint8_t nvm_float_in_range(float v, float lo, float hi)
{
    return (v == v && v >= lo && v <= hi) ? 1U : 0U; /* v==v rejects NaN */
}

static uint8_t nvm_values_valid(const nvm_values_t *v)
{
    if (!nvm_float_in_range(v->factory_capacity_wh, 1.0f, 200.0f)) return 0U;
    if (!nvm_float_in_range(v->equivalent_cycles, 0.0f, 1000000.0f)) return 0U;
    if (!nvm_float_in_range(v->shunt_mohm, 0.1f, 20.0f)) return 0U;
    if (!nvm_float_in_range(v->sc7a20_x_slope_mg_per_lsb, 0.1f, 4.0f)) return 0U;
    if (!nvm_float_in_range(v->sc7a20_y_slope_mg_per_lsb, 0.1f, 4.0f)) return 0U;
    if (!nvm_float_in_range(v->sc7a20_z_slope_mg_per_lsb, 0.1f, 4.0f)) return 0U;
    if (v->backup_charge_mode > 2U) return 0U;
    return 1U;
}

void nvm_reset_defaults(void)
{
    taskENTER_CRITICAL();
    nvm_data.factory_capacity_wh = NVM_DEFAULT_FACTORY_CAPACITY_WH;
    nvm_data.equivalent_cycles = NVM_DEFAULT_EQUIVALENT_CYCLES;
    nvm_data.shunt_mohm = NVM_DEFAULT_SHUNT_MOHM;
    nvm_data.sc7a20_x_zero_raw = NVM_DEFAULT_SC7A20_X_ZERO_RAW;
    nvm_data.sc7a20_x_slope_mg_per_lsb = NVM_DEFAULT_SC7A20_X_SLOPE_MG_PER_LSB;
    nvm_data.sc7a20_y_zero_raw = NVM_DEFAULT_SC7A20_Y_ZERO_RAW;
    nvm_data.sc7a20_y_slope_mg_per_lsb = NVM_DEFAULT_SC7A20_Y_SLOPE_MG_PER_LSB;
    nvm_data.sc7a20_z_zero_raw = NVM_DEFAULT_SC7A20_Z_ZERO_RAW;
    nvm_data.sc7a20_z_slope_mg_per_lsb = NVM_DEFAULT_SC7A20_Z_SLOPE_MG_PER_LSB;
    nvm_data.backup_charge_mode = NVM_DEFAULT_BACKUP_CHARGE_MODE;
    s_nvm_valid = 0U;
    s_nvm_dirty = 0U;
    taskEXIT_CRITICAL();

    USART_Printf("[NVM] Reset defaults\r\n");
}

uint8_t nvm_is_valid(void)
{
    return s_nvm_valid;
}

void nvm_request_save(void)
{
    s_nvm_dirty = 1U;
}

uint8_t nvm_is_dirty(void)
{
    return s_nvm_dirty;
}

i2c_status_type nvm_load(void)
{
    uint8_t block[NVM_TOTAL_SIZE];
    uint16_t stored_crc;
    uint16_t calc_crc;
    nvm_values_t tmp;
    i2c_status_type st;

    st = SD3078_SramRead(0U, block, sizeof(block));
    if (st != I2C_OK) {
        s_nvm_valid = 0U;
        return st;
    }

    stored_crc = (uint16_t)block[NVM_CRC_OFFSET] |
                 ((uint16_t)block[NVM_CRC_OFFSET + 1U] << 8);
    calc_crc = nvm_crc16(block);

    if (stored_crc != calc_crc) {
        s_nvm_valid = 0U;
        return I2C_OK;
    }

    tmp.factory_capacity_wh = nvm_unpack_float(&block[NVM_OFF_FACTORY_CAPACITY_WH]);
    tmp.equivalent_cycles = nvm_unpack_float(&block[NVM_OFF_EQUIVALENT_CYCLES]);
    tmp.shunt_mohm = nvm_unpack_float(&block[NVM_OFF_SHUNT_MOHM]);
    tmp.sc7a20_x_zero_raw = nvm_unpack_i16(&block[NVM_OFF_SC7A20_X_ZERO_RAW]);
    tmp.sc7a20_x_slope_mg_per_lsb = nvm_unpack_float(&block[NVM_OFF_SC7A20_X_SLOPE]);
    tmp.sc7a20_y_zero_raw = nvm_unpack_i16(&block[NVM_OFF_SC7A20_Y_ZERO_RAW]);
    tmp.sc7a20_y_slope_mg_per_lsb = nvm_unpack_float(&block[NVM_OFF_SC7A20_Y_SLOPE]);
    tmp.sc7a20_z_zero_raw = nvm_unpack_i16(&block[NVM_OFF_SC7A20_Z_ZERO_RAW]);
    tmp.sc7a20_z_slope_mg_per_lsb = nvm_unpack_float(&block[NVM_OFF_SC7A20_Z_SLOPE]);
    tmp.backup_charge_mode = block[NVM_OFF_BACKUP_CHARGE_MODE];

    if (!nvm_values_valid(&tmp)) {
        s_nvm_valid = 0U;
        return I2C_OK;
    }

    taskENTER_CRITICAL();
    nvm_data = tmp;
    s_nvm_valid = 1U;
    s_nvm_dirty = 0U;
    taskEXIT_CRITICAL();

    USART_Printf("[NVM] Restore OK\r\n");
    return I2C_OK;
}

i2c_status_type nvm_save(void)
{
    uint8_t block[NVM_TOTAL_SIZE];
    uint16_t crc;
    nvm_values_t snap;
    i2c_status_type st;

    taskENTER_CRITICAL();
    snap = nvm_data;
    taskEXIT_CRITICAL();

    if (!nvm_values_valid(&snap)) {
        return I2C_ERR_STEP_1;
    }

    memset(block, 0, sizeof(block));
    nvm_pack_float(&block[NVM_OFF_FACTORY_CAPACITY_WH], snap.factory_capacity_wh);
    nvm_pack_float(&block[NVM_OFF_EQUIVALENT_CYCLES], snap.equivalent_cycles);
    nvm_pack_float(&block[NVM_OFF_SHUNT_MOHM], snap.shunt_mohm);
    nvm_pack_i16(&block[NVM_OFF_SC7A20_X_ZERO_RAW], snap.sc7a20_x_zero_raw);
    nvm_pack_float(&block[NVM_OFF_SC7A20_X_SLOPE], snap.sc7a20_x_slope_mg_per_lsb);
    nvm_pack_i16(&block[NVM_OFF_SC7A20_Y_ZERO_RAW], snap.sc7a20_y_zero_raw);
    nvm_pack_float(&block[NVM_OFF_SC7A20_Y_SLOPE], snap.sc7a20_y_slope_mg_per_lsb);
    nvm_pack_i16(&block[NVM_OFF_SC7A20_Z_ZERO_RAW], snap.sc7a20_z_zero_raw);
    nvm_pack_float(&block[NVM_OFF_SC7A20_Z_SLOPE], snap.sc7a20_z_slope_mg_per_lsb);
    block[NVM_OFF_BACKUP_CHARGE_MODE] = snap.backup_charge_mode;

    crc = nvm_crc16(block);
    block[NVM_CRC_OFFSET] = (uint8_t)crc;
    block[NVM_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8);

    st = SD3078_SramWrite(0U, block, sizeof(block));
    if (st == I2C_OK) {
        s_nvm_valid = 1U;
        USART_Printf("[NVM] Write OK\r\n");
    }
    return st;
}

i2c_status_type nvm_process(void)
{
    i2c_status_type st;

    taskENTER_CRITICAL();
    if (s_nvm_dirty == 0U) {
        taskEXIT_CRITICAL();
        return I2C_OK;
    }
    /* 先消费本次 dirty。保存过程中若其它任务再次修改，setter 会重新置 1，
     * 成功返回后下一轮仍会再保存，不会丢更新。 */
    s_nvm_dirty = 0U;
    taskEXIT_CRITICAL();

    st = nvm_save();
    if (st != I2C_OK) {
        s_nvm_dirty = 1U;
    }
    return st;
}

i2c_status_type nvm_init(void)
{
    i2c_status_type st = nvm_load();

    if (st != I2C_OK) return st;
    if (s_nvm_valid) return I2C_OK;

    nvm_reset_defaults();
    st = nvm_save();
    if (st == I2C_OK) {
        s_nvm_valid = 1U;
        s_nvm_dirty = 0U;
    }
    return st;
}

float nvm_get_factory_capacity_wh(void)
{
    return nvm_data.factory_capacity_wh;
}

void nvm_set_factory_capacity_wh(float value)
{
    taskENTER_CRITICAL();
    nvm_data.factory_capacity_wh = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

float nvm_get_equivalent_cycles(void)
{
    return nvm_data.equivalent_cycles;
}

void nvm_set_equivalent_cycles(float value)
{
    taskENTER_CRITICAL();
    nvm_data.equivalent_cycles = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

void nvm_add_equivalent_cycles(float delta)
{
    if (delta <= 0.0f) return;
    taskENTER_CRITICAL();
    nvm_data.equivalent_cycles += delta;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

float nvm_get_shunt_mohm(void)
{
    return nvm_data.shunt_mohm;
}

void nvm_set_shunt_mohm(float value)
{
    taskENTER_CRITICAL();
    nvm_data.shunt_mohm = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

int16_t nvm_get_sc7a20_x_zero_raw(void)
{
    return nvm_data.sc7a20_x_zero_raw;
}

void nvm_set_sc7a20_x_zero_raw(int16_t value)
{
    taskENTER_CRITICAL();
    nvm_data.sc7a20_x_zero_raw = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

float nvm_get_sc7a20_x_slope_mg_per_lsb(void)
{
    return nvm_data.sc7a20_x_slope_mg_per_lsb;
}

void nvm_set_sc7a20_x_slope_mg_per_lsb(float value)
{
    taskENTER_CRITICAL();
    nvm_data.sc7a20_x_slope_mg_per_lsb = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

int16_t nvm_get_sc7a20_y_zero_raw(void)
{
    return nvm_data.sc7a20_y_zero_raw;
}

void nvm_set_sc7a20_y_zero_raw(int16_t value)
{
    taskENTER_CRITICAL();
    nvm_data.sc7a20_y_zero_raw = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

float nvm_get_sc7a20_y_slope_mg_per_lsb(void)
{
    return nvm_data.sc7a20_y_slope_mg_per_lsb;
}

void nvm_set_sc7a20_y_slope_mg_per_lsb(float value)
{
    taskENTER_CRITICAL();
    nvm_data.sc7a20_y_slope_mg_per_lsb = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

int16_t nvm_get_sc7a20_z_zero_raw(void)
{
    return nvm_data.sc7a20_z_zero_raw;
}

void nvm_set_sc7a20_z_zero_raw(int16_t value)
{
    taskENTER_CRITICAL();
    nvm_data.sc7a20_z_zero_raw = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

float nvm_get_sc7a20_z_slope_mg_per_lsb(void)
{
    return nvm_data.sc7a20_z_slope_mg_per_lsb;
}

void nvm_set_sc7a20_z_slope_mg_per_lsb(float value)
{
    taskENTER_CRITICAL();
    nvm_data.sc7a20_z_slope_mg_per_lsb = value;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}

uint8_t nvm_get_backup_charge_mode(void)
{
    return nvm_data.backup_charge_mode;
}

void nvm_set_backup_charge_mode(uint8_t mode)
{
    if (mode > 2U) return;
    taskENTER_CRITICAL();
    nvm_data.backup_charge_mode = mode;
    s_nvm_dirty = 1U;
    taskEXIT_CRITICAL();
}
