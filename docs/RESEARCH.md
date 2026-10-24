# 技术选型与接入依据

> 核验日期：2026-10-24。硬件路线、价格和采购清单统一记录在 [hardware/BOM.md](../hardware/BOM.md)，本文只说明平台和接入方式。

## 当前方案

```text
现有六舵机成品玩具（当前）或 ESP32-S3 Otto 套件（备选）
＋ FlashDuck 或 Otto 兼容打印结构
＋ XiaoZhi WebSocket/Opus 客户端
＋ 灵矽 OTA、ASR、LLM、TTS 与设备 MCP
```

软件保留语音、双足移动、转向、坐下和展示动作；不实现 15 舵机、强化学习、自主导航和视觉感知。

## 平台选择

| 方案 | 适用能力 | 限制 | 结论 |
| --- | --- | --- | --- |
| ESP32-S3 或兼容 MCU＋6 路 PWM 舵机 | 麦克风、扬声器、屏幕、WSS 和 MCP，成本与装配可控 | 固定步态，需要确认具体玩具的引脚和烧录权限 | 当前采用 |
| Linux SBC＋15 个总线舵机 | 更高自由度、IMU、视觉和本地强化学习 | 采购、装配、标定和调试时间显著增加 | 不作为首版 |

ESP32-S3 方案复用 XiaoZhi 的音频、WebSocket、OTA 和 MCP 代码，并将语音理解放到云端，因此不需要 Linux 或第二颗 MCU。该结论只适用于主控和引脚与 Otto 板型兼容的实物。

## 接入基线

| 项目 | 基线 |
| --- | --- |
| 固件仓库 | <https://github.com/txp666/xiaozhi-esp32> |
| 固定提交 | `fb7f8152628f07a3f1bca91c222d3bc8853b1f70` |
| 板型 | `otto-robot`，无摄像头，6 舵机 |
| 工具链 | ESP-IDF 6.1 |
| 灵矽 OTA | <https://xrobo.qiniuapi.com/v1/ota/> |
| WebSocket 协议 | <https://linx.qiniu.com/docs/xrobot/platform/websocket> |
| 设备 MCP | <https://linx.qiniu.com/docs/xrobot/mcp/hardware-mcp> |

比赛允许复用已有 codebase，但要求先导入完整上游源码并创建 `v0` 标签，之后才能提交本项目新增代码。第一个导入 PR 保持上游原样，是为了明确比赛开发起点；`v0` 之后的修改仍通过 PR 正常进行。

云端与设备职责、MCP 工具和动作限幅统一记录在 [ARCHITECTURE.md](ARCHITECTURE.md)。云端负责理解和选择动作，设备负责舵机时序、限位、断网和超时停止。

## 全双工与语音打断

灵矽 WebSocket 协议支持 `realtime` 模式：设备持续上传麦克风，服务端检测到人声后打断 AI 播报，并发送 `tts.stop(is_aborted=true)`。固件开启服务器端 AEC，收到被打断的停止消息后立即清空本地音频队列。回声、误打断率和响应时延仍需真机验收，验证项见 [VALIDATION.md](VALIDATION.md)。

## 许可与来源

- 上游固件和第三方组件保留各自许可证、来源地址和固定版本。
- Fanduck 与 Microduck 的机械模型含有非商业限制，不能由软件许可证覆盖。
- FlashDuck 页面标注“标准数字文件许可－社区使用版”，仓库只记录链接，不转载模型文件。
- 比赛提交必须区分上游代码、第三方模型和本人新增实现。
