# 修改日志（Backup Snapshot）

> **对照基线**：`http://192.168.1.16:8000/xuans/dummy.git` 的 `master` 分支（pristine 原版）。
> **本快照内容**：`host-pc/`、`dummy/`、`3.Software/` 三个工作目录（已排除构建 / 依赖 / 缓存产物）。
> **diff 统计**（仅核心 12 个固件源文件 vs 原版）：约 +1900 / -380 行。

---

## 0. 仓库结构调整

原版 `dummy.git` 把三个固件项目放在仓库根：

```
dummy-35motor-fw/   dummy-42motor-fw/   dummy-ref-core-fw/
```

本快照统一收敛到 `dummy/` 子目录下，并新增同级 `dummy/esp32-iot/` 引入 ESP32 + Mongoose 网络栈源码（uart-bridge 等示例）：

```
dummy/
├── dummy-35motor-fw/        ← 子板（35 步进）固件
├── dummy-42motor-fw/        ← 子板（42 步进）固件
├── dummy-ref-core-fw/       ← REF 1.0 主控板固件
└── esp32-iot/               ← 新增：ESP32 子项目（uart-bridge + Mongoose 示例）
```

仓库还新增了：

- `host-pc/`：全新的 Python 后端 + Vue 前端（详见第 1-3 节）
- `3.Software/`：上位机相关工具集（CLI-Tool / DummyStudio / simple-cli）
- 根 `.gitignore`：参照 `dummy/.gitignore` 风格分类

---

## 1. host-pc/backend —— 全新 FastAPI 后端

新建 Python 后端项目，提供 REST + WebSocket 接口控制机械臂。

### 1.1 协议层 `app/core/protocol.py`
- 实现 ESP32 ASCII 协议封装（`!`、`#`、`&`、`@`、`>` 五种前缀）
- `cmd_move_j` / `cmd_move_l` 支持**可选 speed 参数**（None 时发 6 参变体，否则 7 参）
- 补齐缺失的命令 tag 映射（`>` 作为 MoveJ 别名）
- `get_dce` 暴露 `quiet_ms` 与 `total_timeout` 用于多行响应聚合

### 1.2 核心 `app/core/robot.py`
- 完整的 Robot 高层 API（生命周期、运动、夹爪、RGB、状态查询、维护、调优、异步事件）
- **修复 `GetSpeed` 解析 bug**：固件回包是 `GetSpeed OK SPEED:%.2f`，原代码直接 `float(rest.split()[0])` 导致全部抛 `RobotError`；现已正确剥离 `SPEED:` 前缀
- **dispatcher 防崩**：`_dispatch_loop` 每次迭代独立 `try/except`，避免单条畸形固件输出让整个任务沉默死掉
- 多行响应聚合 + 状态缓存修复

### 1.3 传输 `app/core/transport.py`
- **关键 bug 修复**：`SerialTransport._read_line` 原实现固件 burst 多行时只切第 1 行就 return，剩余 7-14 行**永远卡在 `_buf` 中**。修复后优先消费缓冲区中所有完整行再读串口。这是 `#ONLINE` / `#GET_DCE` "中毒"现象的真凶
- `_publish` 改用 `_safe_put` helper，去掉不可达的 `try/except QueueFull` 死代码

### 1.4 API 层 `app/api/`
- `connection.py`：连接管理、端口列表
- `robot.py`：所有典型动作的 typed Pydantic 路由
- `ws.py`：WebSocket 状态广播
- **新增 `GET /api/diag/state`**：返回 dispatcher 是否活、`_pending`/`_sub.queue` 深度、最后 30 行 RX 日志，专为排查"所有命令一起超时"

### 1.5 入口 `app/main.py`
- 启动时挂载前端静态文件（PyInstaller 冻结时从 `_MEIPASS/frontend_dist`，开发模式从 `frontend/dist`）
- frozen 模式自动 `webbrowser.open` 到 `http://127.0.0.1:8000/`
- SPA fallback 路由

### 1.6 测试 `tests/test_simulation.py`
- 大量 `FakeTransport` 模拟测试（**118 全过**），覆盖：
  - 生命周期、手爪、RGB、状态查询、运动、诊断、重启、维护、调优、异步事件
  - 命令 tag 映射、`>` 别名
  - 多行响应聚合、超时/恢复、错误路径

### 1.7 实机调试脚本 `scripts/`
- `smoke_real.py`：分阶段（A 只读+RGB / B 夹爪 / C 本体运动）
- `probe.py` ~ `probe7.py`、`probe_clean*.py`、`probe_postfix.py`、`probe_timing.py`、`probe_fingerprint.py`：定位 `#ONLINE` / `#GET_DCE` 中毒 bug 的探针脚本

