# ModbusRTU 教程与 RA6M5 机械臂项目实践

本文用于帮助理解 ModbusRTU 的基本原理、真实工程中的设计方法，以及本工程 `Robotic_Arm_Control` 为什么这样设计 RS485/Modbus 从站协议。

阅读目标：

- 知道 ModbusRTU 是什么，和 RS485 是什么关系。
- 知道主站、从站各自应该怎么设计。
- 知道常用功能码应该如何使用，什么时候用 `03H`、`04H`、`06H`、`10H`。
- 知道嵌入式板子做 Modbus 设备时，和 PLC 有什么区别。
- 知道一个新项目如果准备用 Modbus 通信，应该如何规划寄存器表、命令、状态、错误码和测试流程。
- 能理解当前 RA6M5 机械臂控制板协议，并能用 PC 串口助手临时代替 vision board 进行测试。

## 1. 先建立整体概念

Modbus 是一种工业通信协议。它最早是围绕 PLC 场景发展起来的，后来因为简单、公开、容易实现，在电机驱动器、温控器、传感器、采集模块、变频器、仪表、嵌入式控制板中都非常常见。

ModbusRTU 是 Modbus 在串口总线上的二进制帧格式。常见组合是：

```text
RS485 电气层 + ModbusRTU 协议层
```

要注意这两个概念不是一回事：

| 名称 | 解决的问题 | 类比 |
|---|---|---|
| RS485 | 电气信号怎么在两根线上传输，支持差分、远距离、多节点 | 路 |
| UART | 串口字节怎么发，波特率、数据位、停止位、校验位 | 车道规则 |
| ModbusRTU | 字节流里每一帧是什么意思，地址、功能码、寄存器、CRC | 交通语言 |

所以不能说“RS485 协议”就等于 Modbus。RS485 上可以跑 ModbusRTU，也可以跑自定义协议、Profibus、DMX512 等。我们这个机械臂项目选择的是：

```text
57600 baud
8N1
RS485 半双工
ModbusRTU
CRC16
```

## 2. 主站和从站

ModbusRTU 总线上通常是：

```text
一个主站 + 多个从站
```

主站负责发起所有通信。从站只能回答主站，不能自己主动抢总线说话。

本工程里的角色是：

```text
vision board / PC USB-RS485  = 主站
RA6M5 机械臂控制板            = 从站，地址 1
ZDT 电机                      = 从站，地址 2
```

当 vision board 还没有修好时，可以用 PC 串口助手或 Modbus Poll 临时代替 vision board。只要 PC 发出的 ModbusRTU 帧和 vision board 将来发的一样，那么 PC 测通就代表机械臂控制板的从站协议基本可用。

数据流如下：

```text
PC 串口助手 / vision board
        |
        |  ModbusRTU 请求帧
        v
RS485 总线
        |
        v
RA6M5 机械臂控制板，slave=1
        |
        |  解析功能码和寄存器
        v
arm_control / arm_action / arm_kinematics
        |
        v
PCA9685
        |
        v
舵机
```

关键原则：

```text
机械臂控制板是从站，只响应主站请求，不主动发数据。
```

如果多个从站都主动发数据，RS485 半双工总线会冲突，主站也无法可靠解析。

## 3. ModbusRTU 帧格式

ModbusRTU 的一帧一般是：

```text
从站地址 | 功能码 | 数据区 | CRC16低字节 | CRC16高字节
```

例如读 16 个 input register：

```text
01 04 00 00 00 10 F1 C6
```

逐字节解释：

| 字节 | 含义 |
|---|---|
| `01` | 从站地址 1 |
| `04` | 功能码，读 input registers |
| `00 00` | 起始寄存器地址 `0x0000` |
| `00 10` | 读取数量 `16` 个寄存器 |
| `F1 C6` | CRC16，低字节在前，高字节在后 |

### 3.1 大端和小端

Modbus 寄存器本身是 16 位。寄存器地址、寄存器数量、寄存器值，在帧里通常是高字节在前：

```text
0x1234 发送为 12 34
```

但是 CRC16 是低字节在前：

```text
CRC = 0xC6F1
发送为 F1 C6
```

这是新手最容易错的地方之一。

### 3.2 寄存器是 16 位

Modbus 的 register 是 16 位单位：

```text
1 个 register = 2 个字节 = uint16
```

