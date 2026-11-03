# 系统架构

本文说明仓库固件的运行链路、动作接口和验证状态。固件来源见[来源与许可](../firmware/README.md#来源与许可)，实拍样机与仓库构建版本的区别见[演示与仓库固件](../firmware/README.md#演示与仓库固件)。

## 运行链路

```text
用户语音
   │
   ▼
ESP32-S3：采音、音频处理、Opus 编码
   │ WSS
   ▼
灵矽：ASR → LLM → TTS
   │             │
   │ MCP 调用    └── 音频返回设备播放
   ▼
设备 MCP Server → 动作与参数校验 → 本地步态/头嘴控制 → 舵机
```

灵矽是现有云端服务，仓库不包含服务端实现。小智固件提供网络、音频和设备 MCP 框架，导入的分支已包含 Otto 板级适配与运动控制；本项目在此基础上增加鸭形适配、交互调整和动作保护。

## 模块与代码入口

| 模块 | 职责与本项目改动 | 代码入口 |
| --- | --- | --- |
| 会话与按键 | 沿用小智会话状态机，增加板级模式切换、PTT 接管与恢复、播放中止处理 | [application.cc](../firmware/xiaozhi-esp32/main/application.cc)、[otto_robot.cc](../firmware/xiaozhi-esp32/main/boards/otto-robot/otto_robot.cc) |
| 音频参考 | 将播放 PCM 转为设备端 AEC 的参考输入，处理重采样、缓冲和延迟 | [otto_audio_codec.h](../firmware/xiaozhi-esp32/main/boards/otto-robot/otto_audio_codec.h)、[otto_audio_codec.cc](../firmware/xiaozhi-esp32/main/boards/otto-robot/otto_audio_codec.cc) |
| 普通 Otto 动作 | 在上游运动控制基础上收紧云端动作集合，增加互斥、超时和停止路径 | [otto_controller.cc](../firmware/xiaozhi-esp32/main/boards/otto-robot/otto_controller.cc) |
| 鸭形动作 | 新增头嘴控制、独立限位与校准、动作编排和合作式停止，腿脚复用 Otto 运动实现 | [duck_controller.cc](../firmware/xiaozhi-esp32/main/boards/otto-robot/duck_controller.cc)、[otto_movements.cc](../firmware/xiaozhi-esp32/main/boards/otto-robot/otto_movements.cc) |
| 显示 | 在原屏幕显示上增加状态和小窗口单眼布局 | [otto_emoji_display.cc](../firmware/xiaozhi-esp32/main/boards/otto-robot/otto_emoji_display.cc) |
| 板型与构建 | 选择板卡引脚；同一板级目录提供 `otto-robot`、`otto-duck` 两个构建变体 | [config.h](../firmware/xiaozhi-esp32/main/boards/otto-robot/config.h)、[config.json](../firmware/xiaozhi-esp32/main/boards/otto-robot/config.json) |

板型默认根据 GPIO15/16 电平识别，随后单独探测摄像头。板卡接线与机械校准步骤见[装配与校准](../hardware/ASSEMBLY.md)。

## 启动与语音

设备先通过热点配网，再请求灵矽 OTA 获取激活信息。完成控制台绑定后，设备取得 WSS 地址和 token，携带设备标识建立连接，并通过 `hello` 协商音频格式与工具能力。接入地址与协议资料统一见[固件说明](../firmware/README.md#来源与许可)。

```text
麦克风 → AFE（AEC/VAD）→ Opus → WSS → 灵矽 ASR/LLM
扬声器 ← PCM/I2S ← Opus 解码 ← WSS ← 灵矽 TTS
           └── 播放 PCM 软件参考 → AFE
```

AEC（声学回声消除）复用既有 AFE 音频处理框架。本项目把送往扬声器的数字音频复制为参考流，供算法估计自身播报的回声，并修复参考流的重采样和缓冲问题。软件参考接通不等于声学效果达标：播放延迟、喇叭失真和壳体耦合仍影响回声与自打断。

三种会话模式来自上游。本项目增加板级切换与 PTT（按住说话）适配，并修复接管、松手和模式恢复的时序。

| 模式 | 收音行为 | 播放与中止 |
| --- | --- | --- |
| Manual | 显式开始和结束收音；PTT 按住期间流式上传，松手发送结束收音消息 | 按键可中止播报并接管收音 |
| Auto | 自动结束一轮收音，播报结束后继续监听 | 播报时关闭普通语音上行；可通过按键或支持的唤醒词中止 |
| Realtime | 对话中持续上传麦克风，播报时保持收音 | 处理服务端打断消息，并有本地 AEC 后 VAD 打断路径；效果仍待真机验收 |

自打断指设备把自身播报误当作用户插话，导致停止播报。目前原 Otto 样机仍存在该问题；鸭形固件尚未完成声学验收。实体键提供独立于语音识别的本地中止入口。

## MCP 动作接口

云端通过 `tools/call` 选择设备提供的动作，例如：

```json
{
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "self.otto.action",
      "arguments": {
        "action": "walk",
        "steps": 1,
        "speed": 1200,
        "direction": 1
      }
    }
  }
}
```

| 构建变体 | 动作工具 | 动作范围 |
| --- | --- | --- |
| `otto-robot` | `self.otto.action` | `walk`、`turn`、`sit`、`swing`、`hand_wave`、`greeting`、`home` |
| `otto-duck` | `self.otto.action`、`self.flashduck.action` | 下列 20 种鸭形动作 |

- 移动与姿态：`walk`、`turn`、`jump`、`swing`、`updown`、`sit`、`home`。
- 头嘴动作：`head_turn`、`head_shake`、`look_around`、`quack`、`chatter`。
- 组合动作：`curious`、`preen`、`happy_waddle`、`stretch`、`dance`、`peck`、`yawn`、`sleepy`。

鸭形的 `self.otto.*` 与 `self.flashduck.*` 指向同一个控制器，用于兼容已有工具调用和鸭形提示词。两组名称不代表两套固件。鸭形不接受旧手臂动作，也不接受任意舵机序列。

| 参数 | 普通 Otto | 鸭形 |
| --- | --- | --- |
| `steps` | 1–10，默认 3 | 1–10，默认 1；`quack/chatter` 最多 3 次 |
| `speed` | 800–2000ms，数值越小越快 | 实际执行收紧至 800–2000ms；舞蹈周期进一步限制为 800–1000ms |
| `amount` | 0–60；`arm_swing` 同范围 | 0–30，再按头嘴和腿脚范围限幅 |
| `direction` | 1 为前进/左转，-1 为后退/右转 | 行走/转向只接受 ±1；`head_turn` 的 0 表示回正 |

辅助工具包括 `stop`、`get_status`、`set_trim`、`get_trims`、`get_ip`，以及 `self.battery.get_level`。鸭形辅助工具同样提供两组前缀。

校准接口按结构区分：普通 Otto 使用四个腿脚与 `left_hand/right_hand`，微调范围为 ±50°；鸭形改为 `mouth/neck`，微调范围为 ±10°，空闲时才接受，经工作任务保存并归位。鸭形校准独立存储，头嘴最终输出在微调后再次限幅。校准应按装配步骤逐关节进行。

控制器接受动作请求，只表示安排执行，不等于实物已经到位。普通 Otto 状态为 `moving/idle`；鸭形状态为 `moving/idle/stopped/timeout/disabled`。鸭形的 `sit`、`head_turn` 会保持姿态，需要归位时调用 `home`。

鸭形动作拒绝通过 JSON-RPC `error` 返回；普通 Otto 的部分拒绝以错误文本返回，`result.isError` 仍为 `false`。调用方需同时处理 JSON-RPC 错误和结果中的拒绝文本，不能仅凭 `isError` 判断动作已受理。

## 动作保护

动作时序在设备端执行，云端动作调用经过白名单和参数校验。`action` 请求执行中拒绝新动作；停止入口不受该忙状态限制。默认构建不注册任意舵机序列工具。

| 保护路径 | 普通 Otto | 鸭形 |
| --- | --- | --- |
| 停止 | 中止动作、清空队列并归位 | 合作式取消并保持当前位置；工作任务结束前仍拒绝新动作 |
| 超时 | 30 秒 watchdog，触发停止和归位 | 30 秒期限包含待执行时间，触发取消 |
| 断网 | 触发本地停止，重连不恢复旧动作 | 触发本地取消，重连不恢复旧动作 |
| 实体键 | 动作中按键优先停步 | 动作中按下优先请求取消 |
| 引脚与限位 | 使用板级引脚配置和动作参数范围 | 舵机引脚冲突时不初始化动作；头嘴使用独立机械限位 |

这些是代码中的保护路径，不能替代机械行程和停止时延的实测。穿戴设备、外部任务事件等主动交互尚未实现，后续方向见[产品与商业思考](PRODUCT.md)。

## 实现状态

验证记录截至 2026-11-03，区分实物演示、源码测试和构建结果。

| 范围 | 已有证据 | 尚未验证或仍存在的问题 |
| --- | --- | --- |
| 上游基线 | `txp666/xiaozhi-esp32@fb7f815` 完整导入并校验，建立 `v0` 标签 | 后续新增能力以各次验证记录为准 |
| 原 Otto 语音链路 | 此前已在实物上跑通 OTA 激活、WSS 会话、MCP 调用和模式切换 | 设备端 AEC 已烧录，但回声与自打断尚未解决 |
| 实拍样机 | 已完成装配和多个语音动作演示 | 视频运行版本见[演示与仓库固件](../firmware/README.md#演示与仓库固件)，不能作为仓库鸭形固件的验收结果 |
| 鸭形适配 | 2026-11-02 完成独立构建变体、头嘴控制、限位、校准、停止和单眼窗口代码；两种变体构建通过 | 本次固件的实物板型/引脚、打印件行程、动作效果、窗口可读性待验收 |
| 回归修复 | 2026-11-03 修复音频参考重采样越界、首块丢失、满缓冲读写竞争；修复 PTT 长按附加单击、连接中松手、manual 接管恢复；修复普通 Otto 入队 busy 竞态 | 本轮未烧录，动作、PTT 和 AEC 效果仍待真机验收 |

2026-11-03 的本地验证结果：

- `otto-robot`、`otto-duck` 均通过 ESP-IDF 6.1 构建，目标芯片为 `esp32s3`。
- 主机测试运行 136 项，133 项通过、3 项跳过。跳过原因是缺少 cJSON/mbedTLS 开发头及资源编码测试的 Python 依赖。
- 音频专项 ASan、TSan 检查通过；新增回归先复现旧问题，再验证修复。

下一轮真机验收需记录停止时延、断网响应、30 秒超时、各关节实际行程和音频打断效果。此前提出的“500ms 停步、2 秒归位/断网停止”尚无实测结果，不作为当前性能承诺。
