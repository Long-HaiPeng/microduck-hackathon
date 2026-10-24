# 系统架构

> 本文描述首版目标和模块边界。实际验证状态统一记录在 [VALIDATION.md](VALIDATION.md)，未通过的项不标为已验证。

## 运行拓扑

```text
灵矽云端
  ├─ OTA 激活、设备绑定、WSS token
  ├─ ASR / LLM / TTS
  └─ MCP tools/list、tools/call
          │ WSS + Opus
          ▼
ESP32-S3 固件
  ├─ WebSocket 与音频编解码
  ├─ 设备 MCP Server
  ├─ 动作白名单、参数校验和停止逻辑
  ├─ 6 舵机动作队列
  └─ 屏幕、按键、电池和状态显示
          │
          ▼
六舵机玩具本体＋FlashDuck/Otto 打印结构
```

## 模块划分

| 模块 | 职责 | 来源与改动 |
| --- | --- | --- |
| 云端语音服务 | ASR、LLM、TTS、全双工 VAD 和设备 MCP 调用 | 灵矽平台 |
| 网络与协议 | OTA、WSS、Opus 收发、会话状态 | XiaoZhi 上游 |
| 板级适配 | 屏幕、音频 Codec、I2C、按键、电源和舵机引脚 | `otto-robot` 板型，按实物核对 |
| 动作控制 | 固定步态、动作队列、校准和 HOME | `otto_controller.cc`，本项目修改 |
| 安全控制 | 断网停止、动作超时、实体键停车、参数限幅 | 本项目新增 |
| 产品界面 | 表情、状态、聊天文本和电量显示 | XiaoZhi 与 Otto 组件 |
| 外壳与装配 | 成品玩具、打印壳、紧固件和装配说明 | 第三方玩具及社区模型 |
| 文档与验证 | 选型、BOM、测试矩阵和 Demo 记录 | 参赛者与 AI 协助 |

## 分工

参赛者负责硬件采购、实物装配、灵矽账号绑定、真机动作测试和 Demo 录制。AI 协助代码修改、文档整理、静态检查和测试设计。XiaoZhi、灵矽协议、FlashDuck 外壳和第三方组件分别保留原来源与许可证，不计作个人原创。

## 启动与绑定

1. 设备创建配网热点，用户通过手机配置 Wi-Fi。
2. 设备请求灵矽 OTA：`https://xrobo.qiniuapi.com/v1/ota/`。
3. 未绑定设备获得激活码，用户在灵矽控制台完成绑定。
4. 设备再次请求 OTA，取得 WebSocket 地址和 token。
5. 设备携带 `Authorization`、`Device-Id`、`Client-Id`、`Protocol-Version` 建立 WSS。
6. 设备发送 `hello`，声明 Opus 16 kHz 单声道、MCP 和服务器端 AEC 能力。

## 语音链路

```text
麦克风 PCM → Opus → WSS 上行 → 灵矽 ASR/LLM
                                      │
TTS Opus ← WSS 下行 ← 服务端 VAD/AEC ←┘
   │
   └→ 扬声器
```

语音会话使用灵矽 `realtime` 模式。TTS 播放期间麦克风继续上行，服务端检测到用户人声后打断当前播报；设备收到 `tts.stop(is_aborted=true)` 时清空本地播放队列。实体键提供独立的本地中止入口。

## MCP 动作接口

云端通过标准 `tools/call` 请求动作：

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
        "steps": 3,
        "speed": 1200,
        "direction": 1
      }
    }
  }
}
```

可控动作：`walk / turn / sit / swing / hand_wave / greeting / home`。系统工具：`self.otto.stop / self.otto.get_status / self.battery.get_level`。

## 动作与安全

云端只选择动作和参数，舵机时序始终由本地执行。当前限制：

- 单次最多 10 步，速度 800–2000ms，幅度 0–60。
- 同时只接受一个动作；执行中再次调用返回 busy。
- 云端不得发送任意舵机角度序列。
- `self.otto.stop` 不受 busy 限制；500ms 内停止步态，2 秒内完成归位。
- 动作中短按实体键优先停车；空闲时按键仍用于对话控制。
- 单个动作最长 30 秒，超时后停止、清空队列并归位。
- WSS 断开后 2 秒内停止步态、清空队列并归位；重连不恢复旧动作。

| 职责 | 灵矽云端 | 设备本地 |
| --- | --- | --- |
| 语音识别和生成 | 是 | 采集与播放 |
| 理解自然语言 | 是 | 否 |
| 选择动作和参数 | 是 | 白名单与范围校验 |
| 舵机时序与步态 | 否 | 是 |
| 停止、限位和故障处理 | 可触发停止意图 | 最终执行 |
| 断网安全 | 不可依赖 | 是 |

实现状态和真机验收结果统一记录在 [VALIDATION.md](VALIDATION.md)。本地原型已接入断网停止、动作 watchdog、实体键停车和全双工中断清理；这些能力在实体测试完成前都不算已验证。

## 首版不包含

不在云端运行高频舵机闭环；不让 LLM 控制任意角度；不增加第二套游戏音频 WSS；不引入 ONNX 强化学习、自主导航、拾取、踢球、轮滑或跌落恢复。