如果要传 32 位整数、float、double、字符串，就需要占用多个寄存器。不同厂家的 32 位字节序可能不一样，所以如果项目里要用 32 位数据，一定要在协议文档里明确：

```text
高字寄存器在前还是低字寄存器在前
每个寄存器内部高字节在前
是否使用 IEEE754 float
```

当前机械臂项目尽量用 16 位寄存器，减少歧义。

## 4. Modbus 的四类数据区

标准 Modbus 里常见四类对象：

| 类型 | 常见读写 | 典型含义 |
|---|---|---|
| Coils | 读写单 bit | 开关量输出 |
| Discrete Inputs | 只读单 bit | 开关量输入 |
| Input Registers | 只读 16 位寄存器 | 运行状态、测量值 |
| Holding Registers | 可读写 16 位寄存器 | 参数、目标值、控制命令 |

很多 PLC、仪表、变频器会使用这种模型。比如：

```text
线圈：继电器输出
离散输入：限位开关
输入寄存器：温度、电流、速度
保持寄存器：目标频率、PID 参数、控制命令
```

但是在嵌入式自定义设备里，不一定四类都要用全。常见做法是：

```text
04H input registers  = 只读状态
06H / 10H holding registers = 写控制和目标
```

当前机械臂项目就是这样设计的。

## 5. 常用功能码怎么理解

功能码可以理解为 Modbus 的“标准动词”。寄存器地址才是你的项目自定义内容。

非常重要：

```text
不要把功能码当成你的业务命令 ID 随便自定义。
```

也就是说，不要设计成：

```text
0x81 = 机械臂复位
0x82 = 播放动作
0x83 = 设置舵机
```



这会变成私有协议，不利于使用标准 Modbus 工具，也不利于 vision board 使用统一主站逻辑。

更推荐：

```text
06H / 10H 写标准 holding register
然后用寄存器值表达业务命令
```

例如本工程：

```text
写 0x00F0 = 9 表示回到复位姿态
写 0x00F0 = 5 表示播放 NG 分拣动作
```

### 5.1 常用功能码表

| 功能码 | 名称 | 常见用途 | 本项目是否使用 |
|---:|---|---|---|
| `01H` | Read Coils | 读多个线圈 bit | 暂不使用 |
| `02H` | Read Discrete Inputs | 读多个离散输入 bit | 暂不使用 |
| `03H` | Read Holding Registers | 读可读写寄存器 | 可选调试，不作为正式状态轮询 |
| `04H` | Read Input Registers | 读只读状态寄存器 | 正式使用 |
| `05H` | Write Single Coil | 写单个线圈 bit | 暂不使用 |
| `06H` | Write Single Register | 写单个 holding register | 正式使用 |
| `0FH` | Write Multiple Coils | 写多个线圈 bit | 暂不使用 |
| `10H` | Write Multiple Registers | 写多个 holding registers | 正式使用 |
| `16H` | Mask Write Register | 按位修改寄存器 | 一般少用，暂不使用 |

工程中最常见的三件事：

```text
读状态：04H
写单个命令：06H
写一组目标值：10H
```

## 6. PLC 和嵌入式板子的区别

### 6.1 PLC 常见特点

PLC 的 Modbus 映射通常比较固定：

- 线圈、输入、输入寄存器、保持寄存器区分明显。
- 很多资料会用 `00001`、`10001`、`30001`、`40001` 这种地址表示法。
- 通信周期通常和 PLC 扫描周期、上位机轮询周期配合。
- 状态量和控制量往往和 PLC 内部寄存器直接对应。

比如某个 PLC 文档可能写：

```text
40001 = 目标速度
40002 = 启停控制
30001 = 当前速度
30002 = 错误码
```

这里的 `40001` 是一种人类显示用地址，不一定等于协议帧里的 `0x0000`。

### 6.2 嵌入式板子常见特点

嵌入式板子没有 PLC 那么固定的寄存器模型，但如果要做标准 Modbus 设备，仍然应该遵守这些原则：

1. 功能码使用标准功能码。
2. 业务命令放到寄存器值里，而不是乱造功能码。
3. 只读状态和写控制分开。
4. 寄存器表稳定，不要频繁改地址。
5. 每个寄存器的单位、符号、有无缩放都写清楚。
6. 控制命令不要直接在通信中断里执行。
7. 批量写命令要先完整校验，再统一执行。
8. 要有命令执行结果寄存器，方便主站确认。

