# Microduck-Inspired Biped Voice Robot

基于六舵机双足玩具和可打印鸭壳的桌面机器人。灵矽负责 ASR、LLM、TTS，设备通过 MCP 工具调用执行本地动作。

**Demo**：[《复刻 Micro Duck（仅实现半成品）并接入灵矽》](https://www.bilibili.com/video/BV1Gznf6VEm2)（B 站，4 分 20 秒）

## 状态

上游固件已完整导入并合入 `main`（`v0` 基线），实体设备完成多轮真机烧录，灵矽语音链路真机已打通。当前工作：舵机槽位硬件修复。

| 项目 | 状态 |
| --- | --- |
| 硬件 | ESP32-S3、屏幕、电池、充电均正常；舵机槽位/排针修复中 |
| 固件 | 灵矽接入、动作安全、断网停步、30 秒 watchdog、三种对话模式、设备端 AEC 已实现 |
| 构建与测试 | ESP-IDF 6.1 构建通过，主机测试 130 通过；见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)「实现状态」 |
| 语音链路 | 灵矽 OTA/WSS/MCP 真机已通，OTA 双环境切换已实现 |

## 首版功能

- 灵矽 OTA 激活与 WSS 语音对话
- 前进、后退、左转、右转、坐下、摇摆、挥手
- 云端 LLM 通过 MCP 选择设备动作
- 动作白名单、限步数、限速和单动作队列
- TTS 播放期间保持麦克风上行，用户可直接说话打断；实体键也能立即中止播报

## 目标硬件配置

基于自行组装的六舵机双足玩具（Otto 结构），加装 3D 打印鸭形外壳。Otto/闪猫侠套件作为备选。设备已核对：可进入烧录模式（按住 BOOT 上电）、屏幕与音频正常、原厂链路可接灵矽。

```text
ESP32-S3-WROOM N16R8
└─ 兼容 otto-robot 的控制板
   ├─ 6 × SG90/MG90S 180°
   ├─ 1.54" LCD
   ├─ I2S 麦克风 + MAX98357A + 4Ω/3W 扬声器
   └─ 电池、开关、USB-C
```

本台实际支出约 ¥270（玩具＋打印＋运费）。详细清单见 [hardware/BOM.md](hardware/BOM.md)，装配与校准见 [hardware/ASSEMBLY.md](hardware/ASSEMBLY.md)。

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

源码已入库。激活 ESP-IDF 6.1 后，从 `firmware/xiaozhi-esp32/` 构建：

```bash
cd firmware/xiaozhi-esp32
python scripts/build.py otto-robot --name otto-robot
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

`scripts/build.py` 生成 `build/merged-binary.bin`。已复核的 commit、工具链和产物摘要见[系统架构](docs/ARCHITECTURE.md)「实现状态」。

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
