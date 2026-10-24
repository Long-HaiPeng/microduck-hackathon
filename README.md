# Microduck-Inspired Biped Voice Robot

基于六舵机双足玩具和可打印鸭壳的桌面机器人。灵矽负责 ASR、LLM、TTS，设备通过 MCP 工具调用执行本地动作。

## 状态

原型开发中。项目采用“非 1:1、保留关键能力”的缩减范围。本地未提交原型曾构建通过；源码尚未入库，当前仓库不可复核。硬件、真实灵矽链路和 demo 尚未验收。

| 项目 | 状态 |
| --- | --- |
| 硬件 | 已选定六舵机成品玩具＋FlashDuck 壳；准备采购，到货后核对烧录与接口兼容性 |
| 固件 | 本地原型已接入断网停止和 30 秒 watchdog，编译通过；源码未入库，真机未验证 |
| 灵矽 OTA/WSS/MCP | 待真机验证 |
| 全双工与语音打断 | 已配置灵矽 `realtime` 和服务器 AEC；待真机验证回声与响应时延 |
| 前进/后退/转向/停止 | 待真机验证 |
| Demo 视频 | 未录制；比赛规则要求提交视频 |

## 首版功能

- 灵矽 OTA 激活与 WSS 语音对话
- 前进、后退、左转、右转、坐下、摇摆、挥手
- 云端 LLM 通过 MCP 选择设备动作
- 动作白名单、限步数、限速和单动作队列
- TTS 播放期间保持麦克风上行，用户可直接说话打断；实体键也能立即中止播报

## 目标硬件配置

当前使用现有的六舵机成品玩具，并为它加装 FlashDuck 鸭形外壳。Otto/闪猫侠套件作为备选。到货后必须核对控制板引脚、音频、屏幕和固件烧录方式，兼容性未确认前不刷舵机固件。

```text
ESP32-S3-WROOM N16R8
└─ 兼容 otto-robot 的控制板
   ├─ 6 × SG90/MG90S 180°
   ├─ 1.54" LCD
   ├─ I2S 麦克风 + MAX98357A + 4Ω/3W 扬声器
   └─ 电池、开关、USB-C
```

目标总价控制在 ¥800 内；超过这个金额就改用 ¥299–600 的 Otto 套件。详细清单见 [hardware/BOM.md](hardware/BOM.md)，装配与校准见 [hardware/ASSEMBLY.md](hardware/ASSEMBLY.md)。

## 运行链路

```text
语音 → 灵矽 ASR/LLM/TTS
             │ WSS + MCP tools/call
             ▼
ESP32-S3 → 本地动作校验 → Otto 步态 → 舵机
```

## 仓库结构

```text
├── firmware/   # ESP32 固件，完整上游源码导入后使用
├── hardware/   # BOM、装配和校准
└── docs/       # 产品、调研、架构、验证与仓库规则
```

## 构建与烧录

固件基线导入后，从 `firmware/xiaozhi-esp32/` 构建：

```bash
idf.py set-target esp32s3
idf.py menuconfig   # Board: Otto Robot
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

要求 ESP-IDF 6.1、ESP32-S3 N16R8 和支持数据传输的 USB-C。首次烧录时可能需要按住 BOOT 再上电。灵矽 OTA 默认为 `https://xrobo.qiniuapi.com/v1/ota/`。

## 文档

- [产品定义与用户研究](docs/PRODUCT.md)：项目范围、用户画像和市场判断
- [技术选型与接入依据](docs/RESEARCH.md)：平台选择、灵矽协议和许可证边界
- [系统架构](docs/ARCHITECTURE.md)：运行拓扑、模块规格、分工和安全设计
- [验收记录](docs/VALIDATION.md)：实际证据、测试矩阵和 Demo 提交要求
- [固件导入说明](firmware/README.md)：上游基线和 `v0` 流程
- [硬件 BOM](hardware/BOM.md)：路线、价格、采购和到货检查
- [装配与校准](hardware/ASSEMBLY.md)：成品玩具和 Otto 套件的装配流程
- [开发与提交规范](CONTRIBUTING.md)：commit、工作树、PR、验证和敏感信息约束

## 适用范围

本项目不是 Pollen Robotics 官方产品，也不是 1:1 Microduck。首版不包含 15 舵机 RL、自稳、跌落恢复、拾取、踢球、轮滑、自主导航或任意舵机角度控制。

## 来源与许可

固件基于 `txp666/xiaozhi-esp32`，导入时必须保留上游 LICENSE、来源 commit 和第三方许可。FlashDuck 外壳来自 MakerWorld 社区模型，仓库只记录来源和链接，不直接转载模型文件；字体、音频及其他素材分别遵守自身许可。
