# 96x96 模型热切换 SOP

本文档记录当前 Vision Board 项目已经验证通过的第一阶段模型热切换流程。

当前阶段目标不是支持任意 Edge Impulse 工程，而是支持同一类 96x96 Visual AD / FOMO-AD 模型在不重新烧录固件的情况下切换检测对象。

## 1. 当前已验证状态

已验证链路：

- 内置模型：`cap_v15_96`
- SD 卡模型包：`cap_v22_96`
- 输入尺寸：`96x96 RGB`
- 热力图网格：`5x5`
- TFLite graph 数量：2 个
- 模型文件：`model.tflite` + `anomaly_model.tflite`
- 元数据文件：`model.ini`
- 加载方式：SD 卡读取模型文件到 SDRAM，然后覆盖 Edge Impulse v15 SDK 内部 graph 配置
- 回退方式：`detect_model_cmd v15` 可切回固件内置模型

已测结果：

```text
model_app_load_cmd /vision/models/cap_v22_96
[model] metadata loaded: cap_v22_96
[model] files=present sdram=loaded main=49064 anomaly=8344 arena0=183808 arena1=76192
```

```text
detect_model_cmd
[detect] model=cap_v22_96 input=96x96 grid=5x5 threshold=8.3 arena=183808
```

切回内置模型：

```text
detect_model_cmd v15
[detect] model=cap_v15_96 input=96x96 grid=5x5 threshold=9.3 arena=183808
```

## 2. 适用范围

当前热切换只适用于以下情况：

- Edge Impulse 工程仍然使用 `96x96` 输入。
- 仍然是 Visual AD / FOMO-AD 这类两段 graph 流程。
- 输出热力图仍然是 `5x5`。
- 训练参数、模型结构、导出方式尽量保持一致，只更换训练数据。
- 新模型用到的 TFLite op 已经被当前固件编译进去。

当前不建议直接热切换：

- `120x120` 或 `160x160` 输入模型。
- graph 数量变化的模型。
- grid 尺寸变化的模型。
- 改了 Edge Impulse 模型容量、后处理方式、学习块结构的模型。
- 需要新增 TFLite op 的模型。

这些不是不能做，而是需要升级固件侧 profile、内存规划、算子集合和 UI 显示逻辑。

## 3. Edge Impulse 导出要求

导出模型时建议保持：

- Deployment 类型：C++ library / MCU SDK 导出。
- 输入尺寸：`96x96`。
- 模型类型：Visual AD / FOMO-AD。
- 量化方式：当前固件链路按导出的 TFLite 文件运行，优先保持与已经验证模型一致。
- 尽量只替换训练数据，不改 impulse 结构和模型容量。

导出后，本地会得到类似目录：

```text
D:\AAAWaterCode\edge_impulse_project\water218146-project-1-cpp-mcu-v22-96x96-test
```

## 4. PC 端打包命令

在工程根目录运行：

```powershell
python D:\AAAWaterCode\Rtthread_workspace\vision_board_basic\pc_tools\vision_dataset_tool.py package-model `
  --source D:\AAAWaterCode\edge_impulse_project\water218146-project-1-cpp-mcu-v22-96x96-test `
  --output C:\Users\Water21\vision_model_packages\cap_v22_96 `
  --name cap_v22_96 `
  --profile cap_v15_96 `
  --input-size 96x96 `
  --grid 5x5 `
  --threshold-auto `
  --arena-auto
```

输出目录结构应为：

```text
cap_v22_96/
  model.tflite
  anomaly_model.tflite
  model.ini
```

`model.ini` 示例：

```ini
name=cap_v22_96
profile=cap_v15_96
input=96x96
grid=5x5
threshold=8.3
arena0=183808
arena1=76192
main=model.tflite
anomaly=anomaly_model.tflite
```

字段含义：

- `name`：UI 和串口显示的模型名。
- `profile`：固件侧兼容 profile，目前固定使用 `cap_v15_96`。
- `input`：模型输入尺寸。
- `grid`：热力图网格尺寸。
- `threshold`：异常阈值，来自 Edge Impulse 导出工程。
- `arena0`：主视觉 graph 的 TFLite arena 大小。
- `arena1`：异常后处理 graph 的 TFLite arena 大小。
- `main`：主模型文件名，当前固定为 `model.tflite`。
- `anomaly`：异常模型文件名，当前固定为 `anomaly_model.tflite`。