嵌入式板子更灵活，也更容易乱。最容易出现的问题是：

```text
今天加一个寄存器，明天改一个地址，后天主站就对不上了。
```

所以一开始就要把寄存器分区设计好。

## 7. 如果一个新项目要用 Modbus，应该怎么设计

建议按下面流程做。

### 7.1 先确定通信角色

先回答：

```text
谁是主站？
谁是从站？
从站地址分别是多少？
总线是否还有其它设备？
是否允许广播地址 0？
```

本项目：

```text
vision board / PC USB-RS485 = 主站
机械臂控制板 = 从站地址 1
ZDT 电机 = 从站地址 2
广播地址 0 暂不依赖
```

### 7.2 再确定串口参数

必须统一：

```text
波特率
数据位
校验位
停止位
CRC
```

本项目：

```text
57600
8N1
Modbus CRC16
```

### 7.3 决定使用哪些功能码

不要一上来支持一堆功能码。对嵌入式设备来说，支持最小但清晰的一组更好。

推荐：

```text
04H：读运行状态
06H：写单个安全控制命令
10H：写多个目标值或一次性命令
```



如果需要兼容通用工具，可以额外支持：

```text
03H：读 holding registers
```



但要明确正式主站到底使用哪个功能码读状态。

### 7.4 设计寄存器分区

不要所有东西堆在一起。推荐按功能分区：

```text
0x0000 - 0x001F：设备基础状态，只读
0x0020 - 0x005F：通道状态，只读
0x0060 - 0x009F：动作/任务状态，只读
0x00A0 - 0x00EF：配置参数，可读写
0x00F0 - 0x00FF：控制命令，写入触发
0x0100 - 0x013F：目标值，写入控制
```

这样后续扩展时不容易乱。

### 7.5 每个寄存器必须定义这些内容

一个寄存器表不应该只写地址和名字，还要写清楚：

```text
地址
读写权限
功能码
数据类型
单位
缩放比例
有符号还是无符号
默认值
合法范围
写入后是否立即生效
异常或拒绝时如何反馈
```

例如：

```text
0x0020
名称：J1 当前角度
权限：只读
功能码：04H
类型：int16
单位：0.1 degree
范围：-900 到 +900
说明：值 123 表示 12.3 度，值 0xFF9C 表示 -10.0 度
```

### 7.6 控制命令要和状态确认配套

主站写命令以后，一定需要知道：

```text
从站收到没有？
命令接受没有？
命令是否正在执行？
执行完成没有？
有没有错误？
```

所以不要只设计“写命令寄存器”，还要设计：

```text
last_command_seq
last_command_code
last_command_result
status_flags
action_state
error_code
```

这就是本项目为什么加 `0x0009-0x000B`。

## 8. 主站应该怎么设计

主站不是简单地“想发就发”。一个可靠主站应该有调度器。

### 8.1 主站基本职责

```text
轮询从站状态
按顺序访问不同从站
控制总线同一时刻只有一个请求
发送请求后等待响应
超时后重试
统计 CRC 错误、超时、解析错误
根据业务状态决定是否下发命令
```

本系统里 vision board 后续应该做：

```text
1. 轮询机械臂 slave=1
2. 轮询 ZDT 电机 slave=2
3. 根据视觉检测结果决定是否让机械臂执行 NG 分拣
4. 机械臂 busy 时不再重复下发新任务
5. 等机械臂 ready 后进入下一轮
```

### 8.2 主站轮询建议

不要一次把所有寄存器从 `0x0000` 读到很远。这样效率低，也容易跨过未定义区域。

推荐分块：

```text
04H slave=1 start=0x0000 count=16  基础状态
04H slave=1 start=0x0020 count=6   当前轴值
04H slave=1 start=0x0060 count=5   动作状态
```



### 8.3 主站发送命令建议

一次性命令要带 `command_seq`：

```text
command_seq 每发一次新命令就加 1
```

主站发命令后，不要只看 Modbus 写响应，还要再读：

```text
0x0009 last_command_seq
0x000A last_command_code
0x000B last_command_result
```

这样才能区分：

```text
通信成功但业务拒绝
通信失败
重复命令被忽略
动作槽为空
急停中拒绝
```

