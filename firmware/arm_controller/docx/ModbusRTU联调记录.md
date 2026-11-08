# ModbusRTU 联调记录

本文记录 RA6M5 机械臂控制板作为 ModbusRTU 从站时的实测结果，供后续 vision board 对接使用。

## 1. 测试环境

```text
测试日期：2026-06-12
临时主站：PC USB-RS485 串口助手
从站设备：RA6M5 机械臂控制板
从站地址：1
总线参数：57600 baud, 8N1, ModbusRTU, CRC16
RS485 模式：半双工
```

当前系统角色：

```text
PC USB-RS485：临时主站，用于替代 vision board 测试
RA6M5 机械臂控制板：ModbusRTU 从站，地址固定为 1
ZDT 电机：规划从站地址 2
```

结论先行：

```text
PC 串口助手作为临时 Modbus 主站的验证已经通过。
机械臂控制板地址 1 的 04H / 06H / 10H 均已实测可用。
后续 vision board 修好后，只要生成同样格式的 ModbusRTU 请求，即可对接机械臂控制板。
```

## 2. 已验证功能清单

```text
1. 04H 读基础状态：通过
2. 04H 读当前轴值：通过
3. 04H 读动作状态：通过
4. 04H 读 last_command：通过
5. 06H 急停：通过
6. 06H 清急停：通过
7. 10H 写 raw us 目标：通过
8. 10H apply raw us：通过，机械臂实际动作
9. command_seq 重复去重：通过，result=9，机械臂不重复动作
10. 10H 回到复位姿态：通过，机械臂实际复位
11. 动作槽为空时播放动作：通过，result=6
12. HMI 录制动作后，Modbus 触发播放：通过，机械臂实际执行
13. HMI 加一步/删除/清空自动同步动作到 Data Flash：固件已实现，待烧录后验证
14. 上电自动加载 Data Flash 动作：固件已实现，待烧录后验证
```

## 3. 04H 读基础状态

请求：

```text
01 04 00 00 00 10 F1 C6
```

含义：

```text
slave=1
function=04H
start=0x0000
count=16
```

回复：

```text
01 04 20 A6 01 00 01 00 2B 00 00 00 06 01 06 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 46 3F
```

解析：

```text
0x0000 device_id           = 0xA601
0x0001 firmware_version    = 0x0001
0x0002 status_flags        = 0x002B
0x0003 error_code          = 0x0000
0x0004 servo_count         = 0x0006
0x0005 heartbeat_counter   = 0x0106，测试当时值，会变化
0x0006 comm_error_count    = 0x0000
0x0007 control_mode        = 0x0000
0x0008 protocol_version    = 0x0001
0x0009 last_command_seq    = 0x0000
0x000A last_command_code   = 0x0000
0x000B last_command_result = 0x0000
0x000C-0x000F reserved     = 0
```

`status_flags=0x002B`：

```text
bit0 ready = 1
bit1 enabled = 1
bit3 homed = 1
bit5 remote_modbus_active = 1
```

结论：

```text
基础状态区读取正常，机械臂板处于就绪状态。
```

## 4. 04H 读当前 6 个轴值

请求：

```text
01 04 00 20 00 06 71 C2
```

回复：

```text
01 04 0C 00 00 05 F7 FC 87 FC 7D 00 00 03 E8 27 02
```

解析：

```text
0x0020 J1 Base  = 0x0000 = 0.0 deg
0x0021 J2 Shldr = 0x05F7 = 1527 = 152.7 deg
0x0022 J3 Elbow = 0xFC87 = -889 = -88.9 deg
0x0023 J4 Pitch = 0xFC7D = -899 = -89.9 deg
0x0024 J5 Roll  = 0x0000 = 0.0 deg
0x0025 J6 Grip  = 0x03E8 = 1000 = 100.0%
```

说明：

```text
J1-J5 单位为 0.1 度，使用 int16 补码。
J6 单位为 0.1%，0=闭合，1000=100.0% 张开。
```

结论：

```text
当前轴值区读取正常，补码解析正常。
```

## 5. 04H 读动作状态

请求：

```text
01 04 00 60 00 05 30 17
```

回复：

```text
01 04 0A 00 00 00 00 00 00 00 00 00 00 D1 7D
```

解析：

