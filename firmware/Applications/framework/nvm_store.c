/*
 * nvm_store.c - minimal persistent store on SD3078 70-byte Backup RAM
 *
 * Layout (offset relative to SD3078 RAM 0x2C):
 *   0x00..0x03  factory_capacity_wh
 *   0x04..0x07  discharge_energy_wh
 *   0x08..0x0B  shunt_mohm
 *   0x0C..0x43  reserved (56B)
 *   0x44..0x45  CRC16-CCITT over payload[0..67], seeded by build magic
 */
#include "nvm_store.h"
#include "sd3078.h"
#include <string.h>

#define NVM_OFF_FACTORY_CAPACITY_WH   0U
#define NVM_OFF_DISCHARGE_ENERGY_WH   4U
#define NVM_OFF_SHUNT_MOHM            8U

/* 存储格式明确绑定 32-bit IEEE754 float / 16-bit int16_t。
 * 用 typedef 断言兼容当前 ARMCLANG C 方言，不依赖 _Static_assert 开关。 */
typedef char nvm_float_must_be_4_bytes[(sizeof(float) == 4U) ? 1 : -1];
typedef char nvm_i16_must_be_2_bytes[(sizeof(int16_t) == 2U) ? 1 : -1];
typedef char nvm_layout_must_fill_sd3078[
    ((NVM_PAYLOAD_SIZE + 2U) == NVM_TOTAL_SIZE &&
     NVM_TOTAL_SIZE == SD3078_RAM_LEN) ? 1 : -1];

nvm_values_t nvm_data;

static uint8_t s_nvm_valid;

static uint16_t nvm_crc16(const uint8_t *data, uint16_t len, uint16_t seed)
{
    uint16_t crc = seed;
    uint16_t i;

    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (i = 0; i < 8U; i++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t nvm_build_magic(void)
{
#ifdef NVM_FIXED_MAGIC
    return (uint16_t)NVM_FIXED_MAGIC;
#else
    /* 原型阶段：用编译时间字符串做轻量混合，达到“每次完整编译换 magic”的目的。
     * 它不是随机数发生器，也不用于安全用途。 */
    static const char build_id[] = __DATE__ " " __TIME__;
    uint16_t h = 0xA5C3U;
    uint16_t i;

    for (i = 0; build_id[i] != '\0'; i++) {
        h = (uint16_t)((h << 5) | (h >> 11));
        h ^= (uint8_t)build_id[i];
        h = (uint16_t)(h * 109U + 0x3DU);
    }
    return (h != 0U) ? h : 0x1D0FU;
#endif
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
    /* v==v 用来拒绝 NaN；范围判断同时拒绝 +/-Inf。 */
    return (v == v && v >= lo && v <= hi) ? 1U : 0U;
}

static uint8_t nvm_values_valid(const nvm_values_t *v)
{
    if (!nvm_float_in_range(v->factory_capacity_wh, 1.0f, 200.0f)) return 0U;
    if (!nvm_float_in_range(v->discharge_energy_wh, 0.0f, 10000000.0f)) return 0U;
    if (!nvm_float_in_range(v->shunt_mohm, 0.1f, 20.0f)) return 0U;
    return 1U;
}

void nvm_reset_defaults(void)
{
    nvm_data.factory_capacity_wh = NVM_DEFAULT_FACTORY_CAPACITY_WH;
    nvm_data.discharge_energy_wh = NVM_DEFAULT_DISCHARGE_ENERGY_WH;
    nvm_data.shunt_mohm = NVM_DEFAULT_SHUNT_MOHM;
    s_nvm_valid = 0U;
}

uint8_t nvm_is_valid(void)
{
    return s_nvm_valid;
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
    calc_crc = nvm_crc16(block, NVM_PAYLOAD_SIZE, nvm_build_magic());

    if (stored_crc != calc_crc) {
        s_nvm_valid = 0U;
        return I2C_OK;
    }

    tmp.factory_capacity_wh = nvm_unpack_float(&block[NVM_OFF_FACTORY_CAPACITY_WH]);
    tmp.discharge_energy_wh = nvm_unpack_float(&block[NVM_OFF_DISCHARGE_ENERGY_WH]);
    tmp.shunt_mohm = nvm_unpack_float(&block[NVM_OFF_SHUNT_MOHM]);

    if (!nvm_values_valid(&tmp)) {
        s_nvm_valid = 0U;
        return I2C_OK;
    }

    nvm_data = tmp;
    s_nvm_valid = 1U;
    return I2C_OK;
}

i2c_status_type nvm_save(void)
{
    uint8_t block[NVM_TOTAL_SIZE];
    uint16_t crc;
    i2c_status_type st;

    if (!nvm_values_valid(&nvm_data)) {
        return I2C_ERR_STEP_1;
    }

    memset(block, 0, sizeof(block));
    nvm_pack_float(&block[NVM_OFF_FACTORY_CAPACITY_WH], nvm_data.factory_capacity_wh);
    nvm_pack_float(&block[NVM_OFF_DISCHARGE_ENERGY_WH], nvm_data.discharge_energy_wh);
    nvm_pack_float(&block[NVM_OFF_SHUNT_MOHM], nvm_data.shunt_mohm);

    crc = nvm_crc16(block, NVM_PAYLOAD_SIZE, nvm_build_magic());
    block[NVM_CRC_OFFSET] = (uint8_t)crc;
    block[NVM_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8);

    st = SD3078_SramWrite(0U, block, sizeof(block));
    if (st == I2C_OK) s_nvm_valid = 1U;
    return st;
}

i2c_status_type nvm_init(void)
{
    i2c_status_type st = nvm_load();

    if (st != I2C_OK) return st;
    if (s_nvm_valid) return I2C_OK;

    nvm_reset_defaults();
    return nvm_save();
}

float nvm_get_factory_capacity_wh(void)
{
    return nvm_data.factory_capacity_wh;
}

void nvm_set_factory_capacity_wh(float value)
{
    nvm_data.factory_capacity_wh = value;
}

float nvm_get_discharge_energy_wh(void)
{
    return nvm_data.discharge_energy_wh;
}

void nvm_set_discharge_energy_wh(float value)
{
    nvm_data.discharge_energy_wh = value;
}

void nvm_add_discharge_energy_wh(float delta_wh)
{
    if (delta_wh > 0.0f) nvm_data.discharge_energy_wh += delta_wh;
}

float nvm_get_shunt_mohm(void)
{
    return nvm_data.shunt_mohm;
}

void nvm_set_shunt_mohm(float value)
{
    nvm_data.shunt_mohm = value;
}
