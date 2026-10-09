/*
 * reset.c - 用户设置复位
 *
 * WORD_CONFIRM 的 CONF 只产生RAM请求，不在UI线程访问I2C：
 * - RAM-only设置由 MCU reset 自然恢复默认；
 * - 当前唯一持久化的用户设置 Backup Charge 恢复 Auto；
 * - EFC/SOH/分流电阻/SC7A20校准等隐藏NVM数据全部保留。
 * load_task 完成NVM落盘后调用 action_reset_process() 真正复位。
 */
#include "menu.h"
#include "at32f423_misc.h"
#include "functions.h"
#include "framework/nvm_store.h"

static volatile uint8_t s_reset_pending;

void action_reset_now(menu_item_t *it)
{
    (void)it;

    nvm_set_backup_charge_mode(NVM_DEFAULT_BACKUP_CHARGE_MODE);
    s_reset_pending = 1U;
}

void action_reset_process(void)
{
    if (s_reset_pending != 0U && nvm_is_valid() && nvm_is_dirty() == 0U) {
        nvic_system_reset();
    }
}