```text
0x0060 action_slot         = 0
0x0061 action_step_count   = 0
0x0062 action_current_step = 0
0x0063 action_state        = 0，READY
0x0064 playback_progress   = 0
```

结论：

```text
动作状态区读取正常。当时未录制动作，所以 step_count=0。
```

## 6. 04H 读 last_command

请求：

```text
01 04 00 09 00 03 60 09
```

初始回复：

```text
01 04 06 00 00 00 00 00 00 60 93
```

解析：

```text
0x0009 last_command_seq    = 0
0x000A last_command_code   = 0
0x000B last_command_result = 0
```

结论：

```text
命令确认区读取正常。
```

## 7. 06H 急停

请求：

```text
01 06 00 F0 00 01 48 39
```

回复：

```text
01 06 00 F0 00 01 48 39
```

随后读 `last_command`：

```text
01 04 00 09 00 03 60 09
```

回复：

```text
01 04 06 00 00 00 01 00 01 F0 93
```

解析：

```text
last_command_seq    = 0
last_command_code   = 1，急停
last_command_result = 1，已接受
```

随后读基础状态：

```text
01 04 00 00 00 10 F1 C6
```

回复：

```text
01 04 20 A6 01 00 01 00 A2 00 00 00 06 15 E3 00 00 00 04 00 01 00 00 00 01 00 01 00 00 00 00 00 00 00 00 07 FD
```

关键解析：

```text
status_flags = 0x00A2
control_mode = 4
```

`status_flags=0x00A2`：

```text
bit1 enabled = 1
bit5 remote_modbus_active = 1
bit7 emergency_stop = 1
```

结论：

```text
06H 急停命令通信成功，业务状态也正确进入急停。
```

## 8. 06H 清除急停

请求：

```text
01 06 00 F0 00 02 08 38
```

回复：

```text
01 06 00 F0 00 02 08 38
```

随后读 `last_command`：

```text
01 04 00 09 00 03 60 09
```

回复：

```text
01 04 06 00 00 00 02 00 01 00 93
```

解析：

```text
last_command_code   = 2，清除急停
last_command_result = 1，已接受
```

随后读基础状态：

```text
01 04 00 00 00 10 F1 C6
```

回复：

```text
01 04 20 A6 01 00 01 00 23 00 00 00 06 23 0E 00 00 00 00 00 01 00 00 00 02 00 01 00 00 00 00 00 00 00 00 B7 CB
```

关键解析：

```text
status_flags = 0x0023
control_mode = 0
```

`status_flags=0x0023`：

```text
bit0 ready = 1
bit1 enabled = 1
bit5 remote_modbus_active = 1
bit7 emergency_stop = 0
```

结论：

```text
清除急停命令正常，机械臂恢复空闲/就绪状态。
```

## 9. 10H 写 raw us 目标

请求：

```text
01 10 01 20 00 06 0C 06 40 01 F4 01 F4 09 8B 05 B1 01 F4 F8 2E
```

含义：

```text
写 0x0120-0x0125 共 6 个 raw us 目标：
0x0120 = 1600us，Base
0x0121 = 500us，Shldr
0x0122 = 500us，Elbow
0x0123 = 2443us，Pitch
0x0124 = 1457us，Roll
0x0125 = 500us，Grip
```

回复：

```text
01 10 01 20 00 06 40 3D
```

结论：

```text
10H 批量写 raw us 目标区成功。
注意：这一步只是写目标，不会立刻动作。
```

## 10. 10H apply raw us

请求：

```text
01 10 00 F1 00 02 04 00 02 00 02 1D 26
```

含义：

```text
0x00F1 target_apply = 2，应用 raw us 目标
0x00F2 command_seq = 2
```

回复：

```text
01 10 00 F1 00 02 10 3B
```

debug 打印：

```text
MODBUS cmd seq=2 code=0x0102 result=1
```

实际现象：

```text
机械臂 Base 执行了小幅动作。
```

结论：

```text
raw us 远程控制链路打通：
Modbus -> target_apply -> arm_action_move_to_targets -> arm_control -> PCA9685 -> 舵机。
```

## 11. command_seq 去重

重复发送同一条 apply raw us：

```text
01 10 00 F1 00 02 04 00 02 00 02 1D 26
```