---

## 2. host-pc/frontend —— Vue 3 前端

新建 Vue 3 + Pinia + Element Plus 前端。

- `src/api/client.ts`：HTTP + WebSocket 客户端，类型化
- `src/stores/robot.ts`：Pinia store，封装连接 / 干跑模式 / 命令队列
- `src/components/JointControl.vue`：6 关节滑杆 + **`overrideSpeed` 开关**控制是否带 speed
- `src/components/CartControl.vue`：6D 笛卡尔输入 + 同样的 `overrideSpeed` 开关
- `src/components/ConnectionPanel.vue`、其他面板组件
- `vite.config.mts`：开发期 `/api` 与 `/ws` 代理到 `:8000`

---

## 3. host-pc PyInstaller 打包

将 backend + frontend 打包为单文件 `DummyRobot.exe`（17.7 MB）。

- `backend/DummyRobot.spec`：PyInstaller 配置，`--add-data frontend/dist → frontend_dist`
- `build.ps1`（仓库根）：一键构建脚本（前端 build + venv 安装 + pyinstaller）
- `app/main.py` 已支持 `_MEIPASS` 路径解析与 `frozen` 模式自动开浏览器

---

## 4. 固件 `dummy-ref-core-fw`（REF 1.0 主控板，已烧录）

实机调试过程中对主控板做的全部改动。中文注释、不改 `Respond` 的输出格式以兼容旧上位机。

### 4.1 通信底层 `Bsp/communication/`

**`interface_can.cpp` —— CAN TX 信号量死锁修复（三道保险）**
- **`tx_error()`** 原本是空函数。CAN 总线无节点应答（例如 REF 板单飞）时，HAL 的 `ErrorCallback` 走 TX_TERR{0,1,2} 调到这里却不释放信号量，配对的 `osSemaphoreAcquire` 永远等不到回调，整个 ASCII 处理线程被拖死十几秒。**修复**：按 `ctx->handle->Instance` 释放对应 `sem_can1_tx` / `sem_can2_tx`。
- **`tx_aborted_callback`** 同步释放信号量，覆盖 abort 路径。
- **`CanSendMessage`** 超时由 `osWaitForever` 改为 **100 ms**，作为兜底——万一以后再有未覆盖的失败路径，最多卡 100 ms。

**`interface_usb.cpp` —— USB CDC 发送加固**
- `USBSender::process_packet` 信号量等待 `PROTOCOL_SERVER_TIMEOUT_MS`（10 ms） → **100 ms**，覆盖 OS 调度抖动。
- **关键**：超时时**不再强行 `CDC_Transmit_FS`**。原版本超时仍调发送，但此时端点必然 `USBD_BUSY`，数据被丢且会错误地 `release` 掉信号量，造成计数失衡引发后续连锁丢包。现在改为：超时直接 `tx_overrun_cnt++` 并返回 `-1`，让上层感知。

**`ascii_processor.hpp` —— `Respond` 合并 packet**
- 把 `<text>` 与 `\r\n` 合并到**同一次** `process_bytes` 调用。原版本拆成两次调用 → USB CDC 路径下每次都要重新抢 `sem_usb_tx`，多行 burst 时第二次抢不到就丢 `\r\n`，进而连锁丢失后续整行。
- 输出字节流（`<text>\r\n`）保持不变，仅减少 USB packet 数量。

### 4.2 RGB 模块完整重构 `Bsp/gpio/rgb.{hpp,cpp}`

**模式枚举从 7 个老 enum 重写为 12 个用户模式 + 5 个 auto 子状态**：

| 模式号 | 名称 | 效果 |
|---|---|---|
| 0 | `MODE_OFF` | 关闭 |
| 1-7 | `MODE_RED/ORANGE/YELLOW/GREEN/CYAN/BLUE/PURPLE` | 七色纯色 |
| 8 | `MODE_CUSTOM` | 用户指定纯色（`customR/G/B`） |
| 9 | `MODE_RAINBOW` | 彩虹流水 |
| 10 | `MODE_BREATH` | 白色呼吸灯 |
| **11** | **`MODE_AUTO`** | **默认自动模式**，由 `autoSubState` 驱动 |

**Auto 子状态（不直接暴露给用户）**：`STANDBY`（白呼吸）/ `WORKING`（绿常亮）/ `COMPLETE`（青慢闪）/ `ESTOP`（红线性快闪）/ `PAUSED`（黄流水灯）。

