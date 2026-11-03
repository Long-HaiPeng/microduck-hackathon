# 固件

完整可构建源码已从以下基线导入（`v0` 标签）：

- 上游：<https://github.com/txp666/xiaozhi-esp32>
- 固定提交：`fb7f8152628f07a3f1bca91c222d3bc8853b1f70`
- 目标芯片：ESP32-S3
- 板型：`otto-robot`，支持摄像头版与无摄像头版配置、6 舵机；实际板型和引脚需结合实物核对
- 工具链：ESP-IDF 6.1

`v0` 导入时，`firmware/xiaozhi-esp32/` 与上游 `fb7f815` 逐文件一致；
之后已加入本项目的语音接入、动作保护和鸭形适配。构建与验收状态见
[ARCHITECTURE.md](../docs/ARCHITECTURE.md)「实现状态」。

板型配置按实物核对：可进入烧录模式，屏幕与音频正常；舵机引脚映射见 [hardware/BOM.md](../hardware/BOM.md)。

`v0` 之后的灵矽适配、安全控制和文档更新都通过独立 PR 提交。上游 LICENSE、第三方许可和来源信息见 [RESEARCH.md](../docs/RESEARCH.md)「许可与来源」。

## 演示与仓库固件

- [约 47 秒实物演示](../docs/media/demo.mp4)使用 FlashDuck 官方固件接入灵矽（linx），展示语音控制摇头、跳舞、坐下和归位。该视频记录的是实拍版本的动作效果。
- 本仓库的 `otto-duck` 固件基于小智/Otto 二次开发，已有构建和主机测试记录，实际板卡引脚、动作行程和真机联动仍待验收。上述视频不能作为该变体的真机验收结果。
