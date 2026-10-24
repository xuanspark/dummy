# Host-PC ↔ Firmware 命令覆盖审计

> 目的：对照 `dummy-ref-core-fw/UserApp/protocols/ascii_protocol.cpp` 里固件支持的所有 ASCII 命令，检查 `host-pc` 三层（前端 UI / 后端 `Robot` 方法 / HTTP 路由）的覆盖情况，方便排任务。
>
> 审计基准版本：2026-05-04

---

## 图例

| 记号 | 含义 |
|------|------|
| ✅ | **全栈接好**：前端 UI + 后端方法 + HTTP 路由齐全 |
| ⚠️ | **半接**：底层已就绪但 UI 未暴露 / UI 直接用 `sendRaw()` 透传（缺类型化方法 + 路由） |
| ❌ | **未接入**：仅在 `backend/app/core/protocol.py` 的 `COMMAND_TAGS` 里有响应解析，没人调用 |
| 🚫 | **完全没碰**：连 tag map 都没有 |

---

## 1. 生命周期（`!` 前缀）

| 命令 | 前端 UI | 后端方法 | HTTP 路由 | 状态 |
|---|---|---|---|---|
| `!START` | `QuickCommands.vue` | `start_robot()` | `POST /api/start` | ✅ |
| `!STOP` | Header E-STOP | `estop()` | `POST /api/estop` | ✅ |
| `!PAUSE` | Header | `pause()` | `POST /api/pause` | ✅ |
| `!RESUME` | Header | `resume()` | `POST /api/resume` | ✅ |
| `!CLOSE` | `QuickCommands.vue` | `shutdown_robot()` | `POST /api/close` | ✅ |
| `!HOME` | `QuickCommands.vue` | `home()` | `POST /api/home` | ✅ |
| `!RESET` | `QuickCommands.vue` | `reset_pose()` | `POST /api/reset` | ✅ |
| `!CALIBRATION` | `DiagPanel.vue` (Maintenance) | `calibrate()` | `POST /api/calibrate` | ✅ |

---

## 2. 手爪（`!HD_*`）

| 命令 | 前端 UI | 后端方法 | HTTP 路由 | 状态 |
|---|---|---|---|---|
| `!HD_O` | `HandClaw.vue` | `hand_open()` | `POST /api/hand/open` | ✅ |
| `!HD_C` | `HandClaw.vue` | `hand_close()` | `POST /api/hand/close` | ✅ |
| `!HD_EN` / `!HD_DIS` | `HandClaw.vue` | `hand_enable(bool)` | `POST /api/hand/enable` | ✅ |
| `!HD_POS <0-100>` | `HandClaw.vue` | `hand_pos(percent)` | `POST /api/hand/pos` | ✅ |
| `!HD_ZERO` | `HandClaw.vue` | `hand_zero()` | `POST /api/hand/zero` | ✅ |
| `!HD_I <0-2.0>` | `HandClaw.vue` | `hand_current(amps)` | `POST /api/hand/current` | ✅ |

---

## 3. RGB 灯效（`!RGB_*`）

| 命令 | 前端 UI | 后端方法 | HTTP 路由 | 状态 |
|---|---|---|---|---|
| `!RGB_ON` / `!RGB_OFF` | `RgbPanel.vue` | `rgb_on()` / `rgb_off()` | `POST /api/rgb/on \| off` | ✅ |
| `!RGB_MODE <0-11>` | `RgbPanel.vue` | `rgb_mode(mode)` | `POST /api/rgb/mode` | ✅ |
| `!RGB_CUSTOM <r> <g> <b>` | `RgbPanel.vue` | `rgb_custom(r,g,b)` | `POST /api/rgb/custom` | ✅ |

---

## 4. 状态查询（`#GET*` / `#ONLINE`）

| 命令 | 前端 UI | 后端方法 | HTTP 路由 | 状态 |
|---|---|---|---|---|
| `#GETJPOS` | `JointControl.vue` 周期轮询 | `get_jpos()` | `GET /api/state/joint` | ✅ |
| `#GETLPOS` | `JointControl.vue` + `CartControl.vue` 轮询 | `get_lpos()` | `GET /api/state/cart` | ✅ |
| **`#GETSPEED`** | — | `get_speed()` | `GET /api/state/speed` | ⚠️ |
| `#ONLINE [1-7]` | `DiagPanel.vue` | `check_online(node?)` | `GET /api/diag/online` | ✅ |

---

## 5. DCE / 运动参数（`#SET_*` / `#GET_DCE` / `#ACC_J` 等）