**新增的灯效原语**：
- `SolidColor(r,g,b)` 通用纯色填充
- `BreathEffect(r,g,b,period)` 正弦曲线呼吸（独立 `breathStep` 计步器）
- `LinearFlash(r,g,b,period)` 三角波线性快闪（独立 `flashStep`）
- `SlowFlash(r,g,b)` 复用呼吸灯短周期版
- `YellowFlow()` 黄色流水灯（彩虹变体）
- `ResetEffectSteps()` 切换效果时同步复位三个计步器，避免相位残留

### 4.3 步进电机驱动 `Robot/actuators/ctrl_step/{hpp,cpp}`

**新增 DCE 参数实时 CAN 查询**（CAN 帧 0x27-0x2A）：
- `QueryDceKp/Kv/Ki/Kd()` 单参数查询；`QueryDceAll()` 一次查 4 个
- `DceParamCallback(paramId, value)` 由 `can_protocol.cpp` 接收回包后调用，更新 `dceKp/Kv/Ki/Kd` 和 `dceQueryDone` 标志
- 用 `volatile int32_t` 防优化

**新增节点在线检测**：
- `QueryOnline()` 复用 0x23（位置查询）作为探测帧
- `OnlineCallback()` 收到任意 0x23/0x25/0x27-0x2A 回包后置 `isOnline=true` 并刷新 `lastResponseTick`

### 4.4 机器人主类 `Robot/instances/dummy_robot.{h,cpp}`（412 行变更）

**手爪 `Hand2` 类**：
- 新增 `enabledFlag` 与 `SetEnable` override，**与基类 `state` 解耦**——基类 `state` 会被 `UpdateAngleCallback` 改写为 `FINISH`/`RUNNING`，导致 `isEnabled()` 返回错误。现 `isEnabled()` 直接读 `enabledFlag`。

**RGB 持久化与自动模式**：
- `LoadRGBModeFromEEPROM` / `SaveRGBModeToEEPROM`（地址 `0x00`，1 byte）
- `SetRGBAutoSubState` / `NotifyWorkStart` / `NotifyWorkDone` —— 工作开始切到 WORKING(绿)，完成切到 COMPLETE(青)
- `AutoModeIdleTick(deltaMs)` —— **5 秒无指令自动回 STANDBY**（白呼吸）
- `OnHandActionRGBHook(open)` 实现：开手爪→工作绿，闭手爪→完成青 + 5 秒回待机
- `rgbMode` 默认值由 `3` 改为 **`11`**（自动模式）

**节点在线检测周期任务**：
- `CheckNodeOnline(node)` 单节点同步检测；`CheckAllNodesOnline()` 全部检测
- `OnlineCheckTick(deltaMs)` —— **5 分钟周期检测**（工作时跳过避免干扰），失败则 `allNodesOnline=false`

**异步系统命令线程**（!START / !HOME / !RESET / !CLOSE）：
- 引入 `SysCmd` enum + `SysCmdMsg` 消息结构 + `InitSysCmdThread` + `PostSysCmd`
- 解决原本阻塞命令独占 UART 线程导致 `!STOP` 无法抢占的问题
- 阻塞动作改为投递到独立线程异步执行

**`Pause()` 返回类型** 由 `void` 改为 `bool`（true=成功进入 PAUSED；false=当前状态不允许）。

### 4.5 主入口 `UserApp/main.cpp`

**`ThreadControlLoopFixUpdate` 急停制动主循环**：

控制环原本是二态（使能/未使能）。引入 `STATE_EMERGENCY_STOP` 后改为三态分支：

| 条件 | 行为 |
|---|---|
| `IsEnabled() && state != EMERGENCY_STOP` | 正常分发 `commandMode` 对应的运动更新（NORMAL / PAUSED 都跑这条） |
| `IsEnabled() && state == EMERGENCY_STOP` | **每 tick 主动广播 V=0 实现机械制动**，并把 `targetJoints` 同步到 `currentJoints`，避免 RESUME 后 `MoveJoints` 跳动 |
| 未使能 | 仅刷新 `UpdateJointAngles` / `UpdateJointPose6D` |

> 这个改动的关键点：急停的 5 ms 实时刹车不再依赖 `OnUsbAsciiCmd → EmergencyStop()` 的调用路径，即使状态被 USB ISR 偷拍设置也能立即生效。

**`ThreadRGBUpdate` 30 ms tick** 内挂入：
- `dummy.AutoModeIdleTick(30)` —— 自动模式空闲计时
- `dummy.OnlineCheckTick(30)` —— 节点在线检测周期累计
- `RGB::ALLOff` 改名为 `RGB::MODE_OFF`

