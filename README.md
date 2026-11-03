# Microduck-Inspired Biped Voice Robot

基于六舵机双足玩具和可打印鸭壳的桌面机器人。本项目固件基于小智/Otto 二次开发，灵矽负责 ASR、LLM、TTS，设备通过 MCP 工具调用执行本地动作。

**实物演示**：[观看约 47 秒 Demo](docs/media/demo.mp4)。展示小鸭接入灵矽后，通过语音控制摇头、跳舞、坐下和归位的效果。视频采用 MP4（H.264/AAC），可下载播放；版本与验证范围见[演示与仓库固件](firmware/README.md#演示与仓库固件)。

## 状态

上游小智/Otto 固件已完整导入并合入 `main`（`v0` 基线）。最新实拍样机已装配并接入灵矽，展示了语音触发的多个动作。

仓库另行开发 `otto-duck` 固件变体：脖子/嘴巴独立控制、限位、小窗口单眼显示。
该变体已完成构建和主机测试，实际板卡引脚、动作行程和真机联动仍待验收；
路演实拍不能作为该变体的验收结果。构建和校准方式见 [hardware/ASSEMBLY.md](hardware/ASSEMBLY.md)。

| 项目 | 状态 |
| --- | --- |
| 路演样机 | 已装配并接入灵矽，实拍了语音触发的多个动作 |
| 仓库固件 | 小智/Otto 基线及 `otto-duck` 变体；动作保护、设备端 AEC 等已有代码，鸭形变体待真机验收 |
| 构建与测试 | Otto、鸭形变体均通过 ESP-IDF 6.1 构建；主机测试运行 136 项，133 项通过、3 项跳过 |
| 已知限制 | 自有固件的回声与自打断尚未解决；实际板卡引脚及鸭形机械行程待核对 |

## 仓库固件功能

- 灵矽 OTA 激活与 WSS 语音对话
- 原 Otto 变体提供前进、后退、转向、坐下、摇摆和挥手；鸭形变体提供行走、转向、转头、摇头、张合嘴和归位，具体真机表现待验收
- 云端 LLM 通过 MCP 选择设备动作
- 动作白名单、限步数、限速和单动作互斥
- TTS 播放期间可保持麦克风上行；真机 AEC 回声与自打断问题尚待解决，不能视为稳定全双工验收

## 目标硬件配置

基于六舵机双足套件，加装 FlashDuck 3D 打印鸭壳。仓库固件支持自动识别摄像头版和无摄像头版配置，实际主板型号与引脚仍需结合实物和启动日志核对。

```text
ESP32-S3（具体模组与板型待核对）
└─ 兼容 otto-robot 的控制板
   ├─ 6 × 舵机（型号与行程以实物为准）
   ├─ 1.54" LCD
   ├─ 麦克风与扬声器（音频方案按实物核对）
   └─ 电池、开关、USB-C
```

此前记录的玩具、打印和运费约 ¥270，尚未计入后续采购及人工等完整成本。详细清单见 [hardware/BOM.md](hardware/BOM.md)，装配与校准见 [hardware/ASSEMBLY.md](hardware/ASSEMBLY.md)。

## 运行链路

```text
语音 → 灵矽 ASR/LLM/TTS
             │ WSS + MCP tools/call
             ▼
ESP32-S3 → 本地动作校验 → Otto 步态 → 舵机
```

## 仓库结构

```text
├── firmware/   # ESP32 固件（上游源码已导入，`v0` 基线）
├── hardware/   # BOM、装配和校准
└── docs/       # 产品、调研与架构
```

## 构建与烧录

激活 ESP-IDF 6.1 后，从 `firmware/xiaozhi-esp32/` 构建鸭形变体：

```bash
cd firmware/xiaozhi-esp32
python3 scripts/build.py otto-robot --name otto-duck --language zh-CN --wake-word wn9_xiaoyaxiaoya_tts2
```

此命令选择“小鸭小鸭”唤醒模型，只构建、不烧录。省略 `--wake-word` 时，中文构建默认选择“你好小智”。切换固件前需先确认实物板型、引脚、舵机行程和恢复方式；实拍版本与本项目构建的区别见[演示与仓库固件](firmware/README.md#演示与仓库固件)。构建与验证记录见[系统架构](docs/ARCHITECTURE.md)「实现状态」。

要求 ESP-IDF 6.1、ESP32-S3 N16R8 和支持数据传输的 USB-C。首次烧录时可能需要按住 BOOT 再上电。灵矽 OTA 默认为 `https://xrobo.qiniuapi.com/v1/ota/`。

## 文档

- [产品定义与用户研究](docs/PRODUCT.md)：项目范围、目标用户和市场判断
- [商业价值与商业化路径](docs/BUSINESS.md)：竞品价格、成本结构与商业化计划
- [技术选型与接入依据](docs/RESEARCH.md)：平台选择、灵矽协议和许可证边界
- [系统架构](docs/ARCHITECTURE.md)：运行拓扑、模块划分、安全设计与实现状态
- [固件导入说明](firmware/README.md)：上游基线和 `v0` 流程
- [硬件 BOM](hardware/BOM.md)：路线、价格、采购和到货检查
- [装配与校准](hardware/ASSEMBLY.md)：玩具组装和 Otto 套件的装配流程
- [开发与提交规范](CONTRIBUTING.md)：commit、工作树、PR、验证和敏感信息约束

## 适用范围

本项目不是 Pollen Robotics 官方产品，也不是 1:1 Microduck。首版不包含 15 舵机 RL、自稳、跌落恢复、拾取、踢球、轮滑、自主导航或任意舵机角度控制。

## 来源与许可

固件基于 `txp666/xiaozhi-esp32`，导入时必须保留上游 LICENSE、来源 commit 和第三方许可。FlashDuck 外壳来自 MakerWorld 社区模型，仓库只记录来源和链接，不直接转载模型文件；字体、音频及其他素材分别遵守自身许可。