| 命令 | 前端 UI | 后端方法 | HTTP 路由 | 状态 |
|---|---|---|---|---|
| `#SET_DCE_KP <n> <v>` | `DceTuning.vue` | `set_dce_gain(n,'kp',v)` | `POST /api/dce/gain` | ✅ |
| `#SET_DCE_KV <n> <v>` | `DceTuning.vue` | `set_dce_gain(n,'kv',v)` | `POST /api/dce/gain` | ✅ |
| `#SET_DCE_KI <n> <v>` | `DceTuning.vue` | `set_dce_gain(n,'ki',v)` | `POST /api/dce/gain` | ✅ |
| `#SET_DCE_KD <n> <v>` | `DceTuning.vue` | `set_dce_gain(n,'kd',v)` | `POST /api/dce/gain` | ✅ |
| `#GET_DCE [n [kp\|kv\|ki\|kd]]` | `DceTuning.vue` (自动回填) | `get_dce(n?, p?)` | `GET /api/dce` | ✅ |
| `#ACC_J <n> <v>` | `DceTuning.vue` | `set_motion_limit(n,'acc',v)` | `POST /api/motion/limit` | ✅ |
| `#SPEED_J <n> <v>` | `DceTuning.vue` | `set_motion_limit(n,'speed',v)` | `POST /api/motion/limit` | ✅ |
| `#I_LIMIT_J <n> <v>` | `DceTuning.vue` | `set_motion_limit(n,'i_limit',v)` | `POST /api/motion/limit` | ✅ |
| `#OFFSET_J <1-7\|all>` | `DiagPanel.vue` (Maintenance) | `offset_joint(target)` | `POST /api/offset_j` | ✅（写 NVS，触发自动重启） |
| `#CMDMODE <mode>` | `DiagPanel.vue` (Maintenance) | `set_cmd_mode(mode)` | `POST /api/cmdmode` | ✅ |
| **`#REBOOT [0\|1-7]`** | `DiagPanel.vue` | `reboot(node?)` | `POST /api/reboot` | ✅ |

---

## 6. 运动队列（`>` / `@` / `&`）

| 命令 | 前端 UI | 后端方法 | HTTP 路由 | 状态 |
|---|---|---|---|---|
| `&<j1,j2,…,j6>[,speed]`（MoveJ） | `JointControl.vue` (Override 开关) | `move_j(j, speed=None)` | `POST /api/move/joint` | ✅ |
| `@<x,y,z,a,b,c>[,speed]`（MoveL） | `CartControl.vue` (Override 开关) | `move_l(pose, speed=None)` | `POST /api/move/cart` | ✅ |
| `>...`（`&` 别名） | —（UI 不暴露） | — | — (`/api/raw` 透传) | ↪️ 别名 |

---

## 待办任务清单（按价值 / 成本排序）

### P0 — 高价值、低成本（✅ 已完成 2026-05-04）

| # | 任务 | 涉及改动 | 状态 |
|---|---|---|---|
| P0-1 | **节点在线自检 UI**：`#ONLINE` 多行聚合 + J1–J6/HAND OL·NL 徽标 | `Robot.check_online()` + `GET /api/diag/online` + `DiagPanel.vue` | ✅ |
| P0-2 | **按节点重启**：`#REBOOT [0\|1-7]`（0=ref、1-6=motor、7=hand、空=全部+系统复位） | `Robot.reboot()` + `POST /api/reboot` + `DiagPanel.vue` 下拉 + 二次确认 | ✅ |
| P0-3 | **MoveL 笛卡尔面板**：X/Y/Z/A/B/C 6 滑条，复用 Joint 节流策略 | `CartControl.vue` + Cartesian Tab | ✅ |
| P0-4 | **LPos 并排显示**：JointControl 同时轮询 `getLpos` 并渲染压缩 strip | `JointControl.vue` + `robot.ts` 已有 `lpos` 字段 | ✅ |

**验证**：后端 `pytest` 22 项全过；前端 `vue-tsc --noEmit` 通过。

### P1 — 诊断 / 高级用户向（✅ 已完成 2026-05-04）

| # | 任务 | 实现 | 状态 |
|---|---|---|---|
| P1-1 | **`!CALIBRATION`** 按钮 | `Robot.calibrate()` + `POST /api/calibrate` + DiagPanel Maintenance 区，二次确认，30s 超时 | ✅ |
| P1-2 | **`#OFFSET_J`** 零点偏移写入 | `Robot.offset_joint(target)` + `POST /api/offset_j` + DiagPanel 红色危险区，弹窗告警 NVS + 可能重启 | ✅ |
| P1-3 | **`#CMDMODE`** 命令模式切换 | `Robot.set_cmd_mode(mode)` + `POST /api/cmdmode`；从 `main.cpp` OLED 识别出 1=SEQ 2=INT 3=TRJ 4=TUN | ✅ |