## 5. 拷贝到 SD 卡

将模型包复制到 SD 卡：

```text
/vision/models/cap_v22_96/
  model.tflite
  anomaly_model.tflite
  model.ini
```

注意路径必须是 `/vision/models/<模型名>/`。

## 6. 板端加载和测试流程

上电后等待 SD 卡挂载完成，再执行：

```text
detect_app_stop_cmd
model_app_load_cmd /vision/models/cap_v22_96
detect_model_cmd
detect_app_init_cmd
detect_app_once_cmd
detect_app_stats_cmd
```

期望看到：

```text
[model] metadata loaded: cap_v22_96
[model] files=present sdram=loaded main=... anomaly=... arena0=... arena1=...
[detect] model=cap_v22_96 input=96x96 grid=5x5 threshold=...
```

连续运行测试：

```text
detect_app_start_cmd
```

等待 5 到 10 秒：

```text
detect_app_stop_cmd
detect_app_stats_cmd
```

UI 联动测试：

1. 进入检测页面。
2. 点击启动。
3. 观察串口是否输出：

```text
[detect] cmd source=ui action=start
```

4. 点击停止。
5. 观察串口是否输出：

```text
[detect] cmd source=ui action=stop
```

串口命令触发时应输出：

```text
[detect] cmd source=msh action=start
[detect] cmd source=msh action=stop
```

## 7. 内置模型回退测试

如果需要回到固件内置 v15 模型：

```text
detect_app_stop_cmd
detect_model_cmd v15
detect_model_cmd
detect_app_once_cmd
detect_app_stats_cmd
```

期望看到：

```text
[detect] model=cap_v15_96 input=96x96 grid=5x5 threshold=9.3 arena=183808
```

## 8. 性能参考

当前已测大致性能：

内置 v15：

```text
ai≈180 ms
invoke≈144 ms
g0≈165 ms
g1≈7 ms
```

SD 卡 v22：

```text
ai≈300 ms
invoke≈259 ms
g0≈275 ms
g1≈17 ms
```

含义：

- `ai`：一次 AI 后端整体耗时。
- `invoke`：TFLite Micro 真正执行模型算子的总耗时。
- `g0`：主视觉 graph，总体最耗时。
- `g1`：异常后处理 graph。

v22 比 v15 慢，主要是 graph0 模型本身更慢，不是 SD 热加载机制导致。

## 9. 常见问题

### 9.1 `capture ai input failed: -116`

这是摄像头采集输入失败或等待超时。偶发可以先忽略；如果连续出现，需要查 camera_app 的帧同步和采集超时。

### 9.2 `Cannot find a RTC device`

当前记录时间戳调用了系统时间，但板端没有 RTC 设备。它不影响 AI 推理，只影响记录时间的真实日期。

### 9.3 `ERR: Out of memory, can't allocate matrix_ptrs[0]`

已修复。原因是 EI DSP 阶段曾经每次从 heap 申请约 108KB 特征矩阵，UI 场景下 heap 更容易紧张。现在该 DSP feature buffer 已改为 SDRAM 静态分配，并修复了 EI SDK 失败路径错误释放 `matrix_ptrs` 导致 heap 断言的问题。

### 9.4 新模型加载失败

按顺序检查：

- SD 卡路径是否正确。
- `model.ini` 是否存在。
- `model.tflite` 和 `anomaly_model.tflite` 是否存在。
- `input` 是否仍为 `96x96`。
- `grid` 是否仍为 `5x5`。
- `arena0/arena1` 是否超过当前固件支持。
- Edge Impulse 模型是否新增了当前固件未编译的 TFLite op。

## 10. 后续扩展方向

下一阶段建议顺序：

1. 继续稳定 `96x96` 同 profile 模型热切换。
2. 在 PC 工具中输出更完整的模型检查报告，例如输入尺寸、threshold、arena、graph 文件大小。
3. 增加固件侧模型包校验日志，把不支持的原因打印清楚。
4. 再考虑新增 `120x120` 或 `160x160` profile。
5. 若要支持不同输入尺寸，需要同时升级图像预处理、AI input buffer、DSP feature buffer、UI grid 显示、TFLM arena 上限和模型包校验逻辑。
