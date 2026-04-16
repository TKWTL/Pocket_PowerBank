# Pocket_PowerBank 软件框架（AT32F423 + FreeRTOS + Keil）

## 1. 目标与约束

- 保持 AT32 WorkBench + Keil 工具链不变。
- 抽取可移植框架，支持 LVGL / u8g2 和统一菜单层。
- 重点落实低功耗：按键唤醒、IRQ 唤醒、无操作休眠、低电压转 Standby。

## 2. 四层架构与目录映射

- `Bootstrap/`：芯片启动与基础外设初始化。
- `Drivers/`：器件驱动和裸硬件适配。
- `Applications/framework/`：系统服务层（事件、睡眠倒计时、功耗策略、设备电源钩子）。
- `Applications/*.c`：业务任务层（UI、按键、SW6306、传感器任务）。

建议边界：

- 任务放在 `Applications/*.c`，保持小项目扁平结构。
- FreeRTOS 对接点在 `Applications/framework`（队列、事件、状态机）与 `Applications/*.c`（任务函数）。
- `svc/pm/ui` 归属：
  - `pm` 在 `Applications/framework`。
  - `ui` 在 `Applications/ui_task.c`。
  - 业务编排在 app 层任务，不放 BSP。

## 3. 任务划分建议

### 3.1 input_task（高优先级）

- 周期 5~10ms 扫描按键。
- 生成事件：单击/双击/长按。
- 每次有效输入调用 `pm_sleep_timer_refresh()`。

### 3.2 ui_task（中优先级）

- 仅处理画面状态机和菜单导航。
- 接收 input_task 事件，不直接扫描按键。
- 屏幕超时后进入 `UI_OFF`，并广播 `APP_EVT_UI_TIMEOUT`。

### 3.3 power_task / pm_task（中优先级）

- 统一睡眠流程：
  1) prepare：背光渐灭、LCD sleep cmd、外设准备。
  2) suspend：关 SPI/I2C 子设备，停不必要时钟。
  3) 选择模式：Halt / DeepSleep / Standby。
  4) wake：恢复时钟、恢复外设、通知 UI。
- 该任务持有系统级倒计时状态。

### 3.4 sw6306_task（中优先级）

- 仅负责 SW6306 访问、状态刷新、协议配置。
- 对外通过事件/消息接口暴露状态。

### 3.5 irq_service（ISR + defer）

- EXINT 中断里不做重活，只发 wake 事件。
- 具体处理放任务上下文。

## 4. 系统级睡眠倒计时 API

`pm_sleep_timer` 提供（当前默认超时 10s）：

- `init(timeout)`：默认超时。
- `refresh()`：有交互或关键事件时刷新。
- `set(timeout)`：菜单中修改超时。
- `force_expire()`：立即触发休眠流程。
- `left_ms()/expired()`：状态查询。

此模块用于无操作休眠，建议所有用户交互、端口插拔、充放电状态变化都刷新倒计时。

## 5. 低功耗模式决策

`pm_policy_next_state()` 输入：

- UI 是否点亮。
- 是否有 USB/DC 连接。
- 是否低电压。
- 是否强制 Standby。
- 是否有待处理 IRQ。

输出：

- `RUN`：保持运行（IdleHook 可执行 WFI）。
- `UI_OFF`：关闭屏幕与 UI 刷新。
- `SLEEP_PREPARE`：进入深睡前的外设收敛阶段。
- `DEEPSLEEP`：中长时间待机。
- `SLEEP_PENDING`：唤醒后短暂输入保护窗口。
- `STANDBY`：运输模式或低压保护。

当前实现中由 `powerdown_task` 直接调用 `pwc_deep_sleep_mode_enter()` 与 `pwc_standby_mode_enter()`。

## 6. 外设无感注册式电源管理

`pm_device_register()` 支持注册：

- `on_prepare`
- `on_suspend`
- `on_resume`
- `order`（顺序）

推荐顺序：

1. UI 相关（背光、LCD）
2. 传感器（SC7A20、SD3078）
3. 通信外设总线
4. 最后是系统时钟相关切换

说明：LCD 与背光建议分开，原因是背光可快速开关用于视觉灭屏，LCD 休眠指令与显存状态管理属于另一类时序。

## 7. 按键与菜单语义（按你的机型约束）

键位：`PREV(MENU)`、`CONF(LED)`、`NEXT(PWR)`，屏幕 160x40 横向。

- 任意键/IRQ 唤醒进入主屏。
- `NEXT` 单击：详细电压电流页。
- `NEXT` 双击：小电流模式开关。
- `CONF` 单击：进入/退出亮度小页（PREV/NEXT 调亮度）。
- `CONF` 双击：LED PWM 开关。
- `PREV` 双击：进入横向图标菜单。

不显示按键提示文字。

## 8. 业务逻辑归属：手电筒 + SW6306 PortA2

该逻辑归属建议：

- 判断与编排在 `app/service`（或 `power_task` 的业务子模块）。
- SW6306 具体寄存器操作在 `sw6306_task`。
- UI 只发出意图事件（例如 `LED_TOGGLE_REQ`）。

不要把该逻辑放到 BSP，BSP 只做硬件抽象。

## 9. 启动门控与 Standby 入口

建议流程：

1. 上电后先做最小初始化。
2. 在点亮屏幕前执行门控：
   - 电池电压阈值检查。
   - 特定按键组合检查。
3. 不满足唤醒条件则回睡眠。
4. 电压过低则进入 Standby。

GUI 菜单可提供 `system_enter_standby()` 直达运输模式。

## 10. Tickless 与 IdleHook

当前采用 IdleHook 的 `__WFI()` 做短期省电是可行方案。GUI 依赖定时器时，不建议直接启用 Tickless 破坏 LVGL 节拍。中长期可改为：

- UI 活跃时：禁用深度节能，仅 WFI。
- UI 熄屏时：允许 pm_task 进入 DeepSleep。


## 11. 参考 Collar-Radio 的迁移方法

借鉴点：

- 按键任务只做消抖和事件翻译，业务在其他任务处理。
- 任何用户操作都刷新休眠倒计时。
- 睡眠流程集中在系统任务轮询，不在 ISR 内做复杂逻辑。

本项目改进：

- 用 `pm_controller` 将倒计时、策略和设备钩子整合成单入口。
- `pm_policy` 明确引入 `UI_OFF/PREPARE/PENDING` 三个中间状态。
- 通过 `PM_MAX_SLEEP_DEPTH` 适配市电设备与电池设备的不同睡眠深度。
