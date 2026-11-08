# 工业联动寄存器表

本文档定义 VisionBoard 检测终端对外暴露的最小工业联动接口。

当前阶段还没有直接绑定到 Modbus RTU 物理层，而是先在固件内部实现一张 16-bit Holding Register 镜像。后续接 RS485/Modbus 时，协议层只需要调用 `industrial_link_read_holding()` 和 `industrial_link_write_holding()`。

## 设计目标

- 让外部执行板可以读取检测状态、OK/NG、异常分数和计数。
- 让外部执行板可以写命令触发开始、停止、单次检测。
- 先固定支持当前 `96x96 + 5x5 + FOMO-AD/Visual AD` 演示链路。
- UI、串口 msh、未来 Modbus 都通过同一套检测命令和结果快照，避免多份状态不一致。

## 寄存器说明

所有寄存器都是 16-bit，无符号整数。多字节字段采用低 16 位在前、高 16 位在后的方式。

| 地址 | 名称 | 读写 | 说明 |
| --- | --- | --- | --- |
| `0x0000` | `magic` | R | 固定为 `0xA15A`，用于识别设备协议 |
| `0x0001` | `run_state` | R | `0=停止`，`1=运行中` |
| `0x0002` | `stage` | R | 检测阶段：`0=idle`，`1=trigger`，`2=capture`，`3=ai`，`4=output` |
| `0x0003` | `latest_result` | R | 最近一次结果：`0=OK/未检测`，`1=NG` |
| `0x0004` | `score_x10` | R | 最近一次异常分数，放大 10 倍 |
| `0x0005` | `threshold_x10` | R | 当前模型阈值，放大 10 倍 |
| `0x0006` | `total_count_lo` | R | 总检测次数低 16 位 |
| `0x0007` | `total_count_hi` | R | 总检测次数高 16 位 |
| `0x0008` | `ok_count_lo` | R | OK 次数低 16 位 |
| `0x0009` | `ok_count_hi` | R | OK 次数高 16 位 |
| `0x000A` | `ng_count_lo` | R | NG 次数低 16 位 |
| `0x000B` | `ng_count_hi` | R | NG 次数高 16 位 |
| `0x000C` | `ai_time_ms` | R | 最近一次 AI 总耗时，单位 ms |
| `0x000D` | `capture_ms` | R | 最近一次采图和预处理耗时，单位 ms |
| `0x000E` | `model_input_w` | R | 当前模型输入宽度 |
| `0x000F` | `model_input_h` | R | 当前模型输入高度 |
| `0x0010` | `grid_w` | R | 当前热力图网格宽度 |
| `0x0011` | `grid_h` | R | 当前热力图网格高度 |
| `0x0012` | `command` | W | 写命令：`1=start`，`2=stop`，`3=once`；读回固定为 `0` |
| `0x0013` | `version` | R | 协议版本，当前为 `1` |

## msh 模拟测试

读取完整寄存器表：

```text
industrial_link_regs_cmd
```

读取部分寄存器，例如从 `0x0000` 开始读 8 个：

```text
industrial_link_regs_cmd 0x0000 8
```

模拟外部设备写命令寄存器，启动连续检测：

```text
industrial_link_write_cmd 0x0012 1
```

模拟外部设备写命令寄存器，停止检测：

```text
industrial_link_write_cmd 0x0012 2
```

模拟外部设备写命令寄存器，触发单次检测：

```text
industrial_link_write_cmd 0x0012 3
```

## 推荐联调流程

```text
detect_app_stop_cmd
detect_model_cmd
detect_app_init_cmd
industrial_link_regs_cmd
industrial_link_write_cmd 0x0012 3
industrial_link_regs_cmd
industrial_link_write_cmd 0x0012 1
industrial_link_regs_cmd
industrial_link_write_cmd 0x0012 2
industrial_link_regs_cmd
```

预期现象：

- 写 `3` 后会触发一次检测，稍等后 `total_count` 增加。
- 写 `1` 后进入连续检测，`run_state=1`，`total_count` 会持续增加。
- 写 `2` 后停止连续检测，`run_state=0`。
- 写命令时日志来源应显示为 `link`，例如：

```text
[detect] cmd source=link action=start
```

## 后续接 Modbus RTU

下一阶段只需要新增一个 Modbus RTU 协议层：

- 功能码 `0x03`：读取 Holding Registers，内部调用 `industrial_link_read_holding_block()`。
- 功能码 `0x06`：写单个 Holding Register，内部调用 `industrial_link_write_holding()`。
- 可选功能码 `0x10`：写多个 Holding Registers，后续需要批量配置参数时再加。

RS485 硬件需要额外确认：

- 使用哪一路 SCI/UART。
- MAX485 的 DE/RE 方向控制脚接到 RA8D1 哪个 GPIO。
- 波特率、校验位、停止位。
- 终端电阻和上下拉偏置是否放在扩展板上。
