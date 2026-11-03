# 固件说明

本仓库使用 `txp666/xiaozhi-esp32` 中已集成 Otto 机器人支持的小智固件作为基线。小智提供语音、联网和显示框架，Otto 相关模块负责板级适配与动作控制，二者编译在同一份设备固件中。

上游源码已完整导入并建立 `v0` 标签，后续加入本项目的设备交互、动作保护和鸭形适配。固定版本、接入资料和许可统一见本文[来源与许可](#来源与许可)。

## 构建变体

| 构建名 | 用途 | 关节分配 |
| --- | --- | --- |
| `otto-robot` | 原 Otto 结构 | 双腿、双脚、双臂 |
| `otto-duck` | 鸭形结构 | 双腿、双脚、嘴巴、脖子 |

两者共用 `otto-robot` 板型目录，通过配置选择控制器和显示方式。`otto-duck` 使用独立构建名称上报设备身份，不能按普通 Otto 固件处理更新。

## 构建

先安装并激活 ESP-IDF 6.1 环境，目标芯片为 ESP32-S3。环境准备可参考[上游说明](xiaozhi-esp32/README_zh.md)。

在仓库根目录执行以下命令构建鸭形变体：

```sh
cd firmware/xiaozhi-esp32
python3 scripts/build.py otto-robot --name otto-duck --language zh-CN
```

构建原 Otto 变体时，将 `--name otto-duck` 改为 `--name otto-robot`。构建脚本会生成固件，不会烧录。

中文构建默认使用“你好小智”唤醒词。完成首次组件配置后，可用 `python3 scripts/build.py --list-wake-words` 查看可用模型，再通过 `--wake-word <模型名>` 选择唤醒词。

构建脚本每次重新生成配置。如果已通过 `idf.py menuconfig` 调整头嘴行程、眼睛窗口等本地参数，后续使用 `idf.py build` 保留这些设置；再次运行 `scripts/build.py` 会恢复变体默认配置。

## 烧录与验收

烧录前按[板卡与接线核对](../hardware/ASSEMBLY.md#板卡与接线核对)确认板型、引脚和供电；烧录后按装配说明逐项校准关节方向与行程。

确认配置后，在固件目录使用 ESP-IDF 烧录命令：

```sh
idf.py -p <串口> flash monitor
```

板级引脚定义见 [config.h](xiaozhi-esp32/main/boards/otto-robot/config.h)。Wi-Fi、设备凭据和本地 SDK 配置不入库。烧录后的最小测试顺序、停止与断网测试见[装配说明](../hardware/ASSEMBLY.md)。

主机回归测试可在固件目录执行：

```sh
python3 -m unittest discover -s scripts/tests -v
```

已执行的构建、测试及真机验证结果统一记录在 [实现状态](../docs/ARCHITECTURE.md#实现状态)。

## 演示与仓库固件

| 对象 | 固件来源 | 验证范围 |
| --- | --- | --- |
| [约 47 秒实物 Demo](../docs/media/demo.mp4) | FlashDuck 官方固件，接入灵矽 | 视频中的摇头、跳舞、坐下和归位 |
| 本仓库 `otto-duck` | 在含 Otto 支持的小智固件上二次开发 | 构建与主机测试；板卡适配、机械行程和真机联动待验收 |

视频记录实拍版本的动作效果，不能作为本仓库鸭形固件的真机验收结果。

## 代码入口

| 路径 | 内容 |
| --- | --- |
| [application.cc](xiaozhi-esp32/main/application.cc) | 会话状态、PTT 与播放打断处理 |
| [Otto 板型目录](xiaozhi-esp32/main/boards/otto-robot) | 板卡、音频、显示、原 Otto 与鸭形控制器 |
| [duck_controller.cc](xiaozhi-esp32/main/boards/otto-robot/duck_controller.cc) | 鸭形动作、校准、停止与工具注册 |
| [config.json](xiaozhi-esp32/main/boards/otto-robot/config.json) | 两个变体的构建配置 |
| [主机测试](xiaozhi-esp32/scripts/tests) | 固件逻辑、音频和动作控制回归测试 |

## 来源与许可

| 项目 | 来源或接入依据 |
| --- | --- |
| 固件基线 | [txp666/xiaozhi-esp32](https://github.com/txp666/xiaozhi-esp32)，导入提交 `fb7f8152628f07a3f1bca91c222d3bc8853b1f70` |
| `v0` 标签 | 导入时固件目录与上述提交逐文件一致，用于区分上游基础与后续改动 |
| 灵矽 OTA | <https://xrobo.qiniuapi.com/v1/ota/> |
| 语音与工具协议 | [WebSocket](https://linx.qiniu.com/docs/xrobot/platform/websocket)、[设备 MCP](https://linx.qiniu.com/docs/xrobot/mcp/hardware-mcp) |
| 鸭形结构与动作参考 | [FlashDuck](https://github.com/FlashCat-Jordan/Flashduck)、[MakerWorld 模型](https://makerworld.com.cn/zh/models/3146116-shan-mao-zao-wu-flashduckxiao-huang-ya-microduckmi) |
| 议题参考 | [MicroDuck 官方资料](https://pollen-robotics.com/microduck/)、[官方源码](https://github.com/pollen-robotics/microduck) |

小智已有网络、音频、MCP、三种会话模式和 AFE 框架，所用分支也已包含 Otto 步态。本项目的新增工作和验证结果见[系统架构](../docs/ARCHITECTURE.md)，不将整套基础能力计为从零实现。

- 固件根目录保留上游 [MIT LICENSE](xiaozhi-esp32/LICENSE)；部分第三方文件保留各自来源标记，例如 [oscillator.h](xiaozhi-esp32/main/boards/otto-robot/oscillator.h) 中的 GPL 标记。
- 模型授权与固件授权分别适用。本仓库只记录模型来源，不转载模型文件；修改、分发和商业使用以对应下载页许可为准。
- 复用和分发代码时保留对应版权与许可说明；字体、音频和其他组件遵守各自许可。