## 9. 从站应该怎么设计

### 9.1 从站必须被动响应

ModbusRTU 从站的基本流程：

```text
接收字节
判断一帧结束
检查地址
检查 CRC
检查功能码
检查寄存器地址和数量
读或写内部寄存器模型
生成响应
发送响应
```

如果地址不是自己的，从站应该直接丢弃，不回复。

如果 CRC 错，从站应该丢弃，不回复。

如果地址是自己的、CRC 正确，但功能码或地址非法，才回复 Modbus exception。

### 9.2 从站不要在中断里执行复杂业务

串口中断里应该尽量只做：

```text
收到字节
放进 ringbuffer
记录错误事件
```

不要在串口中断里做：

```text
解析完整协议
控制舵机
写 Flash
跑运动学
打印大量日志
```

当前项目就是：

```text
SCI3 回调 -> 字节放 ringbuffer
modbus_slave_task() -> 5ms 调度里解析帧
```

### 9.3 10H 必须原子执行

`10H` 是写多个寄存器。如果边解析边执行，可能出现半包命令。

错误做法：

```text
收到 0x00F0 = 5
马上开始播放动作
后面的 command_seq 还没处理
```

正确做法：

```text
先完整收帧
检查 CRC
检查地址
检查 count
检查 byte_count
检查所有参数范围
全部通过后，批量更新 shadow
最后统一触发业务动作
```

当前项目按这个方式做。

### 9.4 协议错误和业务拒绝要分开

协议错误是：

```text
功能码不支持
寄存器地址不存在
数量非法
byte_count 不对
写只读区
```

这些应该返回 Modbus exception。

业务拒绝是：

```text
机械臂正在动作，暂时忙
HMI 手动页面正在占用
急停中
动作槽为空
命令重复
功能暂未实现
```

这些不应该返回 Modbus exception。因为通信本身是成功的，只是业务状态不允许执行。

本项目用：

```text
last_command_result
```

告诉主站业务结果。

## 10. Modbus exception 是什么

如果从站收到合法地址、CRC 正确的请求，但无法按协议处理，会返回异常响应。

异常响应格式：

```text
从站地址 | 功能码+0x80 | 异常码 | CRC
```

例如主站发 `04H`，从站返回异常时功能码变成：

```text
0x84
```

常见异常码：

| 异常码 | 含义 | 典型原因 |
|---:|---|---|
| `01H` | Illegal Function | 功能码不支持 |
| `02H` | Illegal Data Address | 寄存器地址不支持 |
| `03H` | Illegal Data Value | 数量、长度、参数值非法 |

注意：

```text
CRC 错误不返回 exception，而是直接丢弃。
```

因为 CRC 错时，从站不能确定这一帧到底是不是发给自己的，也不能保证响应不会造成更严重的总线混乱。

## 11. RTU 帧结束怎么判断

ModbusRTU 没有像 `FF FF FF` 这样的固定帧尾。它靠总线空闲时间判断一帧结束。

标准思路：

```text
一帧内部字节应该连续发送
帧和帧之间需要有一段静默时间
```

常见规则：

```text
超过 3.5 个字符时间的静默，认为一帧结束
超过 1.5 个字符时间的帧内间隔，可能认为帧错误
```

对于 57600 8N1：

```text
1 个字符大约 10 bit
1 个字符时间约 10 / 57600 = 0.174 ms
3.5 个字符约 0.61 ms
```

当前项目使用 5ms 超时作为帧结束判断，比较保守，优点是实现简单、对普通串口助手友好。缺点是响应会比严格 3.5 字符时间慢一点，但对机械臂控制这种轮询周期来说可以接受。

## 12. 本工程 Modbus 协议设计

### 12.1 基本参数

```text
协议：ModbusRTU
物理层：RS485 半双工
波特率：57600
串口格式：8N1
CRC：Modbus CRC16
机械臂从站地址：1
ZDT 电机从站地址：2
```

### 12.2 支持功能码

```text
04H：读 input registers，用于状态读取
06H：写单个 holding register，用于急停/清急停
10H：写多个 holding registers，用于命令、目标值、command_seq
```

暂时不正式使用：

```text
03H：读 holding registers
```

因为 vision board 正式状态读取统一使用 `04H`。

### 12.3 Input Registers