**验证**：后端 `pytest` 22 项全过；前端 `vue-tsc --noEmit` 通过。

### P2 — 将 ⚠️ "UI 透传" 升级为类型化 API（✅ 已完成 2026-05-04）

| 命令 | 后端方法 | HTTP 路由 | 状态 |
|---|---|---|---|
| `!HD_ZERO` | `hand_zero()` → `(closed, opened)` 角度 | `POST /api/hand/zero` | ✅ |
| `!HD_I <a>` | `hand_current(amps)`（严格校验 0 < amps ≤ 2） | `POST /api/hand/current` | ✅ |
| `#SET_DCE_K{P,V,I,D}` | `set_dce_gain(node, kind, val)`（node 1…6） | `POST /api/dce/gain` | ✅ |
| `#GET_DCE [n [p]]` | `get_dce(node?, param?)` 多行聚合解析，DceTuning 点 Get DCE 即自动回填输入框 | `GET /api/dce` | ✅ |
| `#ACC_J` / `#SPEED_J` / `#I_LIMIT_J` | `set_motion_limit(node, kind, val)`（node 1…7） | `POST /api/motion/limit` | ✅ |

**附加修复**：补齐了 `protocol.py` `COMMAND_TAGS` 中缺失的 `#SET_DCE_K{P,V,I,D}` / `#ACC_J` / `#SPEED_J` / `#I_LIMIT_J` 映射 —— 之前这几条命令调用 `expected_tag()` 会抛 `KeyError`，`sendRaw` 路径是坏的。

**抽象**：`Robot._collect_lines(cmd, predicate)` 复用 —— `check_online()` 和 `get_dce()` 共用安静窗口多行聚合。

**验证**：后端 `pytest` 22 项全过；前端 `vue-tsc --noEmit` 通过。

### P3 — 研究后决议（✅ 已研究 2026-05-04）

| # | 任务 | 决议 | 状态 |
|---|---|---|---|
| P3-1 | **`>` 前缀语义** | `>` 是 `&` 的完全别名。证据：`@d:/Desktop/t3/dummy/dummy-ref-core-fw/Robot/instances/dummy_robot.cpp:782-802` 中 `if (_cmd[0] == '>' \|\| _cmd[0] == '&')` 同一分支走 `MoveJ`，仅 sscanf 首字符不同。**不在 UI 暴露**；仅在 `protocol.py` `COMMAND_TAGS` 加 `">": "Move"` + `expected_tag()` 允许 `>`头，使 `/api/raw` 透传不会 `KeyError`。 | ✅ |

**验证**：后端 `pytest` 22 项全过。

### P4 — 运动命令 speed 参数可选（✅ 已完成 2026-05-04）

固件 `@d:/Desktop/t3/dummy/dummy-ref-core-fw/Robot/instances/dummy_robot.cpp:782-833` 中 `ParseCommand` 同时接受 6 个参数（使用当前速度）和 7 个参数（覆盖速度）两种变体：

```c
if (argNum == 6)      { context->MoveJ(...); }                       // 用当前 speed
else if (argNum == 7) { context->SetJointSpeed(speed); MoveJ(...); }  // 覆盖 speed
```

以前 host-pc 总是发 7 个值，每次运动都重复 `SetJointSpeed`，丢失了“使用上次 / `#SPEED_J` 设定”的能力。

| 层 | 变动 |
|---|---|
| 后端 `protocol.py` | `cmd_move_j` / `cmd_move_l` `speed: Optional[float] = None`；None 发 6 参变体 |
| 后端 `robot.py` | `move_j(j, speed=None)` / `move_l(pose, speed=None)` |
| 后端 `api/robot.py` | `JointMove.speed` / `CartMove.speed` 改 `Optional[float] = None` |
| 前端 `client.ts` | `moveJoint(joints, speed?)` / `moveCart(pose, speed?)`；`speed=null` 时 body 不包含 `speed` 字段 |
| 前端 `store.sendMoveJoint` | `speedDeg?` 可选；干走 dry-run 日志拼接 |
| `JointControl.vue` / `CartControl.vue` | 新增 **Override** 复选框（默认 ON）。关闭后 Speed 输入框置灰，发送 6 参 wire form |