回复：

```text
01 10 00 F1 00 02 10 3B
```

debug 打印：

```text
MODBUS cmd seq=2 code=0x0102 result=9
```

实际现象：

```text
机械臂没有再次动作。
```

随后读 `last_command`：

```text
01 04 00 09 00 03 60 09
```

回复：

```text
01 04 06 00 02 01 02 00 09 79 69
```

解析：

```text
last_command_seq    = 2
last_command_code   = 0x0102，apply raw us
last_command_result = 9，重复命令已忽略
```

结论：

```text
去重机制通过。vision board 若超时重发同 seq + 同命令，机械臂不会重复执行动作。
```

## 12. 10H 回到复位姿态

请求：

```text
01 10 00 F0 00 03 06 00 09 00 00 00 03 7F 43
```

含义：

```text
0x00F0 control_command = 9，回到复位姿态
0x00F1 target_apply = 0
0x00F2 command_seq = 3
```

回复：

```text
01 10 00 F0 00 03 80 3B
```

debug 打印：

```text
MODBUS cmd seq=3 code=0x0009 result=1
```

实际现象：

```text
机械臂平滑回到当前保存的复位姿态。
```

结论：

```text
远程 home 命令可用。
```

## 13. 动作槽为空时播放动作

请求：

```text
01 10 00 F0 00 03 06 00 05 00 00 00 04 2E 80
```

含义：

```text
0x00F0 control_command = 5，播放动作 / NG 分拣
0x00F1 target_apply = 0
0x00F2 command_seq = 4
```

回复：

```text
01 10 00 F0 00 03 80 3B
```

debug 打印：

```text
MODBUS cmd seq=4 code=0x0005 result=6
```

实际现象：

```text
机械臂没有动作，因为当时还没有录制动作。
```

随后读 `last_command`：

```text
01 04 00 09 00 03 60 09
```

回复：

```text
01 04 06 00 04 00 05 00 06 01 50
```

解析：

```text
last_command_seq    = 4
last_command_code   = 5，播放动作
last_command_result = 6，动作槽为空
```

结论：

```text
业务拒绝不使用 Modbus exception，而是正常响应写请求，再通过 last_command_result 告诉主站动作槽为空。
vision board 应识别 result=6 为“机械臂无可播放 NG 动作”。
```

## 14. HMI 录制动作后，Modbus 触发播放

测试流程：

```text
1. 先在 HMI 动作页录制一个简单动作。
2. 再通过 PC USB-RS485 发送播放动作命令。
```

请求：

```text
01 10 00 F0 00 03 06 00 05 00 00 00 05 EF 40
```

含义：

```text
0x00F0 control_command = 5，播放动作 / NG 分拣
0x00F1 target_apply = 0
0x00F2 command_seq = 5
```

回复：

```text
01 10 00 F0 00 03 80 3B
```

实际现象：

```text
机械臂执行了刚刚通过 HMI 录制的动作。
```

结论：

```text
核心 NG 分拣链路实物验证通过：
HMI 录制动作 -> RA6M5 动作槽 -> vision board/PC 通过 Modbus control_command=5 触发播放 -> 机械臂执行动作。
```

## 15. 给 vision board 的对接规则

### 15.1 周期轮询基础状态

```text
04H slave=1 start=0x0000 count=16
```

建议检查：

```text
0x0002 status_flags
0x0003 error_code
0x0007 control_mode
0x0009 last_command_seq
0x000A last_command_code
0x000B last_command_result
```

### 15.2 可选读取当前轴值

```text
04H slave=1 start=0x0020 count=6
```

### 15.3 可选读取动作状态

```text
04H slave=1 start=0x0060 count=5
```

### 15.4 判断是否可以下发 NG 分拣

检查 `0x0002 status_flags`：

```text
ready bit0 = 1
error bit6 = 0
emergency_stop bit7 = 0
local_hmi_active bit4 = 0
```

### 15.5 下发 NG 分拣动作

```text
10H slave=1 start=0x00F0 count=3 values=[5, 0, command_seq++]
```

### 15.6 下发后读取确认

```text
04H slave=1 start=0x0009 count=3
```

根据 `last_command_result` 判断：