基础状态区：

| 地址 | 名称 | 含义 |
|---:|---|---|
| `0x0000` | `device_id` | 固定 `0xA601` |
| `0x0001` | `firmware_version` | 固件版本 |
| `0x0002` | `status_flags` | 状态标志位 |
| `0x0003` | `error_code` | 当前错误码 |
| `0x0004` | `servo_count` | 舵机数量，当前 `6` |
| `0x0005` | `heartbeat_counter` | 心跳计数 |
| `0x0006` | `comm_error_count` | 通信错误计数 |
| `0x0007` | `control_mode` | 当前控制模式 |
| `0x0008` | `protocol_version` | 协议版本 |
| `0x0009` | `last_command_seq` | 最近处理命令序号 |
| `0x000A` | `last_command_code` | 最近处理命令类型 |
| `0x000B` | `last_command_result` | 最近命令结果 |
| `0x000C-0x000F` | `reserved` | 保留，读 0 |

舵机状态区：

| 地址 | 名称 | 含义 |
|---:|---|---|
| `0x0020-0x0025` | 当前轴值 | J1-J5 为 `0.1 度`，J6 为 `0.1%` |
| `0x0030-0x0035` | 当前 us | 当前 PWM 高电平时间 |
| `0x0040-0x0045` | 目标轴值 | 单位同当前轴值 |
| `0x0050-0x0055` | 目标 us | 目标 PWM 高电平时间 |

动作状态区：

| 地址 | 名称 | 含义 |
|---:|---|---|
| `0x0060` | `action_slot` | 当前动作槽 |
| `0x0061` | `action_step_count` | 动作总步数 |
| `0x0062` | `action_current_step` | 当前播放步 |
| `0x0063` | `action_state` | 动作状态 |
| `0x0064` | `playback_progress` | 播放进度，`0-1000` |

### 12.4 Holding Registers

控制命令区：

| 地址 | 名称 | 含义 |
|---:|---|---|
| `0x00F0` | `control_command` | 一次性控制命令 |
| `0x00F1` | `target_apply` | 应用目标触发 |
| `0x00F2` | `command_seq` | 命令序号 |

目标值区：

| 地址 | 名称 | 含义 |
|---:|---|---|
| `0x0100-0x0105` | `axis_target_value` | 6 个目标轴值 |
| `0x0120-0x0125` | `raw_target_us` | 6 个 raw us 调试目标 |

### 12.5 status_flags

`0x0002 status_flags`：

| bit | 含义 |
|---:|---|
| 0 | ready |
| 1 | enabled |
| 2 | moving |
| 3 | homed |
| 4 | local_hmi_active |
| 5 | remote_modbus_active |
| 6 | error |
| 7 | emergency_stop |
| 8 | action_recording |
| 9 | action_playing |

vision board 判断是否能下发任务时，至少应该检查：

```text
ready = 1
error = 0
emergency_stop = 0
local_hmi_active = 0
```

### 12.6 control_mode

`0x0007 control_mode`：

| 值 | 含义 |
|---:|---|
| 0 | 空闲 |
| 1 | 手动 |
| 2 | 记录 |
| 3 | 回放 |
| 4 | 急停 |

### 12.7 control_command

`0x00F0 control_command`：

| 值 | 命令 |
|---:|---|
| 1 | 急停 |
| 2 | 清除急停 |
| 3 | 开始记录，当前未实现 |
| 4 | 停止记录，当前未实现 |
| 5 | 开始回放 / NG 分拣动作 |
| 6 | 停止回放 |
| 7 | 强制同步当前 RAM 动作到 Data Flash，维护/调试用 |
| 8 | 清除动作 |
| 9 | 回到复位姿态 |

### 12.8 target_apply

`0x00F1 target_apply`：

| 值 | 含义 |
|---:|---|
| 1 | 应用 `0x0100-0x0105` 的轴目标 |
| 2 | 应用 `0x0120-0x0125` 的 raw us 目标 |

`raw us` 是调试入口，不是底层旁路。它仍然必须经过：

```text
范围检查
arm_action_move_to_targets()
arm_control_set_target_us()
PCA9685
```

不能在 Modbus 模块里直接写 PCA9685。

## 13. command_seq 去重机制

Modbus 主站有重试机制。例如主站发了播放动作命令，但响应丢了，主站可能会再发一次同样的命令。