**测试**：新增 `test_move_j_without_speed` / `test_move_l_without_speed` / `>` 别名 tag 断言。后端 `pytest` 24 项全过；前端 `vue-tsc --noEmit` 通过。

**用户例子（可以原样发）**：
```
@175,44,160,-135,-13.54,-14.6     # MoveL 不覆盖 speed
&0,0,134,35,66,0                  # MoveJ 不覆盖 speed
```

### P5 — 端到端模拟测试（✅ 已完成 2026-05-05）

新增 `@d:/Desktop/t3/host-pc/backend/tests/test_simulation.py`（94 项）。
用一个进程内的 `FakeTransport` 桩照 firmware
`@d:/Desktop/t3/dummy/dummy-ref-core-fw/UserApp/protocols/ascii_protocol.cpp`
里实际的回包字符串脚本化响应，全程驱动 `Robot` 类型化 API，验证：

- 上行 wire 字节确实是固件可解析的语法（`&` / `@` 6 vs 7 参变体；`!HD_POS` 数值范围；`#SET_DCE_K?` 节点 1..6；motion limit 节点 1..7 等）
- 多行响应（`#ONLINE` 带 7 个 `J[n] OL/NL` 行，`#GET_DCE` 14 行交替 header+payload）能被 quiet-window 聚合 + 解析
- 错误响应 `Move Error: limit J1 Max` / `not_enabled` / `GetDce Error:` 等正确转成 `RobotError`
- 范围/类型验证在**发出之前**拒绝（不会污染串口）
- 异步事件（用户按下物理 Estop 时的 `Estop OK`）不会让在飞 `_exec` 提前完成

**测试覆盖**：lifecycle / hand / RGB / state queries / movement /
`#ONLINE` / `#REBOOT` / maintenance / `#SET_DCE_*` / `#ACC_J|SPEED_J|I_LIMIT_J` /
`#GET_DCE` / async events / 全套 tag 映射回归。

**抓到的真 bug**：`Robot.get_speed()` 与 dispatcher 状态缓存均直接 `float(rest.split()[0])`，但
固件回包格式是 `GetSpeed OK SPEED:%.2f`（带 `SPEED:` 前缀），生产代码会一直抛
`malformed GetSpeed payload`。已在 `@d:/Desktop/t3/host-pc/backend/app/core/robot.py:85-92` 与 `:203-213`
修复为容忍 `SPEED:N` / 裸数字两种格式。

**最终结果**：后端 `pytest` **118/118 ✓**（24 protocol + 94 simulation）；前端 `vue-tsc --noEmit` ✓

### P6 — 实机 smoke-test（✅ 已完成 2026-05-05）

新增 `@d:/Desktop/t3/host-pc/backend/scripts/smoke_real.py`，分阶段对真机走完一遍 REST API：

- **Phase A** — 只读 + RGB（无运动）
- **Phase B** — 夹爪（小幅、可控）
- **Phase C** — 本体运动（`!START` / `!HOME` / 微动 / `!CLOSE`）— 需 `--phase C`

#### 实机环境
- 板子：**REF 1.0**（VID:PID `1209:0D32`），COM4 接口
- 后端 `python -m app.main` + 前端 `npm run dev`

#### 实机抓到的 host-pc bug（已修）

1. **`Robot.get_speed()` 与 dispatcher 状态缓存**直接 `float(rest.split()[0])`，但固件实际回 `GetSpeed OK SPEED:%.2f`（带 `SPEED:` 前缀）→ 全部 `RobotError("malformed GetSpeed payload")`。修复点：`@d:/Desktop/t3/host-pc/backend/app/core/robot.py:85-93` `:211-221`。**这个 bug 模拟测试和实机测试都抓到了。**

#### 实机加固（无 bug、纯防御）

2. **dispatcher 防崩**：`_dispatch_loop` 现在每个迭代独立 try/except，单条畸形固件输出再也不会让整个 task 沉默死掉。`@d:/Desktop/t3/host-pc/backend/app/core/robot.py:68-115`
3. **`Transport._publish` 死代码清理**：原来在工作线程上 `try/except QueueFull` 是不可达的（`call_soon_threadsafe` 仅排队），现在用 `_safe_put` helper 在 asyncio 线程内捕获。`@d:/Desktop/t3/host-pc/backend/app/core/transport.py:102-118`
4. **新增诊断 endpoint** `GET /api/diag/state` —— 返回 dispatcher 是否活、`_pending`/`_sub.queue` 深度、最后 30 行 RX log_tail，专为排查"所有命令一起超时"这种死锁。`@d:/Desktop/t3/host-pc/backend/app/api/robot.py:185-211`

