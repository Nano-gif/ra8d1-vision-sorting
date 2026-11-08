# NG 分拣角度补偿协议 — 定稿 v1

视觉板（Modbus **主站**）检测到 NG 件后，把该件朝向角度发给机械臂（**从站 addr=1，57600 8N1**），
机械臂在 NG 分拣动作里**新增两处腕部 Roll(J5) 动作**（对齐 + 复位，见 §3）。
标准 8 步法本身**不含**任何腕旋转。当前版本只做角度补偿，不做摆放偏移。

---

## 1. 新增寄存器（机械臂从站侧）

写（保持寄存器，0x06/0x10）：

| 地址 | 名称 | 类型 | 说明 |
|---|---|---|---|
| `0x00D0` | object_yaw_x10 | int16（两补码存 uint16） | 物品朝向，单位 0.1° |
| `0x00D1` | task_param_seq | uint16 | 与本次 PLAY 的 seq 相同 |

读（输入寄存器，0x04）—— 仅供调试，主站热路径不会读：

| 地址 | 名称 | 说明 |
|---|---|---|
| `0x0065` | last_object_yaw_x10 | 最近一次锁存的角度 |
| `0x0066` | last_task_param_seq | 最近一次锁存的序号 |
| `0x0067` | task_param_valid | 上次 PLAY 是否成功绑定角度 (0/1) |

实现提示：`modbus_slave_write_shadow()` 加 0x00D0/0x00D1 两个 case；
`modbus_slave_read_input_register()` 加 0x0065~0x0067。

---

## 2. 锁存与绑定逻辑（关键）

主站每次取件发两帧：

```
帧1: 0x10 写 0x00D0 count=2 = [yaw_x10, seq]    ← 角度+序号，一帧原子写
帧2: 0x10 写 0x00F0 count=3 = [5(PLAY), 0, seq]  ← 现有 PLAY，seq 与帧1相同
```

机械臂在执行 PLAY（command==5）的那一刻锁存：

```c
if (shadow_task_param_seq == command_seq) {   /* command_seq 即现有 0x00F2 去重序号 */
    latched_yaw      = (int16_t)shadow_object_yaw_x10;
    task_param_valid = 1;
} else {
    latched_yaw      = 0;
    task_param_valid = 0;     /* 本次没带角度 -> 按不补偿处理 */
}
last_object_yaw_x10 = latched_yaw;
last_task_param_seq = command_seq;
```

> 保证“这次角度只绑定这次动作”，杜绝上一次角度残留。
> 主站为半双工请求/响应：帧1 收到响应后才发帧2，顺序天然保证。

---

## 3. 腕部 Roll(J5) 插入动作 + 死区（重点，已按现场更正）

> ⚠️ 原草案"在第 5 步做 -yaw 补偿"**作废**。标准 8 步法本身没有腕旋转，
> 腕部 J5 是新插入的，且位置如下。

标准 8 步法（不含腕旋转，供对齐）：
```
1 +X_tool 斜下靠近物品
2 -Z_tool 移到物体正上方
3 夹爪闭合，夹住物体
4 夹爪抬升，把物体从传送带夹起
5 Y+ 逆时针旋转 90°（整臂摆向缺陷区，与腕部无关）
6 放到缺陷存放区域
7 夹爪松开
8 抬起机械臂复位
```

本协议**新增两处** J5 动作：

- **A. 对齐**：在第 2 步之后、第 3 步之前，把腕部 Roll 转到与物品朝向对齐，
  使夹爪顺着物品轴线夹取。
- **B. 复位**：在第 4 步之后，把腕部 Roll **复位到中位 0°**，使被夹物品恢复标准朝向，
  再继续第 5 步整臂摆向缺陷区。

死区：以 `latched_yaw` 判一次，落死区或无有效角度时，**A、B 两处都整步跳过**，不产生任何停顿。

```c
#define NG_ROLL_DEADZONE_X10   30   /* 3.0°，可现场调 */

int16_t roll_x10 = latched_yaw;     /* 对齐方向：与物品朝向同向；若现场转反只改这里符号 */

if (task_param_valid == 0 || abs(latched_yaw) < NG_ROLL_DEADZONE_X10) {
    /* ★死区/无角度：A、B 两处都不插入，第3步直接夹、第4步抬起后也不复位★ */
    /* 绝不 move-to-same-position 空占时间 */
} else {
    if (roll_x10 >  900) roll_x10 =  900;   /* J5 量程 ±90° 防御 clamp */
    if (roll_x10 < -900) roll_x10 = -900;
    /* A（第2步后）：roll_target = roll_neutral + roll_x10/10.0，平滑 ~800-1000ms */
    /* B（第4步后）：roll_target = roll_neutral(0°)，平滑 ~800-1000ms */
}
```

- A 和 B 必须**成对**：要么都做，要么都跳过（死区）。
- 符号若现场发现转反了，只改 `roll_x10` 符号，协议不动。

---

## 4. 主站调用顺序（机械臂端不改旧 PLAY 流程）

1. 写 `[0x00D0,0x00D1]=[yaw,seq]`
2. 写 PLAY `[0x00F0,0x00F1,0x00F2]=[5,0,seq]`
3. （可选调试）读 `0x0065~0x0067` 确认绑定

---

## 5. 量程单位

- `object_yaw_x10` 单位 0.1°；主站已归一化/clamp 到 (-90°, +90°]（±900），实际现场只有 ±20~30°。
- J5 零点 1457µs、约 957µs/90°；补偿叠加到现有 roll 基准即可。

---

## 6. 验证步骤

1. `seq=N` 写 `yaw=0` -> PLAY(N)：`valid=1`，落死区 -> A/B 都不插入，全程不卡顿。
2. `seq=N` 写 `yaw=+250`(25°) -> PLAY(N)：第2步后腕对齐 +25°（夹取），第4步后腕复位 0°。
3. 写 `yaw=+250 seq=N`，但 PLAY 用 `seq=M(!=N)`：`valid=0`，A/B 都不插入（验证绑定保护）。
4. 回读 `0x0065/0x0066/0x0067` 确认 last_yaw/last_seq/valid。

---

## 7. 视觉板（主站）侧职责（本仓库已/将实现，列此对齐）

- `angle_x10` 来自 PC 检测结果，已进 NG 队列。
- 取件时在发 PLAY 前用同一 seq 写 `[0x00D0,0x00D1]=[normalize(angle_x10), seq]`。
- 角度归一化：按 180° 取模折叠到 (-900, 900]，再 clamp ±900。
- 死区不在主站做，由机械臂端按 §3 处理（主站始终发真实角度）。