如果从站不去重，就可能导致：

```text
同一个抓取动作被执行两次
同一个清空动作被执行两次
同一个保存动作被执行两次
```

所以本项目规定：

```text
去重键 = command_seq + command_code
```

`command_code` 规则：

```text
control_command 触发时：command_code = control_command
target_apply=1 时：command_code = 0x0101
target_apply=2 时：command_code = 0x0102
```

如果收到重复的：

```text
same command_seq
same command_code
```

从站不重复执行，并设置：

```text
last_command_result = 9
```

## 14. last_command_result

`0x000B last_command_result`：

| 值 | 含义 |
|---:|---|
| 0 | 无 |
| 1 | 已接受 |
| 2 | 忙，拒绝 |
| 3 | 参数非法 |
| 4 | 急停中，拒绝 |
| 5 | 本地 HMI 占用，拒绝 |
| 6 | 动作槽为空 |
| 7 | 执行失败 |
| 8 | 功能未实现 |
| 9 | 重复命令已忽略 |

主站下发命令后，不能只看写响应，还应该读：

```text
04H slave=1 start=0x0009 count=3
```

确认：

```text
last_command_seq
last_command_code
last_command_result
```

## 15. 本项目为什么不用 HMI 的 A5 帧作为 485 协议

HMI 本地串口协议类似：

```text
A5 01 ch us_l us_h FF FF FF
```

这种协议适合串口屏本地控制，但不适合作为 RS485 总线正式协议。

原因：

1. 它不是标准 Modbus，通用 Modbus 工具不能直接测。
2. vision board 已经要控制 ZDT 电机，ZDT 是 Modbus 风格。
3. RS485 总线上有多个设备，需要统一地址、CRC、功能码、异常处理。
4. 机械臂需要状态轮询、错误码、命令确认，简单 A5 帧不够规范。
5. 后续 PC、vision board、机械臂之间要形成闭环，标准寄存器模型更清晰。

所以最终方案是：

```text
本地 HMI：继续使用 A5 本地串口协议
远程 RS485：统一使用 ModbusRTU
```

两者最终都进入同一套机械臂控制层。

## 16. PC 串口助手测试流程

### 16.1 硬件连接

```text
PC USB-RS485 A  -> 机械臂控制板 RS485 A
PC USB-RS485 B  -> 机械臂控制板 RS485 B
GND 建议共地
```

串口助手设置：

```text
波特率：57600
数据位：8
校验位：None
停止位：1
发送模式：HEX
接收模式：HEX
不要自动追加回车换行
```

必须用 HEX 发送。不要把：

```text
01 04 00 00 00 10 F1 C6
```

当普通字符串发出去。

### 16.2 上电 debug 口检查

debug 串口应该看到：

```text
rs485 uart open ok
modbus slave ready: addr=1 baud=57600
```

### 16.3 读基础状态

发送：

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

期望响应开头类似：

```text
01 04 20 A6 01 ...
```

其中：

```text
01      从站地址
04      功能码
20      返回 32 字节数据
A6 01   device_id = 0xA601
```

### 16.4 读 last_command

发送：

```text
01 04 00 09 00 03 60 09
```

读取：

```text
0x0009 last_command_seq
0x000A last_command_code
0x000B last_command_result
```

### 16.5 读当前轴值

发送：

```text
01 04 00 20 00 06 71 C2
```

读取：

```text
J1-J5 当前角度，单位 0.1 度
J6 夹爪开合度，单位 0.1%
```

负数使用 16 位补码。

例如：

```text
FF 9C = -100 = -10.0 度
```

### 16.6 读动作状态

发送：

```text
01 04 00 60 00 05 30 17
```

读取：

```text
action_slot
action_step_count
action_current_step
action_state
playback_progress
```

### 16.7 急停

发送：

```text
01 06 00 F0 00 01 48 39
```

如果正常，从站会回复同样的写入响应。

然后读 last_command：

```text
01 04 00 09 00 03 60 09
```

应看到：

```text
last_command_code = 1
last_command_result = 1
```

### 16.8 清除急停

发送：

```text
01 06 00 F0 00 02 08 38
```

然后读状态确认：

```text
emergency_stop bit 清除
control_mode 不再是 4
```

### 16.9 raw us 小幅度控制 Base