#### #ONLINE / #GET_DCE "中毒"事件复盘（已彻底解决）

**症状**：发 `#ONLINE` 或 `#GET_DCE` 后，host-pc 端所有后续命令一齐超时几秒到十几秒，只能 disconnect+reconnect 唤醒。

**真凶在 host-pc** —— 不是固件。`@d:/Desktop/t3/host-pc/backend/app/core/transport.py:158-186` `SerialTransport._read_line` 实现有 bug：

```python
# 原实现（有 bug）
def _read_line(self):
    chunk = self._ser.read(256)
    if not chunk: return None       # ← 这里 return 掉，buf 里已有的完整行被搁置
    self._buf += chunk
    line, _, self._buf = self._buf.partition(b"\n")  # ← 只切第一行
    return decode(line)
```

固件 burst 发 8-15 行时，pyserial `read(256)` 一次就拿到全部，`buf` 里会有多个 `\n`。原代码 partition 第 1 行 publish 后 return；下一次迭代 `read()` 暂时无新字节 → 立即 return None → `buf` 里剩下 7-14 行**永远不被处理**。

host-pc 因而只看到 `Online OK all_online` 一行，后续 7 行 `J[n] OL` 全丢；dispatcher 等不到 tag 超时，**所有命令看起来一起卡死**。

**修复**：`_read_line` 优先消费 buf 里已有的完整行，再读串口。`@d:/Desktop/t3/host-pc/backend/app/core/transport.py:158-186`

**诊断工具**：`test/test_commands_gui.py` 直接打开串口、按 `\n` 切行，**bypass 整个 host-pc 路径**，用来分辨"固件没发"还是"host-pc 丢了"。

#### 顺带修的固件层加固（已烧录 REF 1.0）

虽然不是"中毒"的主因，但挖掘过程中发现固件有几个潜在隐患，已一并修掉：

| # | 文件 | 修改 | 性质 |
|---|---|---|---|
| 1 | `interface_can.cpp` `tx_error()` | 释放对应 CAN TX 信号量 | 防御 —— CAN 节点异常且连续 TERR 时不再死锁 |
| 2 | `interface_can.cpp` `tx_aborted_callback` | 释放信号量 | 防御 —— abort 路径也要平衡 acquire |
| 3 | `interface_can.cpp` `CanSendMessage` | `osWaitForever` → 100 ms | 兜底 —— 万一遗漏路径不至于永久卡死 ASCII thread |
| 4 | `ascii_processor.hpp` `Respond()` | `<text>+\r\n` 合并到一次 `process_bytes` | 减少 USB packet 数与竞态 |
| 5 | `interface_usb.cpp` `USBSender::process_packet` | 超时不再强行 `CDC_Transmit_FS`（会 BUSY + 错误 release 信号量） | 防御 |

#### 实机通过的命令一览（**19/19 PASS**）

| 类别 | 命令 |
|---|---|
| 连接 | `POST /api/connect`，`POST /api/disconnect`，`GET /api/connection` |
| 状态查询 | `#GETJPOS`，`#GETLPOS`，`#GETSPEED` |
| **多行** | `#ONLINE` → 8 行（`Online OK` + 7 × `J[n] OL`/`HAND[7]`）；`#GET_DCE` → 15 行（7 节点 × kp/kv/ki/kd + `GetDce OK`） |
| RGB | `!RGB_MODE 0/11`，`!RGB_CUSTOM r g b`，`!RGB_ON`/`!RGB_OFF` |
| 夹爪 | `!HD_EN`，`!HD_DIS`，`!HD_POS 0/50/100`，`!HD_O`，`!HD_C` |

验证脚本：`@d:/Desktop/t3/host-pc/backend/scripts/probe_postfix.py`（全绿）、`@d:/Desktop/t3/host-pc/backend/scripts/probe_clean2.py`（单 `#ONLINE` 0.2s 内拿到 8 行）。

后端 `pytest` 跑在最新代码上仍 **118/118 ✓**。

---

## 附录：ESP32 uart-bridge CLI（独立命令通道，不经 host-pc）

ESP32 固件自身的 CLI（串口控制台，WiFi 上/下都可用）已实现以下命令：

```
help | ? | h         显示帮助
info | banner        显示启动横幅
reboot               重启 ESP32
ll <0-4>             Mongoose 日志等级
wifi list / add / del / connect / <SSID> [PASS]
ls / cat / rm        SPIFFS 文件操作
```

实现位置：`@/D:/Desktop/t3/dummy/esp32-iot/firmware/examples/esp32/uart-bridge/main/cli.c`