**`Main()` 启动序列追加**：
- `dummy.LoadRGBModeFromEEPROM()` —— 恢复用户上次保存的 RGB 模式
- `dummy.InitSysCmdThread()` —— 启动异步系统命令线程

### 4.6 协议层 `UserApp/protocols/`

**`ascii_protocol.cpp`（+995 / -186 行 vs 原版）—— 大量新增/重写命令**：

| 类别 | 命令 | 说明 |
|---|---|---|
| 状态查询 | `#GETJPOS`、`#GETSPEED` | 多行响应；返回 6 关节 + 夹爪位姿、当前速度（带 `SPEED:` 前缀） |
| 在线 | `#ONLINE` | 多行响应：`Online OK <summary>` + 7 × `J[n] OL/NL` / `HAND[7] OL/NL` |
| DCE 查询 | `#GET_DCE [node [kp\|kv\|ki\|kd]]` | 多行响应；零参→全 7 节点；一参→单节点全 4 项；两参→单参 |
| DCE 写入 | `#SET_DCE_KP/KV/KI/KD <node> <val>` | 单参数实时下发 |
| 关节调优 | `#ACC_J <node> <val>`、`#SPEED_J <node> <val>`、`#I_LIMIT_J <node> <val>` | 加速度 / 速度 / 电流限制 |
| 维护 | `#REBOOT`、`#OFFSET_J <node> <val>` | 软复位、关节偏置 |
| 系统状态 | `!CLOSE`、`!PAUSE`、`!RESUME` | 关机/暂停/恢复（异步线程驱动） |
| RGB | `!RGB_ON`、`!RGB_OFF`、`!RGB_MODE <0-11>`、`!RGB_CUSTOM <r> <g> <b>` | 配合 4.2 重构 |
| 手爪 | `!HD_EN`、`!HD_DIS`、`!HD_O`、`!HD_C`、`!HD_POS <pct>`、`!HD_ZERO`、`!HD_I <val>` | 完整手爪指令集 |

**Move 错误响应统一为带 tag 前缀**（`Move Error: not_homed` / `not_enabled` / `state=<name>` / `queue_full` / `Move OK Q:<n>`），便于 host-pc 解析。

`else` 分支兜底：`Error: unknown command, use '>'/'@'/'&'/'!'/'#' prefix`。

**`can_protocol.cpp`（+20 行）**：
- 收到任意 0x23 / 0x25 回包后调 `OnlineCallback()` 维护节点在线状态
- 新增分发 0x27 / 0x28 / 0x29 / 0x2A → `DceParamCallback(cmd, *(int32_t*)data)`，对 `motorJ[1..6]` 与 `hand2` 都生效

### 4.7 子板固件 `dummy-35motor-fw` / `dummy-42motor-fw`

`UserApp/protocols/interface_can.cpp` 同步加 4 个 case：
- **`0x27` Get DCE Kp** / **`0x28` Kv** / **`0x29` Ki** / **`0x2A` Kd`**
- 子板收到这四个查询帧时，把当前 `boardConfig.dce_kp/kv/ki/kd`（4 字节）按对端期待的 StdId 回送
- 这是支撑主控 `#GET_DCE` 实时查询命令的对端实现，必须 35 / 42 两套子板都同步烧录

---

## 5. 文档

- `host-pc/docs/command-coverage.md`：完整命令覆盖矩阵 + 实机 19/19 通过表 + `#ONLINE`/`#GET_DCE` 中毒事件复盘 + 修复矩阵
- `CHANGELOG.md`（本文件）

---

## 6. 仓库管理

- 根级 `.gitignore`：参照 `dummy/.gitignore` 风格分类（构建产物、嵌入式编译产物、CMake、Python、Node、IDE、系统文件）
- 备份分支 `backup-snapshot`（orphan）：仅含 `host-pc/`、`dummy/`、`3.Software/`，已排除：
  - `build/`、`cmake-build-*/`、`dist/`
  - `.venv/`、`venv/`、`__pycache__/`、`*.pyc`、`*.egg-info/`
  - `node_modules/`、`*.tsbuildinfo`
  - 嵌入式产物 `*.elf/hex/bin/map/o/obj`
  - `test/`、`MD/`、`TEST/`
  - IDE / 系统文件

---

## 验证状态

| 项 | 结果 |
|---|---|
| 后端 pytest | **118 / 118 ✓** |
| 实机 smoke test | **19 / 19 ✓**（含 8 行 `#ONLINE`、15 行 `#GET_DCE`）|
| `DummyRobot.exe` 启动 | ✓ 端点 `/`、`/api/*`、`/assets/*` 全部 200 |
| RGB 收尾 | `mode=11` (auto) + ON ✓ |