第一帧：写 6 路 raw us 目标。

```text
01 10 01 20 00 06 0C 06 40 01 F4 01 F4 09 8B 05 B1 01 F4 F8 2E
```

含义：

```text
0x0120 = 1600us  Base
0x0121 = 500us   Shldr
0x0122 = 500us   Elbow
0x0123 = 2443us  Pitch
0x0124 = 1457us  Roll
0x0125 = 500us   Grip
```

第二帧：应用 raw us，`seq=2`。

```text
01 10 00 F1 00 02 04 00 02 00 02 1D 26
```

期望：

```text
Base 平滑移动到 1600us
debug 打印 MODBUS cmd seq=2 code=0x0102 result=1
```

### 16.10 重复命令测试

再次发送同一帧：

```text
01 10 00 F1 00 02 04 00 02 00 02 1D 26
```

因为 `seq=2` 且 `command_code=0x0102` 已经执行过，所以不应重复执行。

再读：

```text
01 04 00 09 00 03 60 09
```

应看到：

```text
last_command_result = 9
```

## 17. vision board 后续对接流程

vision board 修好后，可以完全替代 PC 串口助手。

### 17.1 周期轮询

建议：

```text
04H slave=1 start=0x0000 count=16
04H slave=1 start=0x0020 count=6
04H slave=1 start=0x0060 count=5
```

### 17.2 判断是否可接收任务

vision board 应检查：

```text
ready = 1
error = 0
emergency_stop = 0
local_hmi_active = 0
```

如果机械臂正在手动控制、正在动作、急停中或报错，就不要下发 NG 分拣任务。

### 17.3 保存 NG 分拣动作

HMI 正常录制动作时，不需要单独的保存按钮。当前固件规定：

```text
HMI 加一步成功 -> RAM 动作槽更新 -> 自动同步写入 Data Flash
HMI 删除最后一步成功 -> RAM 动作槽更新 -> 自动同步写入 Data Flash
HMI 清空动作成功 -> RAM 动作槽变为 0 步 -> 自动同步写入 Data Flash
```

因此，正常调试流程是直接在动作页加一步、删除或清空，断电重启后 `arm_action_init()` 会自动加载最后一次保存的动作槽。

如果后续 PC 或 vision board 做维护工具，也可以让 RA6M5 强制把当前 RAM 动作槽同步到 Data Flash：

```text
10H slave=1 start=0x00F0 count=3 values=[7, 0, command_seq++]
```

含义：

```text
0x00F0 = 7    强制同步动作
0x00F1 = 0    不应用目标
0x00F2 = seq  命令序号
```

同步成功后，断电重启时 `arm_action_init()` 会自动加载这个动作。后续 vision board 触发 NG 分拣时，不需要重新录制动作。

强制同步后建议读取：

```text
04H slave=1 start=0x0009 count=3
```

确认：

```text
last_command_seq == 刚刚发送的 seq
last_command_code == 7
last_command_result == 1
```

如果当前 RAM 动作槽为空，`control_command=7` 也允许执行，含义是保存“0 步动作”。这可以用于把 Flash 里的旧动作明确清掉。

### 17.4 触发 NG 分拣动作

写：

```text
10H slave=1 start=0x00F0 count=3 values=[5, 0, command_seq++]
```

含义：

```text
0x00F0 = 5    播放动作
0x00F1 = 0    不应用目标
0x00F2 = seq  命令序号
```

### 17.5 确认命令结果

读：

```text
04H slave=1 start=0x0009 count=3
```

确认：

```text
last_command_seq == 刚刚发送的 seq
last_command_code == 5
last_command_result == 1
```

### 17.6 等待动作完成

继续轮询：

```text
0x0002 status_flags
0x0063 action_state
0x0064 playback_progress
```

直到：

```text
ready = 1
action_state = done 或 ready
moving = 0
```

再允许下一轮视觉检测结果触发机械臂。

## 18. CRC16 计算思路

ModbusRTU CRC16 常见初值：

```text
0xFFFF
```

多项式反向形式：

```text
0xA001
```

伪代码：

```c
uint16_t crc = 0xFFFF;

for each byte:
    crc ^= byte;
    repeat 8 times:
        if (crc & 0x0001):
            crc = (crc >> 1) ^ 0xA001;
        else:
            crc = crc >> 1;

发送时：
    先发 crc 低字节
    再发 crc 高字节
```