| 值 | 含义 |
|---:|---|
| `1` | 已接受 |
| `2` | 忙，拒绝 |
| `4` | 急停中，拒绝 |
| `5` | 本地 HMI 占用，拒绝 |
| `6` | 动作槽为空 |
| `7` | 执行失败 |
| `9` | 重复命令已忽略 |

### 15.7 等待动作完成

动作执行期间继续轮询：

```text
04H slave=1 start=0x0060 count=5
```

或者读取基础状态里的：

```text
0x0002 status_flags moving/action_playing
```

等待机械臂回到：

```text
ready = 1
moving = 0
action_playing = 0
```

再进入下一轮检测/分拣。

## 16. 新增：动作保存到 Data Flash

本轮固件新增能力：

```text
HMI 录制动作后，动作仍先进入 RAM 动作槽。
HMI 加一步、删除最后一步、清空动作成功后，RA6M5 会立即把当前 RAM 动作槽同步写入 Data Flash。
下次上电时，arm_action_init() 自动加载 Data Flash 中的动作。
vision board 后续触发 NG 分拣时，只需要继续发送播放动作命令。
```

### 16.1 远程强制同步动作命令

请求：

```text
01 10 00 F0 00 03 06 00 07 00 00 00 06 D6 81
```

含义：

```text
0x00F0 control_command = 7，强制同步当前 RAM 动作到 Data Flash
0x00F1 target_apply = 0
0x00F2 command_seq = 6
```

期望回复：

```text
01 10 00 F0 00 03 80 3B
```

随后读取 `last_command`：

```text
01 04 00 09 00 03 60 09
```

期望：

```text
last_command_seq    = 6
last_command_code   = 7，强制同步动作
last_command_result = 1，已接受
```

如果 RAM 动作槽为空，也允许保存。这表示保存“0 步动作”，用于明确清空 Flash 中的旧动作：

```text
last_command_result = 1，已接受
```

### 16.2 断电重启后验证动作仍存在

通过 HMI 加一步后，或通过 `control_command=7` 强制同步成功后，重新上电或复位 RA6M5，然后读取动作状态：

```text
01 04 00 60 00 05 30 17
```

期望：

```text
0x0061 action_step_count > 0
0x0063 action_state = 0，READY
```

debug 口期望看到类似：

```text
arm action loaded from flash count=...
```

### 16.3 播放已保存动作

请求：

```text
01 10 00 F0 00 03 06 00 05 00 00 00 07 6E 81
```

含义：

```text
0x00F0 control_command = 5，播放动作 / NG 分拣
0x00F1 target_apply = 0
0x00F2 command_seq = 7
```

期望回复：

```text
01 10 00 F0 00 03 80 3B
```

随后读 `last_command`，期望：

```text
last_command_seq    = 7
last_command_code   = 5
last_command_result = 1
```

实际现象应为：

```text
机械臂执行保存过的 NG 分拣动作。
```

### 16.4 给 vision board 的新增规则

vision board 不需要关心动作如何存在 RA6M5 内部，只需要知道两条任务级命令：

```text
远程强制同步当前动作槽到 Data Flash，维护/调试可选：
10H slave=1 start=0x00F0 count=3 values=[7, 0, command_seq++]

执行 NG 分拣动作：
10H slave=1 start=0x00F0 count=3 values=[5, 0, command_seq++]
```

正常 HMI 录制不需要单独保存按钮，因为加一步、删除最后一步、清空动作成功后已经自动写入 Data Flash。
`control_command=7` 通常只由调试阶段或维护阶段触发，不建议在每次分拣循环中频繁写入，避免不必要的 Data Flash 擦写。

播放动作仍按原来的确认流程：

```text
04H slave=1 start=0x0009 count=3
04H slave=1 start=0x0060 count=5
```

## 17. 当前状态和下一步

当前已验证：

```text
RS485/ModbusRTU 从站地址 1 可通信
04H/06H/10H 功能可用
command_seq 去重有效
远程播放 HMI 录制动作可用
```

新增待验证：

```text
HMI 加一步/删除/清空自动保存到 Data Flash
上电自动加载已保存动作
断电后再次播放已保存动作
```

下一步：

```text
烧录本轮固件后，按第 16 节测试动作自动保存和断电加载。
测试通过后，vision board 可以按第 16.4 节接入任务级命令。
```