本项目 `modbus_slave_app.c` 内部也是这个算法。

## 19. 常见坑

### 19.1 把 HEX 当字符串发送

错误：

```text
串口助手发送 ASCII 字符 '0' '1' ' ' '0' '4'
```

正确：

```text
HEX 模式发送字节 0x01 0x04 ...
```

### 19.2 CRC 高低字节反了

寄存器字段通常高字节在前，CRC 却是低字节在前。

### 19.3 40001 地址理解错

有些工具显示 `40001`，但实际协议帧地址可能是 `0x0000`。不同工具有“地址从 1 开始”或“地址从 0 开始”的差异。

所以测试时要确认工具设置。

本项目文档里的地址直接使用协议帧地址：

```text
0x0000
0x00F0
0x0100
```

### 19.4 从站主动发数据

ModbusRTU 从站不应该主动发数据。所有状态都等主站来读。

### 19.5 多个任务抢 RS485 发送

同一个从站里，不要让多个任务都能直接发 RS485。

推荐：

```text
只有 Modbus 从站模块能发 RS485 响应
其它模块只更新状态寄存器模型
```

### 19.6 写命令后不读结果

Modbus 写响应只能说明通信层收到并处理了写请求，不代表业务一定执行成功。

所以主站要读：

```text
last_command_seq
last_command_code
last_command_result
```

### 19.7 业务拒绝用 Modbus exception

不推荐把“机械臂忙”“动作槽为空”“HMI 占用”做成 Modbus exception。

这些属于业务状态，应该正常响应写请求，然后通过结果寄存器反馈。

### 19.8 raw us 绕过控制层

raw us 虽然是调试值，但也不能直接写 PCA9685。

正确路径：

```text
Modbus -> arm_action -> arm_control -> PCA9685
```

## 20. 一个通用 Modbus 项目设计模板

如果以后你做新的嵌入式 Modbus 设备，可以按这个模板来：

```text
1. 确定主站和从站
2. 给每个从站分配地址
3. 固定串口参数
4. 选定功能码
5. 规划 input register 状态区
6. 规划 holding register 参数区和命令区
7. 定义状态 bitfield
8. 定义错误码
9. 定义 command_seq 和 last_command_result
10. 明确每个寄存器单位、缩放、范围、有无符号
11. 明确哪些命令需要去重
12. 明确业务拒绝和协议异常的区别
13. 写 PC 串口助手测试帧
14. 写主站轮询流程
15. 硬件联调后冻结协议版本
```

核心设计原则：

```text
功能码用标准的。
寄存器表是项目自己的。
状态只读，控制写入。
命令要可确认。
重复命令要可去重。
从站不主动说话。
控制不要绕过统一业务层。
```

## 21. 当前项目的最佳实践总结

本项目目前采用的方式可以总结成：

```text
04H 负责读状态
06H 负责安全类单命令
10H 负责批量目标和一次性动作
command_seq 防止重复执行
last_command_result 区分业务结果
raw us 也必须走动作层和平滑层
动作录制通过 HMI 加一步/删除/清空自动保存到 Data Flash，control_command=7 只作为远程维护强制同步命令
vision board 只做主站调度，不做底层 PWM 控制
RA6M5 只做机械臂控制板从站，不主动占总线
```

这样设计的好处：

- PC 串口助手可以直接临时代替 vision board 调试。
- vision board 后续只需要实现标准 Modbus 主站，不需要理解 HMI 私有帧。
- ZDT 电机和机械臂控制板都按 `04H 读状态、06H/10H 写控制` 的风格接入。
- 机械臂所有控制来源都进入同一套 `arm_control / arm_action`，不会抢 PCA9685。
- 后续可以继续扩展配置参数、运动学目标、错误码，而不用推翻协议。

## 22. 参考资料

建议后续查标准时优先看这些资料名称：

- `MODBUS Application Protocol Specification V1.1b3`
- `MODBUS over Serial Line Specification and Implementation Guide V1.02`
- Modbus Organization 官网：`https://www.modbus.org/`

本文没有完全照抄标准文档，而是按嵌入式工程实践重新组织，重点解释如何把 ModbusRTU 真正用在 RA6M5 机械臂控制板、vision board、ZDT 电机组成的系统中。
